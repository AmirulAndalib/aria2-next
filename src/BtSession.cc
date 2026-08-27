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
#include "BtSession.h"

#include "ApplicationStatePath.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <climits>
#include <cmath>
#include <limits>
#include <map>
#include <sstream>
#include <utility>

#include <libtorrent/alert.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/address.hpp>
#include <libtorrent/announce_entry.hpp>
#include <libtorrent/close_reason.hpp>
#include <libtorrent/download_priority.hpp>
#include <libtorrent/error_code.hpp>
#include <libtorrent/ip_filter.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/session_params.hpp>
#include <libtorrent/session_stats.hpp>
#include <libtorrent/span.hpp>
#include <libtorrent/torrent_flags.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/write_resume_data.hpp>
#include <libtorrent/aux_/time.hpp>

#include "BtDownload.h"
#include "BtDownloadImpl.h"
#include "BtDownloadCommand.h"
#include "BtPeerBlocklist.h"
#include "BtResumeStore.h"
#include "BtSettings.h"
#include "BufferedFile.h"
#include "DownloadContext.h"
#include "DownloadEngine.h"
#include "File.h"
#include "FileEntry.h"
#include "Log.h"
#include "Option.h"
#include "RequestGroup.h"
#include "BtMetadata.h"
#include "fmt.h"
#include "prefs.h"
#include "util.h"
#include "uri.h"
#include "wallclock.h"

namespace aria2 {

namespace lt = libtorrent;

namespace {

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

void updatePayloadCounter(DownloadContext* context, int64_t current,
                          int64_t& previous, bool upload)
{
  current = std::max<int64_t>(0, current);
  const auto delta = current >= previous ? current - previous : current;
  previous = current;
  auto remaining = static_cast<uint64_t>(delta);
  while (remaining > 0) {
    const auto bytes = static_cast<size_t>(std::min<uint64_t>(
        remaining, std::numeric_limits<size_t>::max()));
    if (upload) {
      context->updateUpload(bytes);
    }
    else {
      context->updateDownload(bytes);
    }
    remaining -= bytes;
  }
}

} // namespace

struct BtSession::Impl {
  struct PendingDelete {
    std::shared_ptr<BtDownload> download;
    DeleteIntent intent = DeleteIntent::Permanent;
  };

  struct MetricIndices {
    int peerSockets = -1;
    int halfOpenPeers = -1;
    int tcpPeers = -1;
    int utpPeers = -1;
    int queuedTrackerAnnounces = -1;
    int connectionAttempts = -1;
    int connectionTimeouts = -1;
    int payloadDownloaded = -1;
    int payloadUploaded = -1;
    int trackerDownloaded = -1;
    int trackerUploaded = -1;
    int ipOverheadDownloaded = -1;
    int ipOverheadUploaded = -1;
    int dhtDownloaded = -1;
    int dhtUploaded = -1;
    int diskBlocksInUse = -1;
    int queuedDiskJobs = -1;
    int diskWriteJobs = -1;
    int diskReadJobs = -1;
    int diskHashJobs = -1;
    int diskJobTime = -1;
    int diskRequestLatency = -1;
    int diskReadWaitingPeers = -1;
    int diskWriteWaitingPeers = -1;
    int dhtNodes = -1;
    int dhtReplacements = -1;
  } metrics;

