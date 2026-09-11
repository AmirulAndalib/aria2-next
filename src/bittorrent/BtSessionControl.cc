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
#include "RequestGroup.h"
#include <libtorrent/session.hpp>
#include "BtDownload.h"
#include "BtDownloadImpl.h"
#include "BtPeerBlocklist.h"
#include "BtSession.h"
#include "BtSettings.h"
#include "BtSnapshot.h"
#include "Option.h"
#include "RecoverableException.h"
#include "prefs.h"
#include "uri.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <libtorrent/address.hpp>
#include <libtorrent/announce_entry.hpp>
#include <libtorrent/error_code.hpp>
#include <libtorrent/info_hash.hpp>
#include <libtorrent/ip_filter.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/socket.hpp>
#include <libtorrent/torrent_flags.hpp>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "support/Numbers.h"
#include "DlAbortEx.h"
#include "support/Text.h"
#include "bittorrent/BtSessionInternal.h"

namespace aria2 {
namespace bt_session {
std::string hashKey(const lt::info_hash_t& hashes)
{
  if (hashes.has_v1()) {
    return std::string("v1:") +
           std::string(reinterpret_cast<const char*>(hashes.v1.data()),
                       static_cast<size_t>(hashes.v1.size()));
  }
  if (hashes.has_v2()) {
    return std::string("v2:") +
           std::string(reinterpret_cast<const char*>(hashes.v2.data()),
                       static_cast<size_t>(hashes.v2.size()));
  }
  return {};
}

lt::ip_filter makeIpFilter(const BtPeerBlocklist& blocklist)
{
  lt::ip_filter filter;
  for (const auto& range : blocklist.ipv4Ranges()) {
    lt::address_v4::bytes_type first{};
    lt::address_v4::bytes_type last{};
    std::copy_n(range.first.begin(), first.size(), first.begin());
    std::copy_n(range.last.begin(), last.size(), last.begin());
    filter.add_rule(lt::address_v4(first), lt::address_v4(last),
                    lt::ip_filter::blocked);
  }
  for (const auto& range : blocklist.ipv6Ranges()) {
    lt::address_v6::bytes_type first{};
    lt::address_v6::bytes_type last{};
    std::copy_n(range.first.begin(), first.size(), first.begin());
    std::copy_n(range.last.begin(), last.size(), last.begin());
    filter.add_rule(lt::address_v6(first), lt::address_v6(last),
                    lt::ip_filter::blocked);
  }
  return filter;
}
} // namespace bt_session
using namespace bt_session;

void BtSession::validateGlobalOptions(const Option* option) const
{
  makeBtConfig(option);
}

void BtSession::applyGlobalOptions(const Option* option)
{
  auto config = makeBtConfig(option);
  const bool networkChanged = !impl_->config.hasSameNetwork(config);
  impl_->session->apply_settings(config.settings);
  impl_->config = std::move(config);
  impl_->downloadRateLimit = option->getAsInt(PREF_MAX_OVERALL_DOWNLOAD_LIMIT);
  if (networkChanged) {
    impl_->listenEndpoints.clear();
    impl_->listenPort = 0;
  }
}

void BtSession::setGlobalDownloadLimit(int limit)
{
  limit = std::max(0, limit);
  if (impl_->downloadRateLimit == limit) {
    return;
  }
  lt::settings_pack settings;
  settings.set_int(lt::settings_pack::download_rate_limit, limit);
  impl_->session->apply_settings(settings);
  impl_->downloadRateLimit = limit;
}

bool BtSession::applyDownloadOptions(
    const std::shared_ptr<BtDownload>& download, const Option* option)
{
  return applyDownloadOptionsInternal(download, option,
                                      download &&
                                          !download->awaitingFileSelection() &&
                                          !download->fileSelectionReady());
}

bool BtSession::applyDownloadOptionsInternal(
    const std::shared_ptr<BtDownload>& download, const Option* option,
    bool synchronizeFileSelection)
{
  if (!download) {
    return false;
  }
  const auto previousSavePath = download->impl_->params.save_path;
  const auto previousTrackerRevision = download->impl_->trackerRevision;
  download->configure(option);
  download->updateSelection(download->group()->getDownloadContext());
  const auto& handle = download->impl_->handle;
  if (previousSavePath != download->impl_->params.save_path) {
    if (handle.is_valid()) {
      download->impl_->previousSavePath = previousSavePath;
      handle.move_storage(download->impl_->params.save_path);
    }
  }
  if (!handle.is_valid()) {
    return false;
  }
  handle.set_max_connections(download->impl_->params.max_connections);
  handle.set_max_uploads(download->impl_->params.max_uploads);
  handle.set_upload_limit(download->impl_->params.upload_limit);
  handle.set_download_limit(download->impl_->params.download_limit);
  const auto mask =
      lt::torrent_flags::disable_dht | lt::torrent_flags::disable_pex |
      lt::torrent_flags::disable_lsd | lt::torrent_flags::sequential_download |
      lt::torrent_flags::super_seeding;
  handle.set_flags(download->impl_->params.flags & mask, mask);
  if (download->impl_->appliedTrackerRevision !=
      download->impl_->trackerRevision) {
    std::vector<lt::announce_entry> trackers;
    trackers.reserve(download->impl_->params.trackers.size());
    for (size_t i = 0; i < download->impl_->params.trackers.size(); ++i) {
      lt::announce_entry tracker(download->impl_->params.trackers[i]);
      tracker.tier = i < download->impl_->params.tracker_tiers.size()
                         ? download->impl_->params.tracker_tiers[i]
                         : 0;
      trackers.push_back(std::move(tracker));
    }
    handle.replace_trackers(trackers);
    download->impl_->appliedTrackerRevision = download->impl_->trackerRevision;
    if (download->impl_->trackerRevision != previousTrackerRevision) {
      handle.force_reannounce(0, lt::torrent_handle::high_priority);
    }
  }
  const bool filePriorityUpdatePending =
      synchronizeFileSelection && synchronizeSelection(download.get());
  if (!filePriorityUpdatePending) {
    requestResumeCheckpoint(download.get());
  }
  return filePriorityUpdatePending;
}

void BtSession::forceRecheck(const std::shared_ptr<BtDownload>& download)
{
  if (!download || !download->impl_->handle.is_valid() ||
      !download->impl_->handle.in_session()) {
    throw DL_ABORT_EX("BitTorrent task is not present in the session");
  }
  download->beginProgressVerification();
  download->applyTransportState(BtSnapshot::State::Checking);
  download->impl_->handle.force_recheck();
}

void BtSession::forceAnnounce(const std::shared_ptr<BtDownload>& download)
{
  if (!download || !download->impl_->handle.is_valid() ||
      !download->impl_->handle.in_session()) {
    throw DL_ABORT_EX("BitTorrent task is not present in the session");
  }
  download->impl_->handle.force_reannounce();
  download->impl_->handle.force_dht_announce();
  download->impl_->handle.force_lsd_announce();
}

void BtSession::replaceTrackers(const std::shared_ptr<BtDownload>& download,
                                const std::vector<BtTrackerConfig>& trackers)
{
  if (!download || !download->group()) {
    throw DL_ABORT_EX("BitTorrent task is not available");
  }

  std::vector<BtTrackerSpec> entries;
  entries.reserve(trackers.size());
  for (const auto& tracker : trackers) {
    if (tracker.url.empty() || tracker.tier < 0 || tracker.tier > 255) {
      throw DL_ABORT_EX("Invalid BitTorrent tracker entry");
    }
    if (!util::startsWith(tracker.url, "http://") &&
        !util::startsWith(tracker.url, "https://") &&
        !util::startsWith(tracker.url, "udp://")) {
      throw DL_ABORT_EX("BitTorrent trackers must use HTTP, HTTPS, or UDP");
    }
    const auto found = std::find_if(entries.begin(), entries.end(),
                                    [&tracker](const BtTrackerSpec& entry) {
                                      return entry.url == tracker.url;
                                    });
    if (found != entries.end()) {
      continue;
    }
    entries.push_back({tracker.url, tracker.tier, BtTrackerOrigin::Rpc});
  }

  download->impl_->sourceTrackers = std::move(entries);
  download->impl_->trackerOverride = true;
  applyDownloadOptions(download, download->group()->getOption().get());
  requestResumeCheckpoint(download.get(), true);
}

void BtSession::replaceWebSeeds(const std::shared_ptr<BtDownload>& download,
                                const std::vector<std::string>& webSeeds)
{
  if (!download || !download->impl_->handle.is_valid() ||
      !download->impl_->handle.in_session()) {
    throw DL_ABORT_EX("BitTorrent task is not present in the session");
  }
  std::vector<std::string> normalized;
  normalized.reserve(webSeeds.size());
  for (const auto& webSeed : webSeeds) {
    uri::UriStruct parsed;
    if (!uri::parse(parsed, webSeed) || parsed.host.empty() ||
        (parsed.protocol != "http" && parsed.protocol != "https")) {
      throw DL_ABORT_EX("BitTorrent web seeds must use HTTP or HTTPS");
    }
    if (std::find(normalized.begin(), normalized.end(), webSeed) ==
        normalized.end()) {
      normalized.push_back(webSeed);
    }
  }

  const auto current = download->impl_->handle.url_seeds();
  try {
    for (const auto& webSeed : current) {
      if (std::find(normalized.begin(), normalized.end(), webSeed) ==
          normalized.end()) {
        download->impl_->handle.remove_url_seed(webSeed);
      }
    }
    for (const auto& webSeed : normalized) {
      if (current.find(webSeed) == current.end()) {
        download->impl_->handle.add_url_seed(webSeed);
      }
    }
  }
  catch (const std::exception& error) {
    const auto partial = download->impl_->handle.url_seeds();
    for (const auto& webSeed : partial) {
      if (current.find(webSeed) == current.end()) {
        download->impl_->handle.remove_url_seed(webSeed);
      }
    }
    for (const auto& webSeed : current) {
      if (partial.find(webSeed) == partial.end()) {
        download->impl_->handle.add_url_seed(webSeed);
      }
    }
    throw DL_ABORT_EX("Unable to replace BitTorrent web seeds: " +
                      std::string(error.what()));
  }
  download->impl_->params.url_seeds = normalized;
  download->snapshot_.webSeeds = normalized;
  requestResumeCheckpoint(download.get(), true);
}

std::pair<size_t, size_t>
BtSession::addPeers(const std::shared_ptr<BtDownload>& download,
                    const std::vector<std::string>& peers)
{
  if (!download || !download->impl_->handle.is_valid() ||
      !download->impl_->handle.in_session()) {
    throw DL_ABORT_EX("BitTorrent task is not present in the session");
  }
  size_t added = 0;
  size_t failed = 0;
  for (const auto& peer : peers) {
    std::string host;
    std::string portText;
    if (!peer.empty() && peer.front() == '[') {
      const auto closing = peer.find(']');
      if (closing != std::string::npos && closing + 1 < peer.size() &&
          peer[closing + 1] == ':') {
        host = peer.substr(1, closing - 1);
        portText = peer.substr(closing + 2);
      }
    }
    else {
      const auto separator = peer.rfind(':');
      if (separator != std::string::npos) {
        host = peer.substr(0, separator);
        portText = peer.substr(separator + 1);
      }
    }
    int32_t port = 0;
    lt::error_code error;
    const auto address = lt::make_address(host, error);
    if (host.empty() || error || !util::parseIntNoThrow(port, portText) ||
        port < 1 || port > UINT16_MAX) {
      ++failed;
      continue;
    }
    try {
      download->impl_->handle.connect_peer(
          lt::tcp::endpoint(address, static_cast<uint16_t>(port)));
      ++added;
    }
    catch (const std::exception&) {
      ++failed;
    }
  }
  return {added, failed};
}

bool BtSession::replaceIpFilter(const std::vector<std::string>& rules,
                                std::string& error)
{
  try {
    if (!impl_->blocklist.replace(rules, "RPC")) {
      return false;
    }
    impl_->session->set_ip_filter(makeIpFilter(impl_->blocklist));
    impl_->filterRevision = impl_->blocklist.revision();
    return true;
  }
  catch (RecoverableException& exception) {
    error = exception.what();
    return false;
  }
}

void BtSession::loadIpFilter(const std::string& path)
{
  impl_->blocklist.load(path);
  impl_->session->set_ip_filter(makeIpFilter(impl_->blocklist));
  impl_->filterRevision = impl_->blocklist.revision();
}

size_t BtSession::ipFilterRuleCount() const { return impl_->blocklist.count(); }

uint64_t BtSession::ipFilterRevision() const { return impl_->filterRevision; }

} // namespace aria2
