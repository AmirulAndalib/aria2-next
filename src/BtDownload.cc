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
#include "BufferedFile.h"
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
  impl->sourceTrackers = impl->params.trackers;
  impl->sourceTrackerTiers = impl->params.tracker_tiers;
  return impl;
}

bool hashesMatch(const lt::info_hash_t& expected, const lt::info_hash_t& actual)
{
  return (!expected.has_v1() ||
          (actual.has_v1() && expected.v1 == actual.v1)) &&
         (!expected.has_v2() || (actual.has_v2() && expected.v2 == actual.v2));
}

bool unsupportedTracker(const std::string& url)
{
  return util::startsWith(url, "ws://") || util::startsWith(url, "wss://");
}

} // namespace

BtDownload::BtDownload(std::unique_ptr<Impl> impl, Source source)
    : impl_(std::move(impl)), source_(source)
{
  auto attrs = BtMetadata();
  assignHashes(&attrs, snapshot_, impl_->params.info_hashes);
  snapshot_.name = impl_->params.name;
  snapshot_.announceList = announceList(impl_->params);
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
  impl_->params.trackers = impl_->sourceTrackers;
  impl_->params.tracker_tiers = impl_->sourceTrackerTiers;
  std::vector<std::string> excludedTrackers;
  const auto& excluded = option->get(PREF_BT_EXCLUDE_TRACKER);
  util::split(excluded.begin(), excluded.end(),
              std::back_inserter(excludedTrackers), ',', true);
  if (std::find(excludedTrackers.begin(), excludedTrackers.end(), "*") !=
      excludedTrackers.end()) {
    impl_->params.trackers.clear();
    impl_->params.tracker_tiers.clear();
  }
  for (size_t i = impl_->params.trackers.size(); i > 0; --i) {
    const auto index = i - 1;
    if (std::find(excludedTrackers.begin(), excludedTrackers.end(),
                  impl_->params.trackers[index]) != excludedTrackers.end()) {
      impl_->params.trackers.erase(impl_->params.trackers.begin() + index);
      if (index < impl_->params.tracker_tiers.size()) {
        impl_->params.tracker_tiers.erase(impl_->params.tracker_tiers.begin() +
                                          index);
      }
    }
  }
  std::vector<std::string> addedTrackers;
  const auto& added = option->get(PREF_BT_TRACKER);
  util::split(added.begin(), added.end(), std::back_inserter(addedTrackers),
              ',', true);
  const bool metadataUnknownMagnet =
      source_ == Source::Magnet && !impl_->params.ti;
  const bool privateTorrent = impl_->params.ti && impl_->params.ti->priv();
  if (!metadataUnknownMagnet && !privateTorrent) {
    std::vector<std::string> usableTrackers;
    for (auto tracker : addedTrackers) {
      tracker = util::strip(tracker);
      if (tracker.empty() ||
          std::find(impl_->params.trackers.begin(), impl_->params.trackers.end(),
                    tracker) != impl_->params.trackers.end()) {
        continue;
      }
      if (unsupportedTracker(tracker)) {
        A2_LOG_DEBUG(fmt("Ignoring unsupported WebTorrent tracker: %s",
                         tracker.c_str()));
        continue;
      }
      if (std::find(usableTrackers.begin(), usableTrackers.end(), tracker) ==
          usableTrackers.end()) {
        usableTrackers.push_back(std::move(tracker));
      }
    }

    const int baseTier = impl_->params.tracker_tiers.empty()
                             ? 0
                             : *std::max_element(
                                   impl_->params.tracker_tiers.begin(),
                                   impl_->params.tracker_tiers.end()) +
                                   1;
    if (baseTier > std::numeric_limits<uint8_t>::max()) {
      throw DL_ABORT_EX("Too many BitTorrent tracker tiers");
    }
    for (const auto& tracker : usableTrackers) {
      impl_->params.trackers.push_back(tracker);
      impl_->params.tracker_tiers.push_back(baseTier);
    }
  }
  snapshot_.announceList = announceList(impl_->params);
  snapshot_.magnetLink = lt::make_magnet_uri(impl_->params);
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
        lt::torrent_flags::sequential_download);
  impl_->params.flags |= lt::torrent_flags::duplicate_is_error;
  impl_->params.flags |= lt::torrent_flags::update_subscribe;
  impl_->params.flags |= lt::torrent_flags::apply_ip_filter;

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
        {entry->getPath(), entry->getLength(), 0, entry->isRequested()});
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
  for (size_t i = 0; i < count; ++i) {
    snapshot_.files[i].selected = files[i]->isRequested();
    snapshot_.files[i].path = files[i]->getPath();
  }
}

