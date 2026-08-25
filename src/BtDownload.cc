/* <!-- copyright */
/*
 * aria2 - The high speed download utility
 *
 * Copyright (C) 2026 The aria2-next contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
/* copyright --> */
#include "BtDownload.h"
#include "BtDownloadImpl.h"

#include <algorithm>
#include <limits>
#include <map>
#include <sstream>
#include <utility>

#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/file_storage.hpp>
#include <libtorrent/load_torrent.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/read_resume_data.hpp>
#include <libtorrent/storage_defs.hpp>
#include <libtorrent/torrent_flags.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <libtorrent/torrent_info.hpp>

#include "DlAbortEx.h"
#include "BtResumeStore.h"
#include "DownloadContext.h"
#include "FileEntry.h"
#include "Log.h"
#include "Option.h"
#include "RequestGroup.h"
#include "BtMetadata.h"
#include "fmt.h"
#include "prefs.h"
#include "util.h"

namespace aria2 {

namespace lt = libtorrent;

namespace {

std::string hashBytes(const lt::sha1_hash& hash)
{
  return {reinterpret_cast<const char*>(hash.data()),
          static_cast<size_t>(hash.size())};
}

std::string hashBytes(const lt::sha256_hash& hash)
{
  return {reinterpret_cast<const char*>(hash.data()),
          static_cast<size_t>(hash.size())};
}

std::string hashHex(const lt::sha1_hash& hash)
{
  return util::toHex(hashBytes(hash));
}

std::string hashHex(const lt::sha256_hash& hash)
{
  return util::toHex(hashBytes(hash));
}

void assignHashes(BtMetadata* attrs, BtSnapshot& snapshot,
                  const lt::info_hash_t& hashes)
{
  if (hashes.has_v1()) {
    attrs->infoHash = hashBytes(hashes.v1);
    snapshot.infoHashV1 = hashHex(hashes.v1);
  }
  if (hashes.has_v2()) {
    attrs->infoHashV2 = hashBytes(hashes.v2);
    snapshot.infoHashV2 = hashHex(hashes.v2);
    if (attrs->infoHash.empty()) {
      attrs->infoHash = attrs->infoHashV2;
    }
  }
}

std::vector<std::vector<std::string>>
announceList(const lt::add_torrent_params& params)
{
  std::map<int, std::vector<std::string>> tiers;
  for (size_t i = 0; i < params.trackers.size(); ++i) {
    const int tier =
        i < params.tracker_tiers.size() ? params.tracker_tiers[i] : 0;
    tiers[tier].push_back(params.trackers[i]);
  }

  std::vector<std::vector<std::string>> result;
  result.reserve(tiers.size());
  for (auto& entry : tiers) {
    result.push_back(std::move(entry.second));
  }
  return result;
}

std::unique_ptr<BtDownload::Impl>
makeImpl(lt::add_torrent_params params,
         const std::vector<std::string>& webSeeds)
{
  params.url_seeds.insert(params.url_seeds.end(), webSeeds.begin(),
                          webSeeds.end());
  auto impl = make_unique<BtDownload::Impl>();
  impl->params = std::move(params);
  const auto origin = impl->params.ti ? BtTrackerOrigin::Metainfo
                                     : BtTrackerOrigin::Magnet;
  for (size_t i = 0; i < impl->params.trackers.size(); ++i) {
    impl->sourceTrackers.push_back(
        {impl->params.trackers[i],
         i < impl->params.tracker_tiers.size()
             ? impl->params.tracker_tiers[i]
             : 0,
         origin});
  }
  impl->effectiveTrackers = impl->sourceTrackers;
  return impl;
}

bool hashesMatch(const lt::info_hash_t& expected, const lt::info_hash_t& actual)
{
  return (!expected.has_v1() ||
          (actual.has_v1() && expected.v1 == actual.v1)) &&
         (!expected.has_v2() || (actual.has_v2() && expected.v2 == actual.v2));
}

bool hasAllPieces(const lt::typed_bitfield<lt::piece_index_t>& pieces,
                  int pieceCount)
{
  if (pieces.size() < pieceCount) {
    return false;
  }
  for (int index = 0; index < pieceCount; ++index) {
    if (!pieces[lt::piece_index_t{index}]) {
      return false;
    }
  }
  return true;
}

bool unsupportedTracker(const std::string& url)
{
  return util::startsWith(url, "ws://") || util::startsWith(url, "wss://");
}

const char* trackerOriginName(BtTrackerOrigin origin)
{
  switch (origin) {
  case BtTrackerOrigin::Metainfo:
    return "metainfo";
  case BtTrackerOrigin::Magnet:
    return "magnet";
  case BtTrackerOrigin::Resume:
    return "resume";
  case BtTrackerOrigin::Global:
    return "global";
  case BtTrackerOrigin::Rpc:
    return "rpc";
  }
  return "unknown";
}

std::vector<BtTrackerSpec>
trackerSpecs(const lt::add_torrent_params& params, BtTrackerOrigin origin)
{
  std::vector<BtTrackerSpec> result;
  result.reserve(params.trackers.size());
  for (size_t i = 0; i < params.trackers.size(); ++i) {
    result.push_back({params.trackers[i],
                      i < params.tracker_tiers.size()
                          ? params.tracker_tiers[i]
                          : 0,
                      origin});
  }
  return result;
}

size_t normalizeTrackerTiers(std::vector<BtTrackerSpec>& trackers,
                             int highestTier)
{
  if (trackers.empty()) {
    return 0;
  }

  std::vector<int> declaredTiers;
  declaredTiers.reserve(trackers.size());
  for (const auto& tracker : trackers) {
    declaredTiers.push_back(tracker.tier);
  }
  std::sort(declaredTiers.begin(), declaredTiers.end());
  declaredTiers.erase(
      std::unique(declaredTiers.begin(), declaredTiers.end()),
      declaredTiers.end());

  const auto highestRank = static_cast<size_t>(highestTier);
  for (auto& tracker : trackers) {
    const auto rank = static_cast<size_t>(
        std::lower_bound(declaredTiers.begin(), declaredTiers.end(),
                         tracker.tier) -
        declaredTiers.begin());
    tracker.tier = static_cast<int>(std::min(rank, highestRank));
  }
  return declaredTiers.size();
}

} // namespace

BtDownload::BtDownload(std::unique_ptr<Impl> impl, Source source)
    : impl_(std::move(impl)), source_(source)
{
  auto attrs = BtMetadata();
  assignHashes(&attrs, snapshot_, impl_->params.info_hashes);
  snapshot_.name = impl_->params.name;
  snapshot_.announceList = announceList(impl_->params);
  snapshot_.webSeeds = impl_->params.url_seeds;
  snapshot_.magnetLink = lt::make_magnet_uri(impl_->params);
  snapshot_.hasMetadata = static_cast<bool>(impl_->params.ti);
  snapshot_.state = snapshot_.hasMetadata
                        ? BtSnapshot::State::Adding
                        : BtSnapshot::State::DownloadingMetadata;
}

BtDownload::~BtDownload() = default;

std::shared_ptr<BtDownload>
BtDownload::fromFile(const std::string& path,
                     const std::vector<std::string>& webSeeds)
{
  lt::error_code error;
  auto params = lt::load_torrent_file(path, error, {});
  if (error) {
    throw DL_ABORT_EX("Unable to load torrent file: " + error.message());
  }
  return std::shared_ptr<BtDownload>(
      new BtDownload(makeImpl(std::move(params), webSeeds), Source::Metainfo));
}

std::shared_ptr<BtDownload>
BtDownload::fromBuffer(const std::string& data,
                       const std::vector<std::string>& webSeeds)
{
  lt::error_code error;
  auto params = lt::load_torrent_buffer(
      {data.data(), static_cast<lt::span<char const>::index_type>(data.size())},
      error, {});
  if (error) {
    throw DL_ABORT_EX("Unable to decode torrent data: " + error.message());
  }
  return std::shared_ptr<BtDownload>(
      new BtDownload(makeImpl(std::move(params), webSeeds), Source::Metainfo));
}

std::shared_ptr<BtDownload> BtDownload::fromMagnet(const std::string& uri)
{
  lt::error_code error;
  auto params = lt::parse_magnet_uri(uri, error);
  if (error) {
    throw DL_ABORT_EX("Unable to parse magnet URI: " + error.message());
  }
  return std::shared_ptr<BtDownload>(
      new BtDownload(makeImpl(std::move(params), {}), Source::Magnet));
}

void BtDownload::configure(const Option* option)
{
  auto effectiveTrackers = impl_->sourceTrackers;

  if (!impl_->trackerOverride) {
    std::vector<std::string> excludedTrackers;
    const auto& excluded = option->get(PREF_BT_EXCLUDE_TRACKER);
    util::split(excluded.begin(), excluded.end(),
                std::back_inserter(excludedTrackers), ',', true);
    if (std::find(excludedTrackers.begin(), excludedTrackers.end(), "*") !=
        excludedTrackers.end()) {
      effectiveTrackers.clear();
    }
    effectiveTrackers.erase(
        std::remove_if(
            effectiveTrackers.begin(), effectiveTrackers.end(),
            [&excludedTrackers](const BtTrackerSpec& tracker) {
              return std::find(excludedTrackers.begin(),
                               excludedTrackers.end(), tracker.url) !=
                     excludedTrackers.end();
            }),
        effectiveTrackers.end());
    std::vector<BtTrackerSpec> uniqueTrackers;
    uniqueTrackers.reserve(effectiveTrackers.size());
    for (auto& tracker : effectiveTrackers) {
      if (tracker.url.empty() || unsupportedTracker(tracker.url) ||
          std::find_if(uniqueTrackers.begin(), uniqueTrackers.end(),
                       [&tracker](const BtTrackerSpec& entry) {
                         return entry.url == tracker.url;
                       }) != uniqueTrackers.end()) {
        continue;
      }
      uniqueTrackers.push_back(std::move(tracker));
    }
    effectiveTrackers = std::move(uniqueTrackers);
    std::vector<std::string> addedTrackers;
    const auto& added = option->get(PREF_BT_TRACKER);
    util::split(added.begin(), added.end(), std::back_inserter(addedTrackers),
                ',', true);
    const bool metadataUnknownMagnet =
        source_ == Source::Magnet && !impl_->params.ti;
    const bool privateTorrent = impl_->params.ti && impl_->params.ti->priv();
    std::vector<std::string> usableTrackers;
    if (!metadataUnknownMagnet && !privateTorrent) {
      for (auto tracker : addedTrackers) {
        tracker = util::strip(tracker);
        if (tracker.empty() ||
            std::find_if(effectiveTrackers.begin(), effectiveTrackers.end(),
                         [&tracker](const BtTrackerSpec& entry) {
                           return entry.url == tracker;
                         }) != effectiveTrackers.end()) {
          continue;
        }
        if (unsupportedTracker(tracker)) {
          A2_LOG_DEBUG(fmt("Ignoring unsupported WebTorrent tracker: %s",
                           logging::sanitizeUri(tracker).c_str()));
          continue;
        }
        if (std::find(usableTrackers.begin(), usableTrackers.end(), tracker) ==
            usableTrackers.end()) {
          usableTrackers.push_back(std::move(tracker));
        }
      }
    }

    constexpr int maxNativeTier =
        std::numeric_limits<std::uint8_t>::max();
    const int highestSourceTier =
        usableTrackers.empty() ? maxNativeTier : maxNativeTier - 1;
    const auto declaredTierCount =
        normalizeTrackerTiers(effectiveTrackers, highestSourceTier);
    const auto nativeSourceTierCount =
        static_cast<size_t>(highestSourceTier) + 1;
    if (declaredTierCount > nativeSourceTierCount &&
        !impl_->trackerTierCompressionReported) {
      A2_LOG_WARN(fmt("Compressed %lu source tracker tiers to %lu to fit "
                      "libtorrent's native tier range",
                      static_cast<unsigned long>(declaredTierCount),
                      static_cast<unsigned long>(nativeSourceTierCount)));
      impl_->trackerTierCompressionReported = true;
    }
    if (!usableTrackers.empty()) {
      const int baseTier =
          effectiveTrackers.empty()
              ? 0
              : std::max_element(
                    effectiveTrackers.begin(), effectiveTrackers.end(),
                    [](const BtTrackerSpec& lhs, const BtTrackerSpec& rhs) {
                      return lhs.tier < rhs.tier;
                    })
                        ->tier +
                    1;
      for (const auto& tracker : usableTrackers) {
        effectiveTrackers.push_back(
            {tracker, baseTier, BtTrackerOrigin::Global});
      }
    }
  }
  else {
    normalizeTrackerTiers(
        effectiveTrackers, std::numeric_limits<std::uint8_t>::max());
  }
  std::vector<std::string> trackers;
  std::vector<int> trackerTiers;
  trackers.reserve(effectiveTrackers.size());
  trackerTiers.reserve(effectiveTrackers.size());
  for (const auto& tracker : effectiveTrackers) {
    trackers.push_back(tracker.url);
    trackerTiers.push_back(tracker.tier);
  }
  if (trackers != impl_->params.trackers ||
      trackerTiers != impl_->params.tracker_tiers) {
    impl_->params.trackers = std::move(trackers);
    impl_->params.tracker_tiers = std::move(trackerTiers);
    ++impl_->trackerRevision;
  }
  impl_->effectiveTrackers = std::move(effectiveTrackers);
  snapshot_.announceList = announceList(impl_->params);
  snapshot_.magnetLink = lt::make_magnet_uri(impl_->params);
  snapshot_.webSeeds = impl_->params.url_seeds;
  if (group_ && group_->getDownloadContext()->hasAttribute(CTX_ATTR_BT)) {
    static_cast<BtMetadata*>(
        group_->getDownloadContext()->getAttribute(CTX_ATTR_BT).get())
        ->announceList = snapshot_.announceList;
  }

  impl_->params.save_path = option->get(PREF_DIR);
  impl_->params.max_connections = option->getAsInt(PREF_BT_MAX_PEERS);
  if (impl_->params.max_connections == 0) {
    impl_->params.max_connections = -1;
  }
  impl_->params.max_uploads = option->getAsInt(PREF_BT_MAX_UPLOADS_PER_TORRENT);
  impl_->params.upload_limit = option->getAsInt(PREF_MAX_UPLOAD_LIMIT);
  if (impl_->params.upload_limit == 0) {
    impl_->params.upload_limit = -1;
  }
  impl_->params.download_limit = option->getAsInt(PREF_MAX_DOWNLOAD_LIMIT);
  if (impl_->params.download_limit == 0) {
    impl_->params.download_limit = -1;
  }
  impl_->params.storage_mode = option->get(PREF_FILE_ALLOCATION) == V_PREALLOC
                                   ? lt::storage_mode_allocate
                                   : lt::storage_mode_sparse;

  impl_->params.flags &=
      ~(lt::torrent_flags::auto_managed | lt::torrent_flags::paused |
        lt::torrent_flags::disable_dht | lt::torrent_flags::disable_pex |
        lt::torrent_flags::disable_lsd | lt::torrent_flags::seed_mode |
        lt::torrent_flags::default_dont_download |
        lt::torrent_flags::sequential_download |
        lt::torrent_flags::super_seeding |
        lt::torrent_flags::stop_when_ready);
  impl_->params.flags |= lt::torrent_flags::duplicate_is_error;
  impl_->params.flags |= lt::torrent_flags::update_subscribe;
  impl_->params.flags |= lt::torrent_flags::apply_ip_filter;
  impl_->params.flags |= lt::torrent_flags::deprecated_override_trackers;

  if (!option->getAsBool(PREF_ENABLE_DHT)) {
    impl_->params.flags |= lt::torrent_flags::disable_dht;
  }
  if (!option->getAsBool(PREF_ENABLE_PEER_EXCHANGE)) {
    impl_->params.flags |= lt::torrent_flags::disable_pex;
  }
  if (!option->getAsBool(PREF_BT_ENABLE_LPD)) {
    impl_->params.flags |= lt::torrent_flags::disable_lsd;
  }
  if (option->getAsBool(PREF_BT_SEED_UNVERIFIED) && impl_->params.ti) {
    impl_->params.flags |= lt::torrent_flags::seed_mode;
  }
  if (option->getAsBool(PREF_FORCE_SEQUENTIAL)) {
    impl_->params.flags |= lt::torrent_flags::sequential_download;
  }
  if (option->getAsBool(PREF_BT_SUPER_SEEDING)) {
    impl_->params.flags |= lt::torrent_flags::super_seeding;
  }
  if (source_ == Source::Magnet && !impl_->params.ti &&
      option->getAsBool(PREF_ENABLE_RPC) &&
      option->getAsBool(PREF_PAUSE_METADATA)) {
    impl_->params.flags |= lt::torrent_flags::default_dont_download;
  }
}

void BtDownload::populateDownloadContext(
    const std::shared_ptr<DownloadContext>& context, const Option* option)
{
  auto attrs = std::make_shared<BtMetadata>();
  assignHashes(attrs.get(), snapshot_, impl_->params.info_hashes);
  attrs->announceList = announceList(impl_->params);
  attrs->name = snapshot_.name;

  if (!impl_->params.ti) {
    context->markTotalLengthIsUnknown();
    auto name = impl_->params.name;
    if (name.empty()) {
      name = !snapshot_.infoHashV1.empty() ? snapshot_.infoHashV1
                                           : snapshot_.infoHashV2;
    }
    context->getFirstFileEntry()->setPath(
        util::applyDir(option->get(PREF_DIR), name));
    context->setAttribute(CTX_ATTR_BT, std::move(attrs));
    context->setAcceptMetalink(false);
    return;
  }

  const auto& info = *impl_->params.ti;
  const auto& files = info.layout();
  std::vector<std::shared_ptr<FileEntry>> entries;
  entries.reserve(static_cast<size_t>(files.num_files()));
  for (lt::file_index_t index{0}; index < files.end_file(); ++index) {
    entries.push_back(std::make_shared<FileEntry>(
        util::applyDir(option->get(PREF_DIR), files.file_path(index)),
        files.file_size(index), files.file_offset(index)));
  }
  context->setFileEntries(entries.begin(), entries.end());
  context->setPieceLength(info.piece_length());
  context->markTotalLengthIsKnown();
  context->setBasePath(util::applyDir(option->get(PREF_DIR), info.name()));

  attrs->name = info.name();
  attrs->mode =
      files.num_files() > 1 ? BT_FILE_MODE_MULTI : BT_FILE_MODE_SINGLE;
  attrs->privateTorrent = info.priv();
  attrs->creationDate = impl_->params.creation_date;
  attrs->comment = impl_->params.comment;
  attrs->createdBy = impl_->params.created_by;
  snapshot_.name = attrs->name;
  snapshot_.privateTorrent = attrs->privateTorrent;
  snapshot_.hasMetadata = true;
  snapshot_.totalLength = context->getTotalLength();
  snapshot_.files.clear();
  for (const auto& entry : entries) {
    snapshot_.files.push_back(
        {entry->getPath(), entry->getLength(), 0, 1, entry->isRequested()});
  }
  context->setAttribute(CTX_ATTR_BT, std::move(attrs));
  context->setAcceptMetalink(false);
}

void BtDownload::updateFilePaths(
    const std::shared_ptr<DownloadContext>& context, const Option* option) const
{
  if (!impl_->params.ti) {
    return;
  }
  context->setBasePath(
      util::applyDir(option->get(PREF_DIR), impl_->params.ti->name()));
  const auto& files = impl_->params.ti->layout();
  for (lt::file_index_t index{0}; index < files.end_file(); ++index) {
    context->setFilePathWithIndex(
        static_cast<size_t>(static_cast<int>(index)) + 1,
        util::applyDir(option->get(PREF_DIR), files.file_path(index)));
  }
  std::istringstream indexOut(option->get(PREF_INDEX_OUT));
  for (const auto& entry : util::createIndexPaths(indexOut)) {
    context->setFilePathWithIndex(
        entry.first, util::applyDir(option->get(PREF_DIR), entry.second));
  }
}

void BtDownload::updateSelection(
    const std::shared_ptr<DownloadContext>& context)
{
  const auto& files = context->getFileEntries();
  const auto count = std::min(files.size(), snapshot_.files.size());
  int64_t selectedLength = 0;
  int64_t selectedCompletedLength = 0;
  const auto addLength = [](int64_t current, int64_t value) {
    value = std::max<int64_t>(0, value);
    return current > std::numeric_limits<int64_t>::max() - value
               ? std::numeric_limits<int64_t>::max()
               : current + value;
  };
  for (size_t i = 0; i < count; ++i) {
    snapshot_.files[i].selected = files[i]->isRequested();
    snapshot_.files[i].path = files[i]->getPath();
    if (snapshot_.files[i].selected) {
      selectedLength = addLength(selectedLength, snapshot_.files[i].length);
      selectedCompletedLength =
          addLength(selectedCompletedLength,
                    std::min(snapshot_.files[i].completedLength,
                             snapshot_.files[i].length));
    }
  }
  snapshot_.totalLength = selectedLength;
  snapshot_.completedLength = selectedCompletedLength;
  snapshot_.progressPpm =
      selectedLength > 0
          ? static_cast<int>(std::min<long double>(
                1000000.0L,
                static_cast<long double>(selectedCompletedLength) *
                    1000000.0L / selectedLength))
          : 0;
}

void BtDownload::restoreResumeProgress()
{
  if (!impl_->params.ti || impl_->params.have_pieces.empty() ||
      snapshot_.files.empty()) {
    return;
  }

  std::vector<int64_t> completed(snapshot_.files.size(), 0);
  const auto& info = *impl_->params.ti;
  const auto pieceCount = std::min(impl_->params.have_pieces.size(),
                                   info.num_pieces());
  for (int index = 0; index < pieceCount; ++index) {
    const auto piece = lt::piece_index_t{index};
    if (!impl_->params.have_pieces[piece]) {
      continue;
    }
    for (const auto& slice :
         info.map_block(piece, 0, info.piece_size(piece))) {
      const auto file = static_cast<size_t>(static_cast<int>(slice.file_index));
      if (file >= completed.size()) {
        continue;
      }
      const auto remaining =
          std::max<int64_t>(0, snapshot_.files[file].length - completed[file]);
      completed[file] += std::min<int64_t>(remaining, slice.size);
    }
  }
  for (size_t index = 0; index < completed.size(); ++index) {
    snapshot_.files[index].completedLength = completed[index];
  }
  updateSelection(group_->getDownloadContext());
  snapshot_.selectedComplete =
      snapshot_.hasMetadata &&
      snapshot_.completedLength == snapshot_.totalLength;
  snapshot_.complete =
      hasAllPieces(impl_->params.have_pieces, info.num_pieces());
}

void BtDownload::refreshLogicalProgress()
{
  if (!group_) {
    return;
  }
  updateSelection(group_->getDownloadContext());
  snapshot_.selectedComplete =
      progressState_ == ProgressState::Stable &&
      snapshot_.fileSelectionState == BtSnapshot::FileSelectionState::None &&
      !snapshot_.error.present && snapshot_.totalLength > 0 &&
      snapshot_.completedLength == snapshot_.totalLength;
}

void BtDownload::beginProgressVerification()
{
  progressState_ = ProgressState::Verifying;
  snapshot_.selectedComplete = false;
}

void BtDownload::beginSelectionProgressHold()
{
  progressState_ = ProgressState::Selecting;
  snapshot_.selectedComplete = false;
  snapshot_.complete = false;
}

void BtDownload::beginProgressRefresh()
{
  progressState_ = ProgressState::Refreshing;
  snapshot_.selectedComplete = false;
}

void BtDownload::applyFileProgress(
    const std::vector<int64_t>& completedLengths)
{
  if (progressState_ == ProgressState::Verifying ||
      progressState_ == ProgressState::Selecting) {
    return;
  }

  const auto count = std::min(snapshot_.files.size(),
                              completedLengths.size());
  for (size_t i = 0; i < count; ++i) {
    snapshot_.files[i].completedLength = std::clamp<int64_t>(
        completedLengths[i], 0, snapshot_.files[i].length);
  }
  progressState_ = ProgressState::Stable;
  refreshLogicalProgress();
}

void BtDownload::initialize(RequestGroup* group)
{
  group_ = group;
  impl_->gid = group_->getGID();
  configure(group_->getOption().get());
  if (impl_->resumeLoaded) {
    return;
  }
  impl_->resumeLoaded = true;

  const auto& identity = !snapshot_.infoHashV1.empty() ? snapshot_.infoHashV1
                                                       : snapshot_.infoHashV2;
  impl_->resumePath = BtResumeStore::path(group_->getOption().get(), identity);
  const auto resumeData = BtResumeStore::read(impl_->resumePath);
  if (resumeData.empty()) {
    return;
  }
  lt::error_code error;
  auto restored = lt::read_resume_data(resumeData, error);
  if (error) {
    A2_LOG_WARN(fmt("Ignoring BitTorrent resume data %s: %s",
                    impl_->resumePath.c_str(), error.message().c_str()));
    return;
  }

  if (!hashesMatch(impl_->params.info_hashes, restored.info_hashes) ||
      (restored.ti &&
       !hashesMatch(impl_->params.info_hashes, restored.ti->info_hashes()))) {
    A2_LOG_WARN(
        fmt("Ignoring BitTorrent resume data with mismatched hashes: %s",
            impl_->resumePath.c_str()));
    return;
  }

  const bool sourceHasMetadata = static_cast<bool>(impl_->params.ti);
  if (sourceHasMetadata) {
    restored.ti = impl_->params.ti;
  }
  else if (restored.ti) {
    impl_->sourceTrackers = trackerSpecs(restored, BtTrackerOrigin::Resume);
  }
  if (impl_->params.info_hashes.has_v1() ||
      impl_->params.info_hashes.has_v2()) {
    restored.info_hashes = impl_->params.info_hashes;
  }
  restored.save_path = impl_->params.save_path;
  restored.max_connections = impl_->params.max_connections;
  restored.max_uploads = impl_->params.max_uploads;
  restored.upload_limit = impl_->params.upload_limit;
  restored.download_limit = impl_->params.download_limit;
  impl_->params = std::move(restored);
  completionNotified_ = impl_->params.completed_time != 0 ||
                        impl_->params.finished_time > 0 ||
                        impl_->params.seeding_time > 0;
  beginProgressVerification();
  snapshot_.allTimeDownload =
      std::max<int64_t>(0, impl_->params.total_downloaded);
  snapshot_.allTimeUpload = std::max<int64_t>(0, impl_->params.total_uploaded);
  snapshot_.activeTime = std::max(0, impl_->params.active_time);
  snapshot_.finishedTime = std::max(0, impl_->params.finished_time);
  snapshot_.seedingTime = std::max(0, impl_->params.seeding_time);
  snapshot_.complete =
      impl_->params.ti &&
      hasAllPieces(impl_->params.have_pieces,
                   impl_->params.ti->num_pieces());
  snapshot_.selectedComplete =
      impl_->params.completed_time != 0 || snapshot_.complete;
  configure(group_->getOption().get());

  if (!impl_->params.ti) {
    return;
  }

  populateDownloadContext(group_->getDownloadContext(),
                          group_->getOption().get());
  auto selected =
      util::parseIntSegments(group_->getOption()->get(PREF_SELECT_FILE));
  selected.normalize();
  group_->getDownloadContext()->setFileFilter(std::move(selected));
  updateFilePaths(group_->getDownloadContext(), group_->getOption().get());
  updateSelection(group_->getDownloadContext());
  restoreResumeProgress();
  completionNotified_ = completionNotified_ || snapshot_.selectedComplete;

  if (group_->isPauseRequested()) {
    snapshot_.state = BtSnapshot::State::Paused;
  }

  if (source_ == Source::Magnet &&
      group_->getOption()->getAsBool(PREF_ENABLE_RPC) &&
      group_->getOption()->getAsBool(PREF_PAUSE_METADATA)) {
    beginFileSelectionPause();
    shutdownStage_ = ShutdownStage::Complete;
    group_->setPauseRequested(true);
  }
}

bool BtDownload::hasMetadata() const { return snapshot_.hasMetadata; }

std::string BtDownload::trackerSource(const std::string& url) const
{
  const auto found = std::find_if(
      impl_->effectiveTrackers.begin(), impl_->effectiveTrackers.end(),
      [&url](const BtTrackerSpec& entry) { return entry.url == url; });
  return found == impl_->effectiveTrackers.end()
             ? "unknown"
             : trackerOriginName(found->origin);
}

bool BtDownload::active() const
{
  return shutdownStage_ != ShutdownStage::Complete && !failed();
}

bool BtDownload::stopped() const
{
  return shutdownStage_ == ShutdownStage::Complete;
}

bool BtDownload::failed() const { return snapshot_.error.present; }

bool BtDownload::awaitingFileSelection() const
{
  return snapshot_.fileSelectionState ==
         BtSnapshot::FileSelectionState::Awaiting;
}

bool BtDownload::fileSelectionReady() const
{
  return snapshot_.fileSelectionState == BtSnapshot::FileSelectionState::Ready;
}

bool BtDownload::fileSelectionApplying() const
{
  return snapshot_.fileSelectionState ==
         BtSnapshot::FileSelectionState::Applying;
}

bool BtDownload::shouldPauseAfterMetadata() const
{
  return source_ == Source::Magnet &&
         snapshot_.fileSelectionState ==
             BtSnapshot::FileSelectionState::None &&
         group_ &&
         group_->getOption()->getAsBool(PREF_ENABLE_RPC) &&
         group_->getOption()->getAsBool(PREF_PAUSE_METADATA);
}

void BtDownload::requestStop(StopReason reason)
{
  if (shutdownStage_ != ShutdownStage::Idle) {
    if (reason == StopReason::Stop) {
      stopReason_ = reason;
      snapshot_.state = BtSnapshot::State::Stopping;
      return;
    }
    if (reason == StopReason::FileSelection &&
        stopReason_ == StopReason::Pause) {
      stopReason_ = reason;
      snapshot_.state = BtSnapshot::State::Stopping;
    }
    return;
  }
  stopReason_ = reason;
  shutdownStage_ = ShutdownStage::PendingHandle;
  snapshot_.state = BtSnapshot::State::Stopping;
}

void BtDownload::beginSavingResume()
{
  shutdownStage_ = ShutdownStage::SavingResume;
}

void BtDownload::beginRemoving() { shutdownStage_ = ShutdownStage::Removing; }

void BtDownload::finishStopping()
{
  shutdownStage_ = ShutdownStage::Complete;
  switch (stopReason_) {
  case StopReason::Pause:
    snapshot_.state = BtSnapshot::State::Paused;
    break;
  case StopReason::FileSelection:
    snapshot_.state = BtSnapshot::State::Paused;
    break;
  case StopReason::None:
  case StopReason::Stop:
    snapshot_.state = BtSnapshot::State::Stopped;
    break;
  }
}

std::string BtDownload::fileSelectionError(const Option* option) const
{
  if (!snapshot_.hasMetadata || snapshot_.files.empty()) {
    return "BitTorrent metadata is unavailable for file selection";
  }
  if (!option || !option->defined(PREF_SELECT_FILE) ||
      option->blank(PREF_SELECT_FILE)) {
    return "BitTorrent file selection requires select-file";
  }
  auto selected = util::parseIntSegments(option->get(PREF_SELECT_FILE));
  selected.normalize();
  if (!selected.hasNext()) {
    return "BitTorrent file selection requires select-file";
  }
  while (selected.hasNext()) {
    const auto index = selected.next();
    if (index < 1 || static_cast<size_t>(index) > snapshot_.files.size()) {
      return fmt("BitTorrent select-file index %d is out of range", index);
    }
  }
  return {};
}

void BtDownload::validateFileSelection(const Option* option) const
{
  const auto error = fileSelectionError(option);
  if (!error.empty()) {
    throw DL_ABORT_EX(error);
  }
}

void BtDownload::beginFileSelectionPause()
{
  stopReason_ = StopReason::FileSelection;
  beginSelectionProgressHold();
  if (fileSelectionError(group_ ? group_->getOption().get() : nullptr)
          .empty()) {
    snapshot_.fileSelectionState = BtSnapshot::FileSelectionState::Ready;
  }
  else {
    snapshot_.fileSelectionState = BtSnapshot::FileSelectionState::Awaiting;
  }
  snapshot_.state = BtSnapshot::State::Paused;
  snapshot_.progressPpm = 0;
}

void BtDownload::submitFileSelection(const Option* option)
{
  validateFileSelection(option);
  if (snapshot_.fileSelectionState ==
          BtSnapshot::FileSelectionState::Awaiting ||
      snapshot_.fileSelectionState == BtSnapshot::FileSelectionState::Ready) {
    snapshot_.fileSelectionState = BtSnapshot::FileSelectionState::Ready;
    snapshot_.state = snapshot_.error.present ? BtSnapshot::State::Error
                                               : BtSnapshot::State::Paused;
    snapshot_.selectedComplete = false;
  }
}

void BtDownload::beginFileSelectionApply()
{
  if (awaitingFileSelection()) {
    throw DL_ABORT_EX(
        "BitTorrent download is awaiting a valid select-file option");
  }
  if (!fileSelectionReady() || !group_) {
    return;
  }
  validateFileSelection(group_->getOption().get());
  group_->getOption()->put(PREF_PAUSE_METADATA, A2_V_FALSE);
  beginSelectionProgressHold();
  snapshot_.fileSelectionState = BtSnapshot::FileSelectionState::Applying;
  snapshot_.state = BtSnapshot::State::Adding;
}

void BtDownload::completeFileSelectionApply()
{
  if (fileSelectionApplying()) {
    snapshot_.fileSelectionState = BtSnapshot::FileSelectionState::None;
    if (snapshot_.state == BtSnapshot::State::Paused) {
      snapshot_.state = BtSnapshot::State::Adding;
    }
  }
}

void BtDownload::failFileSelectionApply()
{
  if (fileSelectionApplying()) {
    progressState_ = ProgressState::Stable;
    snapshot_.fileSelectionState = BtSnapshot::FileSelectionState::Ready;
    snapshot_.state = BtSnapshot::State::Paused;
    snapshot_.selectedComplete = false;
  }
}

void BtDownload::applyTransportState(BtSnapshot::State state)
{
  if (snapshot_.error.present) {
    snapshot_.state = BtSnapshot::State::Error;
    return;
  }
  if (impl_->recheckAfterAdd &&
      (impl_->nativeState == BtNativeState::Adding ||
       impl_->nativeState == BtNativeState::Removing)) {
    snapshot_.state = BtSnapshot::State::Recovering;
    return;
  }
  if ((group_ && group_->isPauseRequested()) || awaitingFileSelection() ||
      fileSelectionReady()) {
    snapshot_.state = BtSnapshot::State::Paused;
    return;
  }
  if (shutdownStage_ != ShutdownStage::Idle) {
    return;
  }
  snapshot_.state = state;
}

void BtDownload::setError(std::string message)
{
  BtErrorSnapshot error;
  error.present = true;
  error.kind = "engine";
  error.category = "aria2";
  error.message = std::move(message);
  setError(std::move(error));
}

void BtDownload::setError(BtErrorSnapshot error)
{
  error.present = true;
  snapshot_.error = std::move(error);
  snapshot_.state = BtSnapshot::State::Error;
  snapshot_.selectedComplete = false;
}

void BtDownload::clearError()
{
  snapshot_.error = {};
}

bool BtDownload::takeCompletionNotification()
{
  if (!snapshot_.selectedComplete || completionNotified_) {
    return false;
  }
  completionNotified_ = true;
  return true;
}

void BtDownload::prepareStart()
{
  if (awaitingFileSelection()) {
    throw DL_ABORT_EX(
        "BitTorrent download is awaiting a valid select-file option");
  }
  if (fileSelectionReady()) {
    throw DL_ABORT_EX("BitTorrent file selection has not been resumed");
  }
  stopReason_ = StopReason::None;
  shutdownStage_ = ShutdownStage::Idle;
  impl_->payloadDownloaded = 0;
  impl_->payloadUploaded = 0;
  clearError();
  if (!fileSelectionApplying()) {
    snapshot_.fileSelectionState = BtSnapshot::FileSelectionState::None;
  }
  snapshot_.state = snapshot_.hasMetadata
                        ? BtSnapshot::State::Adding
                        : BtSnapshot::State::DownloadingMetadata;
}

} // namespace aria2