  const Option* option;
  BtConfig config;
  std::atomic<bool> alertsPending{true};
  std::unique_ptr<lt::session> session;
  std::map<a2_gid_t, std::shared_ptr<BtDownload>> downloads;
  std::map<lt::torrent_handle, std::weak_ptr<BtDownload>> handles;
  std::map<std::string, PendingDelete> pendingDeletes;
  uint64_t payloadDownloaded = 0;
  uint64_t payloadUploaded = 0;
  uint64_t loggingRevision = 0;
  int downloadRateLimit = 0;
  BtPeerBlocklist blocklist;
  uint64_t filterRevision = 0;
  uint16_t listenPort = 0;
  uint16_t announcePort = 0;
  std::string externalAddress;
  std::string externalAddressV4;
  std::string externalAddressV6;
  std::chrono::system_clock::time_point lastPoll =
      std::chrono::system_clock::now();
  Timer lastUpdate = Timer::zero();
  Timer lastPieceUpdate = Timer::zero();
  Timer lastPeerUpdate = Timer::zero();
  Timer lastDhtStats = Timer::zero();
  Timer lastSessionStats = Timer::zero();
  Timer lastSessionStateSave = Timer::zero();
  Timer lastRouteUpdate = Timer::zero();
  std::string sessionStateFile;
  std::string lastSessionState;
  size_t droppedAlerts = 0;
  size_t dhtNodes = 0;
  size_t dhtReplacements = 0;
  size_t dhtActiveRequests = 0;
  std::vector<std::string> listenEndpoints;
  uint16_t mappedTcpPort = 0;
  uint16_t mappedUdpPort = 0;
  std::string portMappingError;
  size_t peerSockets = 0;
  size_t halfOpenPeers = 0;
  size_t tcpPeers = 0;
  size_t utpPeers = 0;
  size_t queuedTrackerAnnounces = 0;
  uint64_t connectionAttempts = 0;
  uint64_t connectionTimeouts = 0;
  uint64_t trackerDownloaded = 0;
  uint64_t trackerUploaded = 0;
  uint64_t ipOverheadDownloaded = 0;
  uint64_t ipOverheadUploaded = 0;
  uint64_t dhtDownloaded = 0;
  uint64_t dhtUploaded = 0;
  uint64_t diskBlocksInUse = 0;
  uint64_t queuedDiskJobs = 0;
  uint64_t averageDiskJobTime = 0;
  uint64_t diskRequestLatency = 0;
  size_t diskReadWaitingPeers = 0;
  size_t diskWriteWaitingPeers = 0;
  std::array<uint64_t, lt::performance_alert::num_warnings>
      performanceWarnings{};
  std::string lastPerformanceWarning;
  explicit Impl(const Option* option)
      : option(option),
        config(makeBtConfig(option)),
        loggingRevision(logging::revision()),
        downloadRateLimit(option->getAsInt(PREF_MAX_OVERALL_DOWNLOAD_LIMIT)),
        sessionStateFile(state::btSessionFile(option))
  {
    metrics.peerSockets = lt::find_metric_idx("peer.num_peers_connected");
    metrics.halfOpenPeers = lt::find_metric_idx("peer.num_peers_half_open");
    metrics.tcpPeers = lt::find_metric_idx("peer.num_tcp_peers");
    metrics.utpPeers = lt::find_metric_idx("peer.num_utp_peers");
    metrics.queuedTrackerAnnounces =
        lt::find_metric_idx("tracker.num_queued_tracker_announces");
    metrics.connectionAttempts =
        lt::find_metric_idx("peer.connection_attempts");
    metrics.connectionTimeouts = lt::find_metric_idx("peer.connect_timeouts");
    metrics.payloadDownloaded = lt::find_metric_idx("net.recv_payload_bytes");
    metrics.payloadUploaded = lt::find_metric_idx("net.sent_payload_bytes");
    metrics.trackerDownloaded = lt::find_metric_idx("net.recv_tracker_bytes");
    metrics.trackerUploaded = lt::find_metric_idx("net.sent_tracker_bytes");
    metrics.ipOverheadDownloaded =
        lt::find_metric_idx("net.recv_ip_overhead_bytes");
    metrics.ipOverheadUploaded =
        lt::find_metric_idx("net.sent_ip_overhead_bytes");
    metrics.dhtDownloaded = lt::find_metric_idx("dht.dht_bytes_in");
    metrics.dhtUploaded = lt::find_metric_idx("dht.dht_bytes_out");
    metrics.diskBlocksInUse = lt::find_metric_idx("disk.disk_blocks_in_use");
    metrics.queuedDiskJobs = lt::find_metric_idx("disk.queued_disk_jobs");
    metrics.diskWriteJobs = lt::find_metric_idx("disk.num_write_ops");
    metrics.diskReadJobs = lt::find_metric_idx("disk.num_read_ops");
    metrics.diskHashJobs = lt::find_metric_idx("disk.num_blocks_hashed");
    metrics.diskJobTime = lt::find_metric_idx("disk.disk_job_time");
    metrics.diskRequestLatency = lt::find_metric_idx("disk.request_latency");
    metrics.diskReadWaitingPeers =
        lt::find_metric_idx("peer.num_peers_up_disk");
    metrics.diskWriteWaitingPeers =
        lt::find_metric_idx("peer.num_peers_down_disk");
    metrics.dhtNodes = lt::find_metric_idx("dht.dht_nodes");
    metrics.dhtReplacements = lt::find_metric_idx("dht.dht_node_cache");
  }
};

namespace {

constexpr size_t MAX_SESSION_STATE_SIZE = 16_m;

bool writeAtomic(const std::string& path, const char* data, size_t size)
{
  if (path.empty()) {
    return false;
  }
  const auto temporary = path + "__temp";
  File directory(File(path).getDirname());
  if (!directory.isDir() && !directory.mkdirs()) {
    return false;
  }
  BufferedFile file(temporary.c_str(), BufferedFile::WRITE);
  return file && file.write(data, size) == size && file.close() != EOF &&
         File(temporary).renameTo(path);
}

std::string readStateFile(const std::string& path)
{
  if (path.empty()) {
    return {};
  }
  File state(path);
  if (!state.isFile()) {
    return {};
  }
  const auto size = state.size();
  if (size <= 0 || static_cast<uint64_t>(size) > MAX_SESSION_STATE_SIZE) {
    A2_LOG_WARN(
        fmt("Ignoring invalid BitTorrent session state file %s", path.c_str()));
    return {};
  }
  BufferedFile file(path.c_str(), BufferedFile::READ);
  if (!file) {
    return {};
  }
  std::stringstream data;
  file.transfer(data);
  return data.str();
}

bool hasDhtNodes(const lt::session_params& params)
{
  return !params.dht_state.nodes.empty() || !params.dht_state.nodes6.empty();
}

lt::session_params makeSessionParams(const Option* option,
                                     const BtConfig& config,
                                     std::string& loadedState)
{
  const auto stateFile = state::btSessionFile(option);
  loadedState = readStateFile(stateFile);
  if (loadedState.empty()) {
    return lt::session_params(config.settings);
  }

  try {
    auto params = lt::read_session_params(
        lt::span<char const>(loadedState.data(), loadedState.size()),
        lt::session::save_dht_state);
    params.settings = config.settings;
    if (!hasDhtNodes(params)) {
      loadedState.clear();
    }
    A2_LOG_TRACE(
        fmt("Loaded BitTorrent session state from %s", stateFile.c_str()));
    return params;
  }
  catch (const std::exception& error) {
    A2_LOG_WARN(fmt("Ignoring BitTorrent session state %s: %s",
                    stateFile.c_str(), error.what()));
    loadedState.clear();
    return lt::session_params(config.settings);
  }
}

void saveSessionState(BtSession::Impl* impl)
{
  if (!impl || !impl->session || impl->sessionStateFile.empty()) {
    return;
  }
  impl->lastSessionStateSave = global::wallclock();
  if (!impl->config.dhtEnabled) {
    return;
  }
  try {
    const auto params =
        impl->session->session_state(lt::session::save_dht_state);
    if (!hasDhtNodes(params)) {
      return;
    }
    const auto buffer =
        lt::write_session_params_buf(params, lt::session::save_dht_state);
    std::string state(buffer.begin(), buffer.end());
    if (state == impl->lastSessionState) {
      return;
    }
    if (!writeAtomic(impl->sessionStateFile, state.data(), state.size())) {
      A2_LOG_ERROR(fmt("Failed to save BitTorrent session state to %s",
                       impl->sessionStateFile.c_str()));
      return;
    }
    impl->lastSessionState = std::move(state);
    A2_LOG_TRACE(fmt("Saved BitTorrent session state to %s",
                     impl->sessionStateFile.c_str()));
  }
  catch (const std::exception& error) {
    A2_LOG_ERROR(
        fmt("Failed to serialize BitTorrent session state: %s", error.what()));
  }
}

BtSnapshot::State translateState(const lt::torrent_status& status)
{
  if (status.flags & lt::torrent_flags::paused) {
    return BtSnapshot::State::Paused;
  }
  switch (status.state) {
  case lt::torrent_status::checking_files:
  case lt::torrent_status::checking_resume_data:
    return BtSnapshot::State::Checking;
  case lt::torrent_status::downloading_metadata:
    return BtSnapshot::State::DownloadingMetadata;
  case lt::torrent_status::downloading:
    return BtSnapshot::State::Downloading;
  case lt::torrent_status::finished:
    return BtSnapshot::State::Finished;
  case lt::torrent_status::seeding:
    return BtSnapshot::State::Seeding;
  }
  return BtSnapshot::State::Downloading;
}

bool progressVerificationInProgress(const lt::torrent_status& status)
{
  return status.state == lt::torrent_status::checking_files ||
         status.state == lt::torrent_status::checking_resume_data;
}

std::string bitfield(const lt::typed_bitfield<lt::piece_index_t>& pieces)
{
  std::string data((pieces.size() + 7) / 8, '\0');
  for (int i = 0; i < pieces.size(); ++i) {
    if (pieces[lt::piece_index_t{i}]) {
      data[static_cast<size_t>(i) / 8] |=
          static_cast<char>(0x80 >> (static_cast<unsigned int>(i) % 8));
    }
  }
  return util::toHex(data);
}

std::string peerFlags(const lt::peer_info& peer)
{
  std::string flags;
  auto append = [&flags](char value) {
    if (!flags.empty()) {
      flags += ' ';
    }
    flags += value;
  };
  if (peer.flags & lt::peer_info::interesting) {
    append(peer.flags & lt::peer_info::remote_choked ? 'd' : 'D');
  }
  if (peer.flags & lt::peer_info::remote_interested) {
    append(peer.flags & lt::peer_info::choked ? 'u' : 'U');
  }
  if (peer.flags & lt::peer_info::optimistic_unchoke) {
    append('O');
  }
  if (peer.flags & lt::peer_info::snubbed) {
    append('S');
  }
  if (!(peer.flags & lt::peer_info::outgoing_connection)) {
    append('I');
  }
  return flags;
}

std::vector<std::string> peerSources(const lt::peer_info& peer)
{
  std::vector<std::string> result;
  if (peer.source & lt::peer_info::tracker) {
    result.emplace_back("tracker");
  }
  if (peer.source & lt::peer_info::dht) {
    result.emplace_back("dht");
  }
  if (peer.source & lt::peer_info::pex) {
    result.emplace_back("pex");
  }
  if (peer.source & lt::peer_info::lsd) {
    result.emplace_back("lsd");
  }
  if (peer.source & lt::peer_info::resume_data) {
    result.emplace_back("resume");
  }
  if (peer.source & lt::peer_info::incoming) {
    result.emplace_back("incoming");
  }
  return result;
}

BtPeerSnapshot makePeer(const lt::peer_info& peer, int64_t totalLength)
{
  BtPeerSnapshot result;
  const auto endpoint = peer.remote_endpoint();
  if (peer.flags & lt::peer_info::connecting) {
    result.state = "connecting";
  }
  else if (peer.flags & lt::peer_info::handshake) {
    result.state = "handshaking";
  }
  else {
    result.state = "connected";
    result.peerId.assign(reinterpret_cast<const char*>(peer.pid.data()),
                         peer.pid.size());
    result.bitfield = bitfield(peer.pieces);
  }
  result.clientName = peer.client;
  result.ip = endpoint.address().to_string();
  result.port = endpoint.port();
  result.flags = peerFlags(peer);
  result.transport = peer.flags & lt::peer_info::utp_socket ? "utp" : "tcp";
  if (peer.flags & lt::peer_info::ssl_socket) {
    result.encryption = "tls";
  }
  else if (peer.flags & lt::peer_info::rc4_encrypted) {
    result.encryption = "rc4";
  }
  else if (peer.flags & lt::peer_info::plaintext_encrypted) {
    result.encryption = "encryptedHandshake";
  }
  else {
    result.encryption = "plain";
  }
  result.sources = peerSources(peer);
  result.downloaded = peer.total_download;
  result.uploaded = peer.total_upload;
  result.completedLength = static_cast<int64_t>(
      static_cast<long double>(totalLength) * peer.progress_ppm / 1000000.0L);
  result.downloadSpeed = peer.payload_down_speed;
  result.uploadSpeed = peer.payload_up_speed;
  result.progressPpm = peer.progress_ppm;
  result.amChoking = bool(peer.flags & lt::peer_info::choked);
  result.amInterested = bool(peer.flags & lt::peer_info::interesting);
  result.peerChoking = bool(peer.flags & lt::peer_info::remote_choked);
  result.peerInterested = bool(peer.flags & lt::peer_info::remote_interested);
  result.incoming = !(peer.flags & lt::peer_info::outgoing_connection);
  result.snubbed = bool(peer.flags & lt::peer_info::snubbed);
  result.optimisticUnchoke =
      bool(peer.flags & lt::peer_info::optimistic_unchoke);
  result.seeder = bool(peer.flags & lt::peer_info::seed);
  return result;
}

std::string endpointName(const lt::tcp::endpoint& endpoint)
{
  auto address = endpoint.address().to_string();
  if (endpoint.address().is_v6()) {
    address = '[' + address + ']';
  }
  return address + ':' + std::to_string(endpoint.port());
}

std::vector<BtTrackerSnapshot> makeTrackers(const lt::torrent_handle& handle,
                                            const BtDownload* download)
{
  std::vector<BtTrackerSnapshot> result;
  const auto hashes = handle.info_hashes();
  const auto now = lt::aux::time_now32();
  auto remaining = [now](lt::time_point32 value) -> int64_t {
    if (value == (lt::time_point32::min)()) {
      return -1;
    }
    return std::max<int64_t>(0, lt::total_seconds(value - now));
  };
  for (const auto& tracker : handle.trackers()) {
    BtTrackerSnapshot snapshot;
    snapshot.url = tracker.url;
    snapshot.source = download->trackerSource(tracker.url);
    snapshot.tier = tracker.tier;
    snapshot.verified = tracker.verified;
    bool hasError = false;
    for (const auto& endpoint : tracker.endpoints) {
      int protocolIndex = 0;
      for (const auto& infoHash : endpoint.info_hashes) {
        const bool v1 = protocolIndex++ == 0;
        if ((v1 && !hashes.has_v1()) || (!v1 && !hashes.has_v2())) {
          continue;
        }
        BtTrackerEndpointSnapshot endpointSnapshot;
        endpointSnapshot.localEndpoint = endpointName(endpoint.local_endpoint);
        endpointSnapshot.protocol = v1 ? "v1" : "v2";
        endpointSnapshot.failures = infoHash.fails;
        endpointSnapshot.updating = infoHash.updating;
        endpointSnapshot.seeders = infoHash.scrape_complete;
        endpointSnapshot.leechers = infoHash.scrape_incomplete;
        endpointSnapshot.downloads = infoHash.scrape_downloaded;
        endpointSnapshot.nextAnnounceSeconds =
            remaining(infoHash.next_announce);
        endpointSnapshot.minAnnounceSeconds = remaining(infoHash.min_announce);
        endpointSnapshot.verified = infoHash.start_sent;
        if (infoHash.last_error) {
          endpointSnapshot.message = infoHash.last_error.message();
        }
        else {
          endpointSnapshot.message = infoHash.message;
        }
        endpointSnapshot.status = endpointSnapshot.updating   ? "updating"
                                  : endpointSnapshot.verified ? "working"
                                  : infoHash.last_error       ? "error"
                                                              : "waiting";
        snapshot.endpoints.push_back(std::move(endpointSnapshot));
        snapshot.failures =
            std::max(snapshot.failures, static_cast<int>(infoHash.fails));
        snapshot.updating = snapshot.updating || infoHash.updating;
        snapshot.seeders = std::max(snapshot.seeders, infoHash.scrape_complete);
        snapshot.leechers =
            std::max(snapshot.leechers, infoHash.scrape_incomplete);
        snapshot.downloads =
            std::max(snapshot.downloads, infoHash.scrape_downloaded);
        const auto nextAnnounce = endpointSnapshot.nextAnnounceSeconds;
        if (nextAnnounce >= 0 &&
            (snapshot.nextAnnounceSeconds < 0 ||
             nextAnnounce < snapshot.nextAnnounceSeconds)) {
          snapshot.nextAnnounceSeconds = nextAnnounce;
        }
        const auto minAnnounce = endpointSnapshot.minAnnounceSeconds;
        if (minAnnounce >= 0 &&
            (snapshot.minAnnounceSeconds < 0 ||
             minAnnounce < snapshot.minAnnounceSeconds)) {
          snapshot.minAnnounceSeconds = minAnnounce;
        }
        if (infoHash.last_error) {
          hasError = true;
          snapshot.message = infoHash.last_error.message();
        }
        else if (!infoHash.message.empty()) {
          snapshot.message = infoHash.message;
        }
      }
    }
    snapshot.status = snapshot.updating   ? "updating"
                      : snapshot.verified ? "working"
                      : hasError          ? "error"
                                          : "waiting";
    result.push_back(std::move(snapshot));
  }
  return result;
}

void saveResume(const std::string& path, const lt::add_torrent_params& params)
{
  const auto data = lt::write_resume_data_buf(params);
  BtResumeStore::write(path, data.data(), data.size());
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

std::vector<lt::download_priority_t> makeFilePriorities(RequestGroup* group)
{
  std::vector<lt::download_priority_t> priorities;
  if (!group) {
    return priorities;
  }
  const auto& files = group->getDownloadContext()->getFileEntries();
  priorities.reserve(files.size());
  for (const auto& file : files) {
    priorities.push_back(file->isRequested() ? lt::default_priority
                                             : lt::dont_download);
  }

  const auto& specification = group->getOption()->get(PREF_BT_FILE_PRIORITY);
  if (specification.empty()) {
    return priorities;
  }
  applyBtFilePrioritySpec(priorities, specification);
  for (size_t i = 0; i < priorities.size(); ++i) {
    files[i]->setRequested(priorities[i] != lt::dont_download);
  }
  return priorities;
}

struct SelectionPlan {
  std::vector<lt::download_priority_t> files;
  std::vector<lt::download_priority_t> pieces;
  bool pieceOverrides = false;
};

SelectionPlan makeSelectionPlan(const lt::torrent_handle& handle,
                                RequestGroup* group,
                                bool restoreNativePiecePriorities)
{
  SelectionPlan plan;
  if (!group) {
    return plan;
  }
  group->getBtDownload()->updateSelection(group->getDownloadContext());
  if (!handle.is_valid()) {
    return plan;
  }
  const auto info = handle.torrent_file();
  if (!info ||
      group->getDownloadContext()->getFileEntries().size() !=
          static_cast<size_t>(info->layout().num_files())) {
    return plan;
  }
  plan.files = makeFilePriorities(group);
  const auto& savePath = group->getOption()->get(PREF_DIR);
  size_t index = 0;
  for (const auto& file : group->getDownloadContext()->getFileEntries()) {
    if (index < static_cast<size_t>(info->layout().num_files())) {
      const auto nativeIndex =
          lt::file_index_t{static_cast<int>(index)};
      if (info->layout().pad_file_at(nativeIndex)) {
        plan.files[index] = lt::dont_download;
        file->setRequested(false);
      }
      auto storagePath = file->getPath();
      if (!savePath.empty() && storagePath.size() > savePath.size() &&
          storagePath.compare(0, savePath.size(), savePath) == 0 &&
          (storagePath[savePath.size()] == '/' ||
           storagePath[savePath.size()] == '\\')) {
        storagePath.erase(0, savePath.size() + 1);
      }
      if (storagePath !=
          info->layout().file_path(nativeIndex)) {
        handle.rename_file(nativeIndex, storagePath);
      }
    }
    ++index;
  }
  group->getBtDownload()->updateSelection(group->getDownloadContext());
  auto& snapshotFiles = group->getBtDownload()->mutableSnapshot().files;
  const auto snapshotCount =
      std::min(snapshotFiles.size(), plan.files.size());
  for (size_t i = 0; i < snapshotCount; ++i) {
    snapshotFiles[i].priority =
        static_cast<int>(static_cast<uint8_t>(plan.files[i]));
  }

  const bool prioritizeBoundaries =
      group->getOption()->getAsBool(PREF_BT_FIRST_LAST_PIECE_FIRST);
  if (!prioritizeBoundaries && !restoreNativePiecePriorities) {
    return plan;
  }
  plan.pieceOverrides = prioritizeBoundaries;

  plan.pieces.assign(
      static_cast<size_t>(info->num_pieces()), lt::dont_download);
  const auto& files = info->layout();
  const auto pieceLength = std::max(1, info->piece_length());
  index = 0;
  for (const auto& file : group->getDownloadContext()->getFileEntries()) {
    if (index >= static_cast<size_t>(files.num_files())) {
      break;
    }
    const auto nativeIndex = lt::file_index_t{static_cast<int>(index)};
    const auto fileSize = files.file_size(nativeIndex);
    if (!file->isRequested() || fileSize <= 0) {
      ++index;
      continue;
    }
    const auto first = info->map_file(nativeIndex, 0, 1).piece;
    const auto last = info->map_file(nativeIndex, fileSize - 1, 1).piece;
    for (int piece = static_cast<int>(first); piece <= static_cast<int>(last);
         ++piece) {
      plan.pieces[static_cast<size_t>(piece)] = plan.files[index];
    }
    const auto boundaryPieces = std::max<int64_t>(
        1, static_cast<int64_t>(std::ceil(static_cast<long double>(fileSize) *
                                          0.01L / pieceLength)));
    for (int64_t offset = 0; offset < boundaryPieces; ++offset) {
      const auto front = static_cast<int64_t>(static_cast<int>(first)) + offset;
      const auto back = static_cast<int64_t>(static_cast<int>(last)) - offset;
      if (prioritizeBoundaries && front <= static_cast<int>(last)) {
        plan.pieces[static_cast<size_t>(front)] = lt::top_priority;
      }
      if (prioritizeBoundaries && back >= static_cast<int>(first)) {
        plan.pieces[static_cast<size_t>(back)] = lt::top_priority;
      }
    }
    ++index;
  }
  return plan;
}

void updateDownloadContext(BtDownload* download, RequestGroup* group)
{
  download->populateDownloadContext(group->getDownloadContext(),
                                    group->getOption().get());
  auto selected =
      util::parseIntSegments(group->getOption()->get(PREF_SELECT_FILE));
  selected.normalize();
  group->getDownloadContext()->setFileFilter(std::move(selected));
  download->updateFilePaths(group->getDownloadContext(),
                            group->getOption().get());
  download->updateSelection(group->getDownloadContext());
}

} // namespace

void BtSession::requestResumeCheckpoint(BtDownload* download, bool force)
{
  if (!download || !download->impl_->handle.is_valid() ||
      !download->impl_->handle.in_session()) {
    return;
  }
  if (download->impl_->resumeSaveOutstanding) {
    download->impl_->checkpointPending = true;
    return;
  }
  if (!force) {
    const auto interval =
        impl_->option->getAsInt(PREF_BT_RESUME_SAVE_INTERVAL);
    if (interval == 0) {
      return;
    }
    if (!download->impl_->lastResumeSave.isZero() &&
        download->impl_->lastResumeSave.difference(global::wallclock()) <
            std::chrono::minutes(interval)) {
      return;
    }
  }
  download->impl_->resumeSaveOutstanding = true;
  download->impl_->handle.save_resume_data(
      lt::torrent_handle::save_info_dict |
      lt::torrent_handle::only_if_modified);
}

void BtSession::finishResumeSave(BtDownload* download)
{
  download->impl_->resumeSaveOutstanding = false;
  download->impl_->lastResumeSave = global::wallclock();
  if (download->impl_->checkpointPending &&
      download->shutdownStage() == BtDownload::ShutdownStage::Idle) {
    download->impl_->checkpointPending = false;
    requestResumeCheckpoint(download, true);
  }
}

bool BtSession::synchronizeSelection(BtDownload* download)
{
  if (!download || !download->group() ||
      !download->impl_->handle.is_valid() ||
      !download->impl_->handle.in_session()) {
    return false;
  }

  auto plan = makeSelectionPlan(
      download->impl_->handle, download->group(),
      !download->impl_->appliedPiecePriorities.empty());
  download->impl_->desiredFilePriorities = std::move(plan.files);
  download->impl_->desiredPiecePriorities = std::move(plan.pieces);

  if (download->impl_->desiredFilePriorities.empty()) {
    return false;
  }

  if (download->impl_->filePriorityUpdatePending) {
    return true;
  }
  if (!download->impl_->desiredFilePriorities.empty() &&
      download->impl_->desiredFilePriorities !=
          download->impl_->appliedFilePriorities) {
    download->impl_->filePriorityUpdatePending = true;
    download->impl_->handle.prioritize_files(
        download->impl_->desiredFilePriorities);
    return true;
  }
  if (download->impl_->desiredPiecePriorities !=
      download->impl_->appliedPiecePriorities) {
    download->impl_->handle.prioritize_pieces(
        download->impl_->desiredPiecePriorities);
    if (plan.pieceOverrides) {
      download->impl_->appliedPiecePriorities =
          download->impl_->desiredPiecePriorities;
    }
    else {
      download->impl_->appliedPiecePriorities.clear();
    }
  }
  return false;
}

void BtSession::finishFilePriorityUpdate(BtDownload* download)
{
  if (!download || !download->impl_->filePriorityUpdatePending) {
    return;
  }
  download->impl_->filePriorityUpdatePending = false;
  download->impl_->appliedFilePriorities =
      download->impl_->handle.get_file_priorities();
  download->impl_->appliedPiecePriorities.clear();
  continueSelectionSynchronization(download);
}

void BtSession::continueSelectionSynchronization(BtDownload* download)
{
  if (synchronizeSelection(download)) {
    return;
  }
  requestResumeCheckpoint(download);
  if (download->impl_->resumeAfterFilePriorityUpdate) {
    download->impl_->resumeAfterFilePriorityUpdate = false;
    const bool selectionApplying = download->fileSelectionApplying();
    download->completeFileSelectionApply();
    if (selectionApplying) {
      requestProgressRefresh(download);
    }
    if (download->group() && !download->group()->isHaltRequested() &&
        download->shutdownStage() == BtDownload::ShutdownStage::Idle) {
      resumeTorrent(download);
    }
  }
}

void BtSession::failFilePriorityUpdate(BtDownload* download)
{
  if (!download || !download->impl_->filePriorityUpdatePending) {
    return;
  }
  download->impl_->filePriorityUpdatePending = false;
  download->impl_->resumeAfterFilePriorityUpdate = false;
  download->impl_->appliedFilePriorities.clear();
  download->impl_->appliedPiecePriorities.clear();
  download->failFileSelectionApply();
}

void BtSession::requestProgressRefresh(BtDownload* download)
{
  if (!download || !download->impl_->handle.is_valid() ||
      !download->impl_->handle.in_session()) {
    return;
  }
  download->beginProgressRefresh();
  download->impl_->handle.post_status(
      lt::torrent_handle::query_pieces | lt::torrent_handle::query_name |
      lt::torrent_handle::query_save_path);
  download->impl_->handle.post_file_progress({});
}

void BtSession::resumeTorrent(BtDownload* download)
{
  if (!download || !download->group() || !download->impl_->handle.is_valid()) {
    return;
  }
  if (download->group()->getOption()->getAsBool(PREF_CHECK_INTEGRITY) &&
      !download->impl_->initialRecheckStarted) {
    download->impl_->initialRecheckStarted = true;
    download->impl_->handle.force_recheck();
  }
  download->impl_->handle.unset_flags(lt::torrent_flags::auto_managed |
                                      lt::torrent_flags::stop_when_ready);
  download->impl_->handle.resume();
}

void BtSession::prepareFreshAdd(BtDownload* download)
{
  if (!download || !download->group() || !download->impl_->params.ti) {
    return;
  }

  auto& params = download->impl_->params;
  params.file_priorities = makeFilePriorities(download->group());
  const auto priorityCount =
      std::min(params.file_priorities.size(), download->snapshot_.files.size());
  for (size_t index = 0; index < priorityCount; ++index) {
    download->snapshot_.files[index].priority = static_cast<int>(
        static_cast<std::uint8_t>(params.file_priorities[index]));
  }
  params.have_pieces.clear();
  params.verified_pieces.clear();
  params.unfinished_pieces.clear();
  params.piece_priorities.clear();
  params.completed_time = 0;
  params.finished_time = 0;
  params.seeding_time = 0;
  params.last_seen_complete = 0;
  params.flags &= ~(lt::torrent_flags::auto_managed |
                    lt::torrent_flags::default_dont_download |
                    lt::torrent_flags::seed_mode |
                    lt::torrent_flags::stop_when_ready);
  params.flags |= lt::torrent_flags::paused;

  download->impl_->filePriorityUpdatePending = false;
  download->impl_->resumeAfterFilePriorityUpdate = false;
  download->impl_->desiredFilePriorities = params.file_priorities;
  download->impl_->appliedFilePriorities = params.file_priorities;
  download->impl_->desiredPiecePriorities.clear();
  download->impl_->appliedPiecePriorities.clear();
  download->impl_->recheckAfterAdd = true;
  download->impl_->resumeAfterRecheck = download->impl_->runRequested;
  download->beginProgressVerification();
  download->snapshot_.complete = false;
  download->snapshot_.selectedComplete = false;
  download->snapshot_.state = BtSnapshot::State::Recovering;
  download->clearError();
  if (!download->impl_->resumePath.empty()) {
    File(download->impl_->resumePath).remove();
  }
}

void BtSession::beginNativeDelete(
    const std::shared_ptr<BtDownload>& download, DeleteIntent intent)
{
  if (!download) {
    return;
  }
  const auto handle = download->impl_->handle;
  if (!handle.is_valid() || !handle.in_session()) {
    download->impl_->nativeState = BtNativeState::Detached;
    if (intent == DeleteIntent::Permanent) {
      forgetHandles(download.get());
      download->finishStopping();
      impl_->downloads.erase(download->impl_->gid);
    }
    return;
  }

  if (intent == DeleteIntent::Replace) {
    prepareFreshAdd(download.get());
  }
  else if (!download->impl_->resumePath.empty()) {
    File(download->impl_->resumePath).remove();
  }

  const auto key = hashKey(handle.info_hashes());
  if (key.empty()) {
    download->setError("BitTorrent native handle has no info hash");
    download->shutdownStage_ = BtDownload::ShutdownStage::Complete;
    download->impl_->nativeState = BtNativeState::Detached;
    return;
  }

  impl_->pendingDeletes[key] = {download, intent};
  download->impl_->nativeState = BtNativeState::Removing;
  if (intent == DeleteIntent::Permanent) {
    download->beginRemoving();
  }
  impl_->session->remove_torrent(handle, lt::session::delete_partfile);
}

void BtSession::forgetHandles(BtDownload* download)
{
  for (auto it = impl_->handles.begin(); it != impl_->handles.end();) {
    const auto owner = it->second.lock();
    if (!owner || owner.get() == download) {
      it = impl_->handles.erase(it);
    }
    else {
      ++it;
    }
  }
}

void BtSession::finishNativeDelete(const std::string& key,
                                   const std::string& error)
{
  const auto found = impl_->pendingDeletes.find(key);
  if (found == impl_->pendingDeletes.end()) {
    return;
  }
  auto pending = std::move(found->second);
  impl_->pendingDeletes.erase(found);
  const auto& download = pending.download;
  forgetHandles(download.get());
  download->impl_->handle = {};
  download->impl_->nativeState = BtNativeState::Detached;

  if (!error.empty()) {
    if (pending.intent == DeleteIntent::Replace) {
      download->setError("Unable to reset BitTorrent partfile: " + error);
      download->shutdownStage_ = BtDownload::ShutdownStage::Complete;
      download->snapshot_.state = BtSnapshot::State::Error;
    }
    else {
      A2_LOG_ERROR(fmt("Failed to delete BitTorrent partfile: %s",
                       error.c_str()));
      download->finishStopping();
    }
    impl_->downloads.erase(download->impl_->gid);
    return;
  }

  if (pending.intent == DeleteIntent::Replace) {
    download->impl_->nativeState = BtNativeState::Adding;
    impl_->session->async_add_torrent(download->impl_->params);
    return;
  }

  download->finishStopping();
  impl_->downloads.erase(download->impl_->gid);
}

bool BtSession::recoverPartfile(
    const std::shared_ptr<BtDownload>& download,
    const BtErrorSnapshot& error)
{
  if (!download || download->impl_->partfileRecoveryAttempted ||
      error.category != "libtorrent" ||
      error.code != static_cast<int>(lt::errors::file_too_short) ||
      (error.operation != "partfile_read" &&
       error.operation != "partfile_write")) {
    return false;
  }

  download->impl_->partfileRecoveryAttempted = true;
  download->impl_->filePriorityUpdatePending = false;
  download->impl_->resumeAfterFilePriorityUpdate = false;
  download->impl_->runRequested =
      download->group() && !download->group()->isPauseRequested();
  A2_LOG_WARN("Recovering a structurally invalid BitTorrent partfile");
  beginNativeDelete(download, DeleteIntent::Replace);
  return true;
}

void BtSession::refreshAutomaticRoute(bool reopenSockets)
{
  auto announce = [this]() {
    for (const auto& entry : impl_->downloads) {
      const auto& handle = entry.second->impl_->handle;
      if (!handle.is_valid() || !handle.in_session()) {
        continue;
      }
      handle.force_reannounce();
      handle.force_dht_announce();
      handle.force_lsd_announce();
    }
  };
  if (!impl_->config.automaticRoute) {
    if (reopenSockets) {
      impl_->session->reopen_network_sockets();
      announce();
    }
    return;
  }

  const auto addresses = detectBtRouteAddresses(impl_->option);
  if (!addresses.empty() && addresses != impl_->config.routeAddresses) {
    auto config = makeBtConfig(impl_->option, addresses);
    impl_->session->apply_settings(config.settings);
    impl_->config = std::move(config);
    impl_->listenEndpoints.clear();
    impl_->listenPort = 0;
    A2_LOG_INFO(fmt("BitTorrent route addresses changed: %s",
                    impl_->config.listenInterfaces.c_str()));
    announce();
  }
  else if (reopenSockets) {
    impl_->session->reopen_network_sockets();
    announce();
  }
}

BtSession::BtSession(const Option* option) : impl_(make_unique<Impl>(option))
{
  auto params =
      makeSessionParams(option, impl_->config, impl_->lastSessionState);
  configureBtDiskIo(params, option);
  impl_->session = make_unique<lt::session>(std::move(params));
  impl_->session->set_alert_notify(
      [impl = impl_.get()]() { impl->alertsPending.store(true); });
  impl_->lastSessionStateSave = global::wallclock();
}

BtSession::~BtSession()
{
  impl_->session->set_alert_notify({});
  saveSessionState(impl_.get());
}

void BtSession::attach(const std::shared_ptr<BtDownload>& download,
                       RequestGroup* group, AttachMode mode)
{
  if (!download || !group) {
    return;
  }

  const bool running = mode == AttachMode::Running;
  download->impl_->runRequested = running;
  download->initialize(group);
  download->configure(group->getOption().get());

  if (download->impl_->params.ti &&
      (download->snapshot().files.empty() ||
       group->getDownloadContext()->getFileEntries().size() !=
           static_cast<size_t>(
               download->impl_->params.ti->layout().num_files()))) {
    updateDownloadContext(download.get(), group);
  }
  if (download->impl_->params.ti) {
    download->impl_->params.file_priorities = makeFilePriorities(group);
  }

  impl_->downloads[group->getGID()] = download;

  if (download->impl_->nativeState == BtNativeState::Adding) {
    return;
  }
  if (download->impl_->nativeState == BtNativeState::Attached &&
      download->impl_->handle.is_valid() &&
      download->impl_->handle.in_session()) {
    impl_->handles[download->impl_->handle] = download;
    if (!running) {
      download->snapshot_.state = BtSnapshot::State::Paused;
      return;
    }

    if (download->fileSelectionApplying()) {
      download->configure(group->getOption().get());
      download->updateSelection(group->getDownloadContext());
      beginNativeDelete(download, DeleteIntent::Replace);
      return;
    }

    download->impl_->handle.clear_error();
    const bool filePriorityUpdatePending =
        applyDownloadOptions(download, group->getOption().get());
    if (filePriorityUpdatePending) {
      download->impl_->resumeAfterFilePriorityUpdate = true;
    }
    else {
      const bool selectionApplying = download->fileSelectionApplying();
      download->completeFileSelectionApply();
      if (selectionApplying) {
        requestProgressRefresh(download.get());
      }
      resumeTorrent(download.get());
    }
    return;
  }
  if (download->impl_->nativeState == BtNativeState::Removing) {
    return;
  }

  if (running) {
    download->impl_->params.flags &=
        ~(lt::torrent_flags::paused | lt::torrent_flags::auto_managed |
          lt::torrent_flags::stop_when_ready);
  }
  else {
    download->impl_->params.flags &=
        ~(lt::torrent_flags::paused | lt::torrent_flags::auto_managed);
    download->impl_->params.flags |= lt::torrent_flags::stop_when_ready;
    download->snapshot_.state = BtSnapshot::State::Paused;
    download->beginProgressVerification();
  }
  download->impl_->desiredFilePriorities =
      download->impl_->params.file_priorities;
  download->impl_->appliedFilePriorities =
      download->impl_->params.file_priorities;
  download->impl_->appliedPiecePriorities.clear();
  download->impl_->nativeState = BtNativeState::Adding;
  impl_->session->async_add_torrent(download->impl_->params);
}

std::unique_ptr<Command>
BtSession::start(const std::shared_ptr<BtDownload>& download,
                 RequestGroup* group, DownloadEngine* engine)
{
  download->prepareStart();
  attach(download, group, AttachMode::Running);
  return make_unique<BtDownloadCommand>(engine->newCUID(), download, this,
                                        group, engine);
}

void BtSession::restorePaused(const std::shared_ptr<BtDownload>& download,
                              RequestGroup* group)
{
  if (!download || !group) {
    return;
  }
  if (download->impl_->nativeState == BtNativeState::Adding ||
      download->impl_->nativeState == BtNativeState::Attached) {
    download->impl_->runRequested = false;
    download->snapshot_.state = BtSnapshot::State::Paused;
    return;
  }
  attach(download, group, AttachMode::RestorePaused);
}

void BtSession::poll()
{
  const auto logRevision = logging::revision();
  if (impl_->loggingRevision != logRevision) {
    impl_->loggingRevision = logRevision;
    lt::settings_pack settings;
    settings.set_int(lt::settings_pack::alert_mask, btAlertMask());
    impl_->session->apply_settings(std::move(settings));
    A2_LOG_DEBUG(fmt("component=bittorrent event=diagnostics_reconfigured "
                     "alert_mask=%d",
                     btAlertMask()));
  }
  const auto now = std::chrono::system_clock::now();
  const bool resumedAfterSleep =
      now - impl_->lastPoll > std::chrono::seconds(100);
  if (resumedAfterSleep || impl_->lastRouteUpdate.isZero() ||
      impl_->lastRouteUpdate.difference(global::wallclock()) >= 5_s) {
    impl_->lastRouteUpdate = global::wallclock();
    refreshAutomaticRoute(resumedAfterSleep);
  }
  impl_->lastPoll = now;

  std::vector<lt::alert*> alerts;
  if (impl_->alertsPending.exchange(false)) {
    impl_->session->pop_alerts(&alerts);
  }
  for (auto* alert : alerts) {
    if (auto* added = lt::alert_cast<lt::add_torrent_alert>(alert)) {
      std::shared_ptr<BtDownload> download;
      const auto addedKey = hashKey(added->params.info_hashes);
      for (const auto& entry : impl_->downloads) {
        if (hashKey(entry.second->impl_->params.info_hashes) == addedKey) {
          download = entry.second;
          break;
        }
      }
      if (!download) {
        if (!added->error && added->handle.is_valid()) {
          impl_->session->remove_torrent(added->handle,
                                        lt::session::delete_partfile);
        }
        continue;
      }
      if (added->error) {
        A2_LOG_ERROR(
            fmt("component=bittorrent event=task_add_failed gid=%s "
                "category=%s code=%d message=%s",
                GroupId::toHex(download->group()->getGID()).c_str(),
                added->error.category().name(), added->error.value(),
                logging::sanitizeText(added->error.message()).c_str()));
        download->impl_->nativeState = BtNativeState::Detached;
        download->setError(added->error.message());
        forgetHandles(download.get());
        impl_->downloads.erase(download->impl_->gid);
        download->shutdownStage_ = BtDownload::ShutdownStage::Complete;
        continue;
      }
      const bool removalPending =
          download->impl_->nativeState == BtNativeState::Removing;
      download->impl_->handle = added->handle;
      download->impl_->nativeState = BtNativeState::Attached;
      download->impl_->appliedTrackerRevision =
          download->impl_->trackerRevision;
      impl_->handles[added->handle] = download;
      A2_LOG_INFO(fmt("component=bittorrent event=task_attached gid=%s "
                      "metadata=%s",
                      GroupId::toHex(download->group()->getGID()).c_str(),
                      download->hasMetadata() ? "ready" : "pending"));
      if (removalPending) {
        const auto managed = impl_->downloads.find(download->impl_->gid);
        if (managed != impl_->downloads.end()) {
          beginNativeDelete(managed->second, DeleteIntent::Permanent);
        }
        continue;
      }
      if (download->stopRequested()) {
        const auto managed = impl_->downloads.find(download->impl_->gid);
        if (managed != impl_->downloads.end()) {
          requestStop(managed->second, download->stopReason());
        }
      }
      else if (download->impl_->recheckAfterAdd) {
        download->impl_->initialRecheckStarted = true;
        download->beginProgressVerification();
        download->snapshot_.state = BtSnapshot::State::Recovering;
        download->impl_->handle.force_recheck();
      }
      else {
        bool filePriorityUpdatePending = false;
        if (!download->awaitingFileSelection() &&
            !download->fileSelectionReady()) {
          filePriorityUpdatePending = synchronizeSelection(download.get());
        }
        if (download->impl_->runRequested) {
          if (filePriorityUpdatePending) {
            download->impl_->resumeAfterFilePriorityUpdate = true;
          }
          else {
            const bool selectionApplying = download->fileSelectionApplying();
            download->completeFileSelectionApply();
            if (selectionApplying) {
              requestProgressRefresh(download.get());
            }
            resumeTorrent(download.get());
          }
        }
      }
      continue;
    }

    auto findDownload = [this](const lt::torrent_handle& handle) {
      const auto found = impl_->handles.find(handle);
      return found == impl_->handles.end()
                 ? std::shared_ptr<BtDownload>{}
                 : found->second.lock();
    };
    auto gidFor = [](const std::shared_ptr<BtDownload>& download) {
      return download && download->group()
                 ? GroupId::toHex(download->group()->getGID())
                 : std::string("unknown");
    };

    if (auto* native = lt::alert_cast<lt::log_alert>(alert)) {
      A2_LOG_TRACE(fmt("component=bittorrent event=native_session message=%s",
                       logging::sanitizeText(native->log_message()).c_str()));
      continue;
    }
    if (auto* native = lt::alert_cast<lt::torrent_log_alert>(alert)) {
      const auto download = findDownload(native->handle);
      A2_LOG_TRACE(fmt("component=bittorrent event=native_torrent gid=%s "
                       "message=%s",
                       gidFor(download).c_str(),
                       logging::sanitizeText(native->log_message()).c_str()));
      continue;
    }
    if (auto* peer = lt::alert_cast<lt::peer_error_alert>(alert)) {
      const auto download = findDownload(peer->handle);
      A2_LOG_DEBUG(fmt("component=bittorrent event=peer_error gid=%s "
                       "operation=%s category=%s code=%d message=%s",
                       gidFor(download).c_str(), lt::operation_name(peer->op),
                       peer->error.category().name(), peer->error.value(),
                       logging::sanitizeText(peer->error.message()).c_str()));
      continue;
    }
    if (auto* peer = lt::alert_cast<lt::peer_connect_alert>(alert)) {
      const auto download = findDownload(peer->handle);
      A2_LOG_TRACE(fmt(
          "component=bittorrent event=peer_connected gid=%s "
          "direction=%s message=%s",
          gidFor(download).c_str(),
          peer->direction == lt::peer_connect_alert::direction_t::in ? "in"
                                                                     : "out",
          logging::sanitizeText(peer->message()).c_str()));
      continue;
    }
    if (auto* peer = lt::alert_cast<lt::peer_disconnected_alert>(alert)) {
      if (peer->reason == lt::close_reason_t::torrent_removed) {
        continue;
      }
      const auto download = findDownload(peer->handle);
      A2_LOG_TRACE(fmt("component=bittorrent event=peer_disconnected gid=%s "
                       "operation=%s category=%s code=%d message=%s",
                       gidFor(download).c_str(), lt::operation_name(peer->op),
                       peer->error.category().name(), peer->error.value(),
                       logging::sanitizeText(peer->error.message()).c_str()));
      continue;
    }
    if (auto* tracker = lt::alert_cast<lt::tracker_announce_alert>(alert)) {
      const auto download = findDownload(tracker->handle);
      A2_LOG_DEBUG(
          fmt("component=bittorrent event=tracker_announce gid=%s "
              "family=%s event_code=%d url=%s",
              gidFor(download).c_str(),
              tracker->local_endpoint.address().is_v6() ? "ipv6" : "ipv4",
              static_cast<int>(tracker->event),
              logging::sanitizeUri(tracker->tracker_url()).c_str()));
      continue;
    }
    if (auto* tracker = lt::alert_cast<lt::tracker_reply_alert>(alert)) {
      const auto download = findDownload(tracker->handle);
      A2_LOG_DEBUG(
          fmt("component=bittorrent event=tracker_reply gid=%s "
              "family=%s peers=%d url=%s",
              gidFor(download).c_str(),
              tracker->local_endpoint.address().is_v6() ? "ipv6" : "ipv4",
              tracker->num_peers,
              logging::sanitizeUri(tracker->tracker_url()).c_str()));
      continue;
    }
    if (lt::alert_cast<lt::dht_bootstrap_alert>(alert)) {
      A2_LOG_INFO("component=bittorrent event=dht_bootstrap_complete");
      continue;
    }

    if (auto* checked = lt::alert_cast<lt::torrent_checked_alert>(alert)) {
      auto download = findDownload(checked->handle);
      if (download) {
        requestProgressRefresh(download.get());
        if (download->impl_->recheckAfterAdd) {
          download->impl_->recheckAfterAdd = false;
          const bool resume = download->impl_->resumeAfterRecheck;
          download->impl_->resumeAfterRecheck = false;
          download->clearError();
          download->completeFileSelectionApply();
          if (resume && download->group() &&
              !download->group()->isPauseRequested() &&
              !download->group()->isHaltRequested()) {
            resumeTorrent(download.get());
          }
          else {
            download->snapshot_.state = BtSnapshot::State::Paused;
          }
        }
        if (download->group() && download->group()->isPauseRequested()) {
          download->stopReason_ = BtDownload::StopReason::Pause;
          download->shutdownStage_ = BtDownload::ShutdownStage::Complete;
          download->snapshot_.state = BtSnapshot::State::Paused;
        }
      }
      continue;
    }

    if (auto* rejected =
            lt::alert_cast<lt::fastresume_rejected_alert>(alert)) {
      auto download = findDownload(rejected->handle);
      if (download) {
        download->beginProgressVerification();
      }
      A2_LOG_WARN(rejected->message());
      continue;
    }

    if (auto* failed = lt::alert_cast<lt::hash_failed_alert>(alert)) {
      auto download = findDownload(failed->handle);
      if (download) {
        download->beginProgressVerification();
      }
      continue;
    }

    if (auto* changed = lt::alert_cast<lt::state_changed_alert>(alert)) {
      auto download = findDownload(changed->handle);
      if (download &&
          (changed->state == lt::torrent_status::checking_files ||
           changed->state == lt::torrent_status::checking_resume_data)) {
        download->beginProgressVerification();
      }
      continue;
    }

    if (auto* stats = lt::alert_cast<lt::session_stats_alert>(alert)) {
      const auto counters = stats->counters();
      auto value = [counters](int index) -> uint64_t {
        return index >= 0 && static_cast<size_t>(index) < counters.size()
                   ? static_cast<uint64_t>(
                         std::max<int64_t>(0, counters[index]))
                   : 0;
      };
      impl_->peerSockets = value(impl_->metrics.peerSockets);
      impl_->halfOpenPeers = value(impl_->metrics.halfOpenPeers);
      impl_->tcpPeers = value(impl_->metrics.tcpPeers);
      impl_->utpPeers = value(impl_->metrics.utpPeers);
      impl_->queuedTrackerAnnounces =
          value(impl_->metrics.queuedTrackerAnnounces);
      impl_->connectionAttempts = value(impl_->metrics.connectionAttempts);
      impl_->connectionTimeouts = value(impl_->metrics.connectionTimeouts);
      impl_->payloadDownloaded = value(impl_->metrics.payloadDownloaded);
      impl_->payloadUploaded = value(impl_->metrics.payloadUploaded);
      impl_->trackerDownloaded = value(impl_->metrics.trackerDownloaded);
      impl_->trackerUploaded = value(impl_->metrics.trackerUploaded);
      impl_->ipOverheadDownloaded =
          value(impl_->metrics.ipOverheadDownloaded);
      impl_->ipOverheadUploaded = value(impl_->metrics.ipOverheadUploaded);
      impl_->dhtDownloaded = value(impl_->metrics.dhtDownloaded);
      impl_->dhtUploaded = value(impl_->metrics.dhtUploaded);
      impl_->diskBlocksInUse = value(impl_->metrics.diskBlocksInUse);
      impl_->queuedDiskJobs = value(impl_->metrics.queuedDiskJobs);
      const auto diskJobs = value(impl_->metrics.diskWriteJobs) +
                            value(impl_->metrics.diskReadJobs) +
                            value(impl_->metrics.diskHashJobs);
      impl_->averageDiskJobTime =
          diskJobs == 0 ? 0 : value(impl_->metrics.diskJobTime) / diskJobs;
      impl_->diskRequestLatency = value(impl_->metrics.diskRequestLatency);
      impl_->diskReadWaitingPeers = value(impl_->metrics.diskReadWaitingPeers);
      impl_->diskWriteWaitingPeers =
          value(impl_->metrics.diskWriteWaitingPeers);
      impl_->dhtNodes = value(impl_->metrics.dhtNodes);
      impl_->dhtReplacements = value(impl_->metrics.dhtReplacements);
      continue;
    }

    if (auto* update = lt::alert_cast<lt::state_update_alert>(alert)) {
      for (const auto& status : update->status) {
        auto download = findDownload(status.handle);
        if (!download) {
          continue;
        }
        auto& snapshot = download->snapshot_;
        const auto transportState = translateState(status);
        const bool verificationInProgress =
            progressVerificationInProgress(status);
        if (status.errc && !download->failed()) {
          BtErrorSnapshot error;
          error.present = true;
          error.code = status.errc.value();
          error.kind = "status";
          error.category = status.errc.category().name();
          error.message = status.errc.message();
          download->setError(std::move(error));
        }
        download->applyTransportState(transportState);
        snapshot.name = status.name.empty() ? snapshot.name : status.name;
        snapshot.currentTracker = status.current_tracker;
        snapshot.allTimeDownload = status.all_time_download;
        snapshot.allTimeUpload = status.all_time_upload;
        snapshot.failedBytes = status.total_failed_bytes;
        snapshot.redundantBytes = status.total_redundant_bytes;
        if (download->group()) {
          auto context = download->group()->getDownloadContext().get();
          updatePayloadCounter(context, status.total_payload_download,
                               download->impl_->payloadDownloaded, false);
          updatePayloadCounter(context, status.total_payload_upload,
                               download->impl_->payloadUploaded, true);
        }
        snapshot.numComplete = status.num_complete;
        snapshot.numIncomplete = status.num_incomplete;
        if (!status.pieces.empty()) {
          snapshot.bitfield = bitfield(status.pieces);
        }
        snapshot.queuePosition = static_cast<int>(status.queue_position);
        snapshot.seedingTime =
            static_cast<int>(status.seeding_duration.count());
        snapshot.activeTime = static_cast<int>(status.active_duration.count());
        snapshot.finishedTime =
            static_cast<int>(status.finished_duration.count());
        snapshot.connectCandidates = status.connect_candidates;
        snapshot.numUploads = status.num_uploads;
        snapshot.availabilityPpm =
            status.distributed_full_copies < 0 ||
                    status.distributed_fraction < 0
                ? -1
                : status.distributed_full_copies * 1000000 +
                      status.distributed_fraction * 1000;
        snapshot.hasMetadata = status.has_metadata;
        if (!verificationInProgress &&
            snapshot.fileSelectionState ==
                BtSnapshot::FileSelectionState::None &&
            !snapshot.error.present) {
          download->applyNativeCompletion(status.is_finished,
                                          status.is_seeding);
        }
      }
      continue;
    }

    if (auto* peers = lt::alert_cast<lt::peer_info_alert>(alert)) {
      auto download = findDownload(peers->handle);
      if (download) {
        download->snapshot_.peers.clear();
        download->snapshot_.peers.reserve(peers->peer_info.size());
        download->snapshot_.numPeers = 0;
        download->snapshot_.connectingPeers = 0;
        download->snapshot_.handshakingPeers = 0;
        download->snapshot_.numSeeds = 0;
        for (const auto& peer : peers->peer_info) {
          auto snapshot = makePeer(peer, download->snapshot_.totalLength);
          if (snapshot.state == "connected") {
            ++download->snapshot_.numPeers;
            if (snapshot.seeder) {
              ++download->snapshot_.numSeeds;
            }
          }
          else if (snapshot.state == "connecting") {
            ++download->snapshot_.connectingPeers;
          }
          else {
            ++download->snapshot_.handshakingPeers;
          }
          download->snapshot_.peers.push_back(std::move(snapshot));
        }
      }
      continue;
    }

    if (auto* progress = lt::alert_cast<lt::file_progress_alert>(alert)) {
      auto download = findDownload(progress->handle);
      if (download) {
        std::vector<int64_t> completedLengths;
        completedLengths.reserve(progress->files.size());
        for (const auto completedLength : progress->files) {
          completedLengths.push_back(completedLength);
        }
        download->applyFileProgress(completedLengths);
      }
      continue;
    }

    if (auto* priorities = lt::alert_cast<lt::file_prio_alert>(alert)) {
      auto download = findDownload(priorities->handle);
      if (download) {
        finishFilePriorityUpdate(download.get());
      }
      continue;
    }

    if (auto* priorities =
            lt::alert_cast<lt::file_priorities_alert>(alert)) {
      auto download = findDownload(priorities->handle);
      if (download && download->impl_->filePriorityUpdatePending) {
        download->impl_->filePriorityUpdatePending = false;
        download->impl_->appliedFilePriorities = priorities->priorities;
        download->impl_->appliedPiecePriorities.clear();
        continueSelectionSynchronization(download.get());
      }
      continue;
    }

    if (auto* metadata = lt::alert_cast<lt::metadata_received_alert>(alert)) {
      auto download = findDownload(metadata->handle);
      if (download && download->group()) {
        const bool pauseForSelection = download->shouldPauseAfterMetadata();
        auto info = metadata->handle.torrent_file();
        if (info) {
          download->impl_->params.ti =
              std::make_shared<lt::torrent_info>(*info);
          download->impl_->params.info_hashes = info->info_hashes();
          for (const auto& tracker : metadata->handle.trackers()) {
            if (download->impl_->trackerOverride) {
              break;
            }
            if (download->trackerSource(tracker.url) == "global") {
              continue;
            }
            const auto found = std::find_if(
                download->impl_->sourceTrackers.begin(),
                download->impl_->sourceTrackers.end(),
                [&tracker](const BtTrackerSpec& entry) {
                  return entry.url == tracker.url;
                });
            if (found == download->impl_->sourceTrackers.end()) {
              download->impl_->sourceTrackers.push_back(
                  {tracker.url, tracker.tier, BtTrackerOrigin::Metainfo});
            }
          }
          updateDownloadContext(download.get(), download->group());
          auto managed = impl_->downloads.at(download->group()->getGID());
          const bool removalPending = download->group()->isHaltRequested() &&
                                      !download->group()->isPauseRequested();
          const bool awaitSelection =
              pauseForSelection && !removalPending &&
              download->stopReason() != BtDownload::StopReason::Stop;
          if (awaitSelection) {
            download->beginFileSelectionPause();
          }
          A2_LOG_INFO(
              fmt("component=bittorrent event=metadata_received gid=%s "
                  "files=%lu selection=%s",
                  gidFor(download).c_str(),
                  static_cast<unsigned long>(info->layout().num_files()),
                  awaitSelection ? "awaiting" : "ready"));
          try {
            applyDownloadOptions(managed,
                                 download->group()->getOption().get());
          }
          catch (RecoverableException& error) {
            download->setError(error.what());
            requestStop(managed, BtDownload::StopReason::Stop);
            continue;
          }
          if (awaitSelection) {
            download->group()->setHaltRequested(true, RequestGroup::NONE);
            download->group()->setPauseRequested(true);
            requestStop(managed, BtDownload::StopReason::FileSelection);
          }
          else if (!download->stopRequested()) {
            requestResumeCheckpoint(download.get(), true);
          }
        }
      }
      continue;
    }

    if (auto* finished = lt::alert_cast<lt::torrent_finished_alert>(alert)) {
      auto download = findDownload(finished->handle);
      if (download) {
        A2_LOG_INFO(fmt("component=bittorrent event=payload_finished gid=%s",
                        gidFor(download).c_str()));
        download->impl_->handle.post_file_progress({});
        requestResumeCheckpoint(download.get(), true);
      }
      continue;
    }

    if (auto* saved = lt::alert_cast<lt::save_resume_data_alert>(alert)) {
      auto download = findDownload(saved->handle);
      if (download) {
        try {
          saveResume(download->impl_->resumePath, saved->params);
        }
        catch (RecoverableException& error) {
          A2_LOG_ERROR_EX("Failed to save BitTorrent resume data", error);
        }
        const bool saveStopState = download->impl_->stopSavePending;
        finishResumeSave(download.get());
        if (saveStopState) {
          download->impl_->stopSavePending = false;
          download->impl_->resumeSaveOutstanding = true;
          auto flags = lt::torrent_handle::save_info_dict;
          if (download->stopReason() == BtDownload::StopReason::Stop) {
            flags |= lt::torrent_handle::flush_disk_cache;
          }
          download->impl_->handle.save_resume_data(flags);
          continue;
        }
        if (download->shutdownStage() ==
            BtDownload::ShutdownStage::SavingResume) {
          if (download->stopReason() == BtDownload::StopReason::Pause ||
              download->stopReason() == BtDownload::StopReason::FileSelection) {
            download->finishStopping();
          }
          else {
            const auto managed = impl_->downloads.find(download->impl_->gid);
            if (managed != impl_->downloads.end()) {
              beginNativeDelete(managed->second, DeleteIntent::Permanent);
            }
          }
        }
      }
      continue;
    }

    if (auto* failed =
            lt::alert_cast<lt::save_resume_data_failed_alert>(alert)) {
      auto download = findDownload(failed->handle);
      if (download) {
        if (failed->error != lt::errors::resume_data_not_modified) {
          A2_LOG_ERROR(fmt("Failed to save BitTorrent resume data %s: %s",
                           download->impl_->resumePath.c_str(),
                           failed->error.message().c_str()));
        }
        const bool saveStopState = download->impl_->stopSavePending;
        finishResumeSave(download.get());
        if (saveStopState) {
          download->impl_->stopSavePending = false;
          download->impl_->resumeSaveOutstanding = true;
          auto flags = lt::torrent_handle::save_info_dict;
          if (download->stopReason() == BtDownload::StopReason::Stop) {
            flags |= lt::torrent_handle::flush_disk_cache;
          }
          download->impl_->handle.save_resume_data(flags);
          continue;
        }
        if (download->shutdownStage() ==
            BtDownload::ShutdownStage::SavingResume) {
          if (download->stopReason() == BtDownload::StopReason::Pause ||
              download->stopReason() == BtDownload::StopReason::FileSelection) {
            download->finishStopping();
          }
          else {
            const auto managed = impl_->downloads.find(download->impl_->gid);
            if (managed != impl_->downloads.end()) {
              beginNativeDelete(managed->second, DeleteIntent::Permanent);
            }
          }
        }
      }
      continue;
    }

    if (auto* removed = lt::alert_cast<lt::torrent_removed_alert>(alert)) {
      auto download = findDownload(removed->handle);
      if (download) {
        if (download->impl_->handle == removed->handle) {
          download->impl_->handle = {};
        }
        impl_->handles.erase(removed->handle);
      }
      continue;
    }

    if (auto* deleted = lt::alert_cast<lt::torrent_deleted_alert>(alert)) {
      finishNativeDelete(hashKey(deleted->info_hashes));
      continue;
    }

    if (auto* failed =
            lt::alert_cast<lt::torrent_delete_failed_alert>(alert)) {
      finishNativeDelete(hashKey(failed->info_hashes),
                         failed->error ? failed->error.message()
                                       : std::string());
      continue;
    }

    if (auto* error = lt::alert_cast<lt::file_error_alert>(alert)) {
      auto download = findDownload(error->handle);
      if (download) {
        BtErrorSnapshot snapshot;
        snapshot.present = true;
        snapshot.recoverable = true;
        snapshot.code = error->error.value();
        snapshot.kind = "storage";
        snapshot.category = error->error.category().name();
        snapshot.message = error->error.message();
        snapshot.operation = lt::operation_name(error->op);
        snapshot.file = error->filename();
        const auto managed = impl_->downloads.find(download->impl_->gid);
        if (managed != impl_->downloads.end() &&
            recoverPartfile(managed->second, snapshot)) {
          A2_LOG_WARN(error->message());
          continue;
        }
        failFilePriorityUpdate(download.get());
        snapshot.recoverable = false;
        download->setError(std::move(snapshot));
      }
      A2_LOG_ERROR(error->message());
      continue;
    }

    if (auto* error = lt::alert_cast<lt::torrent_error_alert>(alert)) {
      auto download = findDownload(error->handle);
      if (download && download->snapshot().error.kind != "storage") {
        BtErrorSnapshot snapshot;
        snapshot.present = true;
        snapshot.recoverable = false;
        snapshot.code = error->error.value();
        snapshot.kind = "torrent";
        snapshot.category = error->error.category().name();
        snapshot.message = error->error.message();
        download->setError(std::move(snapshot));
      }
      A2_LOG_ERROR(fmt("component=bittorrent event=task_failed gid=%s "
                       "category=%s code=%d file=%s message=%s",
                       gidFor(download).c_str(), error->error.category().name(),
                       error->error.value(),
                       logging::sanitizeText(error->filename()).c_str(),
                       logging::sanitizeText(error->error.message()).c_str()));
      continue;
    }

    if (auto* moved = lt::alert_cast<lt::storage_moved_alert>(alert)) {
      auto download = findDownload(moved->handle);
      if (download && download->group()) {
        download->impl_->params.save_path = moved->storage_path();
        download->impl_->previousSavePath.clear();
        requestResumeCheckpoint(download.get(), true);
      }
      continue;
    }

    if (auto* moved = lt::alert_cast<lt::storage_moved_failed_alert>(alert)) {
      auto download = findDownload(moved->handle);
      if (download && download->group()) {
        auto& option = download->group()->getOption();
        option->put(PREF_DIR, download->impl_->previousSavePath);
        download->impl_->params.save_path = download->impl_->previousSavePath;
        download->updateFilePaths(download->group()->getDownloadContext(),
                                  option.get());
        download->updateSelection(download->group()->getDownloadContext());
        download->impl_->previousSavePath.clear();
      }
      A2_LOG_ERROR(moved->message());
      continue;
    }

    if (auto* renamed = lt::alert_cast<lt::file_rename_failed_alert>(alert)) {
      A2_LOG_ERROR(renamed->message());
      continue;
    }

    if (auto* tracker = lt::alert_cast<lt::tracker_error_alert>(alert)) {
      const auto download = findDownload(tracker->handle);
      if (tracker->times_in_row == 1 || tracker->times_in_row % 10 == 0) {
        A2_LOG_DEBUG(
            fmt("component=bittorrent event=tracker_error gid=%s family=%s "
                "url=%s operation=%s error=%s consecutive=%d",
                gidFor(download).c_str(),
                tracker->local_endpoint.address().is_v6() ? "ipv6" : "ipv4",
                logging::sanitizeUri(tracker->tracker_url()).c_str(),
                lt::operation_name(tracker->op),
                logging::sanitizeText(tracker->error.message()).c_str(),
                tracker->times_in_row));
      }
      continue;
    }

    if (auto* tracker = lt::alert_cast<lt::tracker_warning_alert>(alert)) {
      const auto download = findDownload(tracker->handle);
      A2_LOG_DEBUG(
          fmt("component=bittorrent event=tracker_warning gid=%s "
              "url=%s message=%s",
              gidFor(download).c_str(),
              logging::sanitizeUri(tracker->tracker_url()).c_str(),
              logging::sanitizeText(tracker->warning_message()).c_str()));
      continue;
    }

    if (auto* listening = lt::alert_cast<lt::listen_succeeded_alert>(alert)) {
      if (listening->socket_type == lt::socket_type_t::tcp ||
          listening->socket_type == lt::socket_type_t::utp) {
        impl_->listenPort = static_cast<uint16_t>(listening->port);
      }
      auto address = listening->address.to_string();
      if (listening->address.is_v6()) {
        address = '[' + address + ']';
      }
      auto endpoint = address + ':' + std::to_string(listening->port);
      if (std::find(impl_->listenEndpoints.begin(),
                    impl_->listenEndpoints.end(),
                    endpoint) == impl_->listenEndpoints.end()) {
        impl_->listenEndpoints.push_back(std::move(endpoint));
      }
      continue;
    }

    if (auto* failed = lt::alert_cast<lt::listen_failed_alert>(alert)) {
      auto address = failed->address.to_string();
      if (failed->address.is_v6()) {
        address = '[' + address + ']';
      }
      const auto endpoint = address + ':' + std::to_string(failed->port);
      impl_->listenEndpoints.erase(
          std::remove(impl_->listenEndpoints.begin(),
                      impl_->listenEndpoints.end(), endpoint),
          impl_->listenEndpoints.end());
      A2_LOG_ERROR(failed->message());
      continue;
    }

    if (auto* external = lt::alert_cast<lt::external_ip_alert>(alert)) {
      const auto address = external->external_address.to_string();
      auto& previous = external->external_address.is_v4()
                           ? impl_->externalAddressV4
                           : impl_->externalAddressV6;
      previous = address;
      impl_->externalAddress = address;
      continue;
    }

    if (auto* mapped = lt::alert_cast<lt::portmap_alert>(alert)) {
      if (mapped->map_protocol == lt::portmap_protocol::tcp) {
        impl_->mappedTcpPort = static_cast<uint16_t>(mapped->external_port);
      }
      else if (mapped->map_protocol == lt::portmap_protocol::udp) {
        impl_->mappedUdpPort = static_cast<uint16_t>(mapped->external_port);
      }
      impl_->portMappingError.clear();
      continue;
    }

    if (auto* mapped = lt::alert_cast<lt::portmap_error_alert>(alert)) {
      const auto error = mapped->error.message();
      if (impl_->portMappingError != error) {
        A2_LOG_WARN(fmt("BitTorrent port mapping failed: %s", error.c_str()));
        impl_->portMappingError = error;
      }
      continue;
    }

    if (auto* stats = lt::alert_cast<lt::dht_stats_alert>(alert)) {
      impl_->dhtNodes = 0;
      impl_->dhtReplacements = 0;
      for (const auto& bucket : stats->routing_table) {
        impl_->dhtNodes += static_cast<size_t>(bucket.num_nodes);
        impl_->dhtReplacements += static_cast<size_t>(bucket.num_replacements);
      }
      impl_->dhtActiveRequests = stats->active_requests.size();
      continue;
    }

    if (auto* dropped = lt::alert_cast<lt::alerts_dropped_alert>(alert)) {
      impl_->droppedAlerts += dropped->dropped_alerts.count();
      if (dropped->dropped_alerts.test(lt::file_prio_alert::alert_type)) {
        for (const auto& entry : impl_->handles) {
          const auto download = entry.second.lock();
          if (download && download->impl_->filePriorityUpdatePending &&
              download->impl_->handle.is_valid() &&
              download->impl_->handle.in_session()) {
            download->impl_->handle.post_file_priorities();
          }
        }
      }
      A2_LOG_ERROR(dropped->message());
      continue;
    }

    if (auto* proxyError = lt::alert_cast<lt::socks5_alert>(alert)) {
      A2_LOG_ERROR(proxyError->message());
      continue;
    }

    if (auto* warning = lt::alert_cast<lt::performance_alert>(alert)) {
      const auto code = static_cast<size_t>(warning->warning_code);
      if (code < impl_->performanceWarnings.size()) {
        if (impl_->performanceWarnings[code]++ == 0) {
          A2_LOG_WARN(warning->message());
        }
      }
      impl_->lastPerformanceWarning = warning->message();
      continue;
    }

    if (auto* banned = lt::alert_cast<lt::ip_ban_alert>(alert)) {
      A2_LOG_DEBUG(banned->message());
      continue;
    }
  }

  if (impl_->lastUpdate.isZero() ||
      impl_->lastUpdate.difference(global::wallclock()) >= 1_s) {
    impl_->lastUpdate = global::wallclock();
    auto query =
        lt::torrent_handle::query_name | lt::torrent_handle::query_save_path;
    if (impl_->lastPieceUpdate.isZero() ||
        impl_->lastPieceUpdate.difference(global::wallclock()) >= 5_s) {
      query |= lt::torrent_handle::query_pieces;
      impl_->lastPieceUpdate = global::wallclock();
    }
    impl_->session->post_torrent_updates(query);
    const bool updatePeers =
        impl_->lastPeerUpdate.isZero() ||
        impl_->lastPeerUpdate.difference(global::wallclock()) >= 2_s;
    if (updatePeers) {
      impl_->lastPeerUpdate = global::wallclock();
    }
    for (const auto& entry : impl_->downloads) {
      const auto& download = entry.second;
      if (download->impl_->handle.is_valid() && download->active()) {
        if (download->hasMetadata() &&
            !download->snapshot_.selectedComplete) {
          download->impl_->handle.post_file_progress({});
        }
        if (updatePeers) {
          download->impl_->handle.post_peer_info();
        }
        if (download->impl_->lastTrackerUpdate.isZero() ||
            download->impl_->lastTrackerUpdate.difference(
                global::wallclock()) >= 10_s) {
          download->snapshot_.trackers =
              makeTrackers(download->impl_->handle, download.get());
          download->impl_->lastTrackerUpdate = global::wallclock();
        }
        requestResumeCheckpoint(download.get());
      }
    }
  }

  if (impl_->lastSessionStateSave.difference(global::wallclock()) >=
      std::chrono::minutes(1)) {
    saveSessionState(impl_.get());
  }
  if (impl_->lastDhtStats.isZero() ||
      impl_->lastDhtStats.difference(global::wallclock()) >= 10_s) {
    impl_->lastDhtStats = global::wallclock();
    impl_->session->post_dht_stats();
  }
  if (impl_->lastSessionStats.isZero() ||
      impl_->lastSessionStats.difference(global::wallclock()) >= 2_s) {
    impl_->lastSessionStats = global::wallclock();
    impl_->session->post_session_stats();
  }
}

void BtSession::requestStop(const std::shared_ptr<BtDownload>& download,
                            BtDownload::StopReason reason)
{
  download->requestStop(reason);
  if (reason == BtDownload::StopReason::Stop &&
      download->shutdownStage() == BtDownload::ShutdownStage::PendingHandle &&
      download->impl_->handle.is_valid()) {
    beginNativeDelete(download, DeleteIntent::Permanent);
    return;
  }
  if (download->shutdownStage() != BtDownload::ShutdownStage::PendingHandle) {
    return;
  }
  if (!download->impl_->handle.is_valid()) {
    return;
  }
  download->beginSavingResume();
  download->impl_->checkpointPending = false;
  download->impl_->handle.pause();
  if (download->impl_->resumeSaveOutstanding) {
    download->impl_->stopSavePending = true;
    return;
  }
  download->impl_->resumeSaveOutstanding = true;
  download->impl_->handle.save_resume_data(
      lt::torrent_handle::save_info_dict);
}

void BtSession::applyGlobalOptions(const Option* option)
{
  auto routeAddresses = detectBtRouteAddresses(option);
  if (routeAddresses.empty() && impl_->config.automaticRoute) {
    routeAddresses = impl_->config.routeAddresses;
  }
  auto config = makeBtConfig(option, routeAddresses);
  const bool networkChanged = !impl_->config.hasSameNetwork(config);
  impl_->session->apply_settings(config.settings);
  impl_->config = std::move(config);
  impl_->downloadRateLimit =
      option->getAsInt(PREF_MAX_OVERALL_DOWNLOAD_LIMIT);
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

void BtSession::validateGlobalOptions(const Option* option) const
{
  makeBtConfig(option);
}

bool BtSession::applyDownloadOptions(
    const std::shared_ptr<BtDownload>& download, const Option* option)
{
  return applyDownloadOptionsInternal(
      download, option,
      download && !download->awaitingFileSelection() &&
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
    download->impl_->appliedTrackerRevision =
        download->impl_->trackerRevision;
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
    const auto found = std::find_if(
        entries.begin(), entries.end(), [&tracker](const BtTrackerSpec& entry) {
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

void BtSession::discard(const std::shared_ptr<BtDownload>& download)
{
  if (!download) {
    return;
  }
  if (!download->impl_->resumePath.empty()) {
    File(download->impl_->resumePath).remove();
  }
  if (download->group()) {
    impl_->downloads[download->group()->getGID()] = download;
  }
  download->group_ = nullptr;
  download->stopReason_ = BtDownload::StopReason::Stop;
  download->shutdownStage_ = BtDownload::ShutdownStage::Removing;
  if (download->impl_->nativeState == BtNativeState::Adding) {
    download->impl_->nativeState = BtNativeState::Removing;
    return;
  }
  const auto handle = download->impl_->handle;
  if (!handle.is_valid() || !handle.in_session()) {
    forgetHandles(download.get());
    impl_->downloads.erase(download->impl_->gid);
    download->impl_->nativeState = BtNativeState::Detached;
    return;
  }
  beginNativeDelete(download, DeleteIntent::Permanent);
}

uint16_t BtSession::listenPort() const { return impl_->listenPort; }

uint16_t BtSession::announcePort() const
{
  const auto proxyType =
      impl_->config.settings.get_int(lt::settings_pack::proxy_type);
  if (proxyType == lt::settings_pack::http ||
      proxyType == lt::settings_pack::http_pw) {
    return 1;
  }
  const auto configured =
      impl_->config.settings.get_int(lt::settings_pack::announce_port);
  return configured == 0 ? impl_->listenPort
                         : static_cast<uint16_t>(configured);
}

std::string BtSession::externalAddress() const
{
  const auto configured =
      impl_->config.settings.get_str(lt::settings_pack::announce_ip);
  return configured.empty() ? impl_->externalAddress : configured;
}

BtSessionStatus BtSession::status() const
{
  BtSessionStatus result;
  result.listenEndpoints = impl_->listenEndpoints;
  result.externalAddress = externalAddress();
  result.portMappingError = impl_->portMappingError;
  result.listenPort = listenPort();
  result.announcePort = announcePort();
  result.mappedTcpPort = impl_->mappedTcpPort;
  result.mappedUdpPort = impl_->mappedUdpPort;
  result.dhtNodes = impl_->dhtNodes;
  result.dhtReplacements = impl_->dhtReplacements;
  result.dhtActiveRequests = impl_->dhtActiveRequests;
  result.droppedAlerts = impl_->droppedAlerts;
  result.peerSockets = impl_->peerSockets;
  for (const auto& entry : impl_->downloads) {
    result.establishedPeers += entry.second->snapshot_.numPeers;
    result.handshakingPeers += entry.second->snapshot_.handshakingPeers;
  }
  result.halfOpenPeers = impl_->halfOpenPeers;
  result.tcpPeers = impl_->tcpPeers;
  result.utpPeers = impl_->utpPeers;
  result.queuedTrackerAnnounces = impl_->queuedTrackerAnnounces;
  result.connectionAttempts = impl_->connectionAttempts;
  result.connectionTimeouts = impl_->connectionTimeouts;
  result.payloadDownloaded = impl_->payloadDownloaded;
  result.payloadUploaded = impl_->payloadUploaded;
  result.trackerDownloaded = impl_->trackerDownloaded;
  result.trackerUploaded = impl_->trackerUploaded;
  result.ipOverheadDownloaded = impl_->ipOverheadDownloaded;
  result.ipOverheadUploaded = impl_->ipOverheadUploaded;
  result.dhtDownloaded = impl_->dhtDownloaded;
  result.dhtUploaded = impl_->dhtUploaded;
  result.diskBlocksInUse = impl_->diskBlocksInUse;
  result.queuedDiskJobs = impl_->queuedDiskJobs;
  result.averageDiskJobTime = impl_->averageDiskJobTime;
  result.diskRequestLatency = impl_->diskRequestLatency;
  result.diskReadWaitingPeers = impl_->diskReadWaitingPeers;
  result.diskWriteWaitingPeers = impl_->diskWriteWaitingPeers;
  result.lastPerformanceWarning = impl_->lastPerformanceWarning;
  for (size_t i = 0; i < impl_->performanceWarnings.size(); ++i) {
    if (impl_->performanceWarnings[i] == 0) {
      continue;
    }
    result.performanceWarnings.emplace_back(
        lt::performance_warning_str(
            static_cast<lt::performance_alert::performance_warning_t>(i)),
        impl_->performanceWarnings[i]);
  }
  result.dhtStateHealthy = impl_->dhtNodes > 0 ||
                           !impl_->lastSessionState.empty();
  return result;
}

int BtSession::downloadSpeed() const
{
  int64_t speed = 0;
  for (const auto& entry : impl_->downloads) {
    if (entry.second->group()) {
      speed += entry.second->group()->calculateStat().downloadSpeed;
    }
  }
  return static_cast<int>(std::min<int64_t>(speed, INT_MAX));
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