void BtDownload::initialize(RequestGroup* group)
{
  group_ = group;
  configure(group_->getOption().get());
  if (impl_->resumeLoaded) {
    return;
  }
  impl_->resumeLoaded = true;

  auto basePath = group_->getDownloadContext()->getBasePath();
  if (basePath.empty()) {
    basePath =
        util::applyDir(group_->getOption()->get(PREF_DIR),
                       !snapshot_.infoHashV1.empty() ? snapshot_.infoHashV1
                                                     : snapshot_.infoHashV2);
  }
  impl_->resumePath = basePath + ".aria2";

  BufferedFile resume(impl_->resumePath.c_str(), BufferedFile::READ);
  if (!resume) {
    return;
  }

  std::stringstream data;
  resume.transfer(data);
  lt::error_code error;
  auto restored = lt::read_resume_data(data.str(), error);
  if (error) {
    A2_LOG_INFO(fmt("Ignoring BitTorrent resume data %s: %s",
                    impl_->resumePath.c_str(), error.message().c_str()));
    return;
  }

  if (!hashesMatch(impl_->params.info_hashes, restored.info_hashes) ||
      (restored.ti &&
       !hashesMatch(impl_->params.info_hashes, restored.ti->info_hashes()))) {
    A2_LOG_INFO(
        fmt("Ignoring BitTorrent resume data with mismatched hashes: %s",
            impl_->resumePath.c_str()));
    return;
  }

  const bool sourceHasMetadata = static_cast<bool>(impl_->params.ti);
  if (sourceHasMetadata) {
    restored.ti = impl_->params.ti;
  }
  else if (restored.ti) {
    impl_->sourceTrackers = restored.trackers;
    impl_->sourceTrackerTiers = restored.tracker_tiers;
  }
  if (impl_->params.info_hashes.has_v1() ||
      impl_->params.info_hashes.has_v2()) {
    restored.info_hashes = impl_->params.info_hashes;
  }
  restored.save_path = impl_->params.save_path;
  restored.max_connections = impl_->params.max_connections;
  restored.upload_limit = impl_->params.upload_limit;
  restored.download_limit = impl_->params.download_limit;
  impl_->params = std::move(restored);
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

  if (source_ == Source::Magnet &&
      group_->getOption()->getAsBool(PREF_ENABLE_RPC) &&
      group_->getOption()->getAsBool(PREF_PAUSE_METADATA)) {
    snapshot_.state = BtSnapshot::State::AwaitingFileSelection;
    group_->setPauseRequested(true);
  }
}

bool BtDownload::hasMetadata() const { return snapshot_.hasMetadata; }

bool BtDownload::active() const
{
  return shutdownStage_ != ShutdownStage::Complete &&
         snapshot_.state != BtSnapshot::State::Error;
}

bool BtDownload::stopped() const
{
  return shutdownStage_ == ShutdownStage::Complete;
}

bool BtDownload::failed() const
{
  return snapshot_.state == BtSnapshot::State::Error;
}

bool BtDownload::awaitingFileSelection() const
{
  return snapshot_.state == BtSnapshot::State::AwaitingFileSelection;
}

bool BtDownload::shouldPauseAfterMetadata() const
{
  return source_ == Source::Magnet && !snapshot_.hasMetadata && group_ &&
         group_->getOption()->getAsBool(PREF_ENABLE_RPC) &&
         group_->getOption()->getAsBool(PREF_PAUSE_METADATA);
}

void BtDownload::requestStop(StopReason reason)
{
  if (shutdownStage_ != ShutdownStage::Idle) {
    if (reason == StopReason::FileSelection &&
        stopReason_ == StopReason::Pause) {
      stopReason_ = reason;
      snapshot_.state = BtSnapshot::State::AwaitingFileSelection;
    }
    return;
  }
  stopReason_ = reason;
  shutdownStage_ = ShutdownStage::PendingHandle;
  if (reason == StopReason::FileSelection) {
    snapshot_.state = BtSnapshot::State::AwaitingFileSelection;
  }
  else {
    snapshot_.state = BtSnapshot::State::Stopping;
  }
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
    snapshot_.state = BtSnapshot::State::AwaitingFileSelection;
    break;
  case StopReason::None:
  case StopReason::Stop:
    snapshot_.state = BtSnapshot::State::Stopped;
    break;
  }
}

void BtDownload::consumeMetadataPause()
{
  if (!awaitingFileSelection() || !group_) {
    return;
  }
  group_->getOption()->put(PREF_PAUSE_METADATA, A2_V_FALSE);
  impl_->fileSelectionResumePending = true;
  snapshot_.state = BtSnapshot::State::Adding;
}

void BtDownload::prepareStart()
{
  stopReason_ = StopReason::None;
  shutdownStage_ = ShutdownStage::Idle;
  snapshot_.errorMessage.clear();
  recoverableError_ = false;
  snapshot_.state = snapshot_.hasMetadata
                        ? BtSnapshot::State::Adding
                        : BtSnapshot::State::DownloadingMetadata;
}

} // namespace aria2
