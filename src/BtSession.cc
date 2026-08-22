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

#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>
#include <sstream>
#include <utility>

#include <libtorrent/alert.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/announce_entry.hpp>
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
#include "wallclock.h"

namespace aria2 {

namespace lt = libtorrent;

struct BtSession::Impl {
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
    int dhtNodes = -1;
    int dhtReplacements = -1;
  } metrics;

  const Option* option;
  std::atomic<bool> alertsPending{true};
  std::unique_ptr<lt::session> session;
  std::map<a2_gid_t, std::shared_ptr<BtDownload>> downloads;
  std::map<lt::torrent_handle, BtDownload*> handles;
  BtPeerBlocklist blocklist;
  uint64_t filterRevision = 0;
  uint16_t listenPort = 0;
  uint16_t announcePort = 0;
  std::string externalAddress;
  Timer lastUpdate = Timer::zero();
  Timer lastPieceUpdate = Timer::zero();
  Timer lastPeerUpdate = Timer::zero();
  Timer lastDhtStats = Timer::zero();
  Timer lastSessionStats = Timer::zero();
  Timer lastSessionStateSave = Timer::zero();
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
  uint64_t payloadDownloaded = 0;
  uint64_t payloadUploaded = 0;
  uint64_t trackerDownloaded = 0;
  uint64_t trackerUploaded = 0;

  explicit Impl(const Option* option)
      : option(option),
        sessionStateFile(option->get(PREF_BT_SESSION_STATE_FILE))
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

lt::session_params makeSessionParams(const Option* option,
                                     std::string& loadedState)
{
  loadedState = readStateFile(option->get(PREF_BT_SESSION_STATE_FILE));
  if (loadedState.empty()) {
    return lt::session_params(makeBtSettings(option));
  }

  try {
    auto params = lt::read_session_params(
        lt::span<char const>(loadedState.data(), loadedState.size()),
        lt::session::save_dht_state);
    params.settings = makeBtSettings(option);
    A2_LOG_INFO(fmt("Loaded BitTorrent session state from %s",
                    option->get(PREF_BT_SESSION_STATE_FILE).c_str()));
    return params;
  }
  catch (const std::exception& error) {
    A2_LOG_WARN(fmt("Ignoring BitTorrent session state %s: %s",
                    option->get(PREF_BT_SESSION_STATE_FILE).c_str(),
                    error.what()));
    loadedState.clear();
    return lt::session_params(makeBtSettings(option));
  }
}

void saveSessionState(BtSession::Impl* impl)
{
  if (!impl || !impl->session || impl->sessionStateFile.empty()) {
    return;
  }
  try {
    const auto params =
        impl->session->session_state(lt::session::save_dht_state);
    const auto buffer =
        lt::write_session_params_buf(params, lt::session::save_dht_state);
    std::string state(buffer.begin(), buffer.end());
    if (state == impl->lastSessionState) {
      impl->lastSessionStateSave = global::wallclock();
      return;
    }
    if (!writeAtomic(impl->sessionStateFile, state.data(), state.size())) {
      A2_LOG_ERROR(fmt("Failed to save BitTorrent session state to %s",
                       impl->sessionStateFile.c_str()));
      return;
    }
    impl->lastSessionState = std::move(state);
    impl->lastSessionStateSave = global::wallclock();
    A2_LOG_DEBUG(fmt("Saved BitTorrent session state to %s",
                     impl->sessionStateFile.c_str()));
  }
  catch (const std::exception& error) {
    A2_LOG_ERROR(
        fmt("Failed to serialize BitTorrent session state: %s", error.what()));
  }
}

BtSnapshot::State translateState(const lt::torrent_status& status)
{
  if (status.errc) {
    return BtSnapshot::State::Error;
  }
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

std::vector<BtTrackerSnapshot> makeTrackers(const lt::torrent_handle& handle)
{
  std::vector<BtTrackerSnapshot> result;
  const auto hashes = handle.info_hashes();
  for (const auto& tracker : handle.trackers()) {
    BtTrackerSnapshot snapshot;
    snapshot.url = tracker.url;
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

void applySelection(const lt::torrent_handle& handle, RequestGroup* group)
{
  if (!group) {
    return;
  }
  group->getBtDownload()->updateSelection(group->getDownloadContext());
  if (!handle.is_valid()) {
    return;
  }
  std::vector<lt::download_priority_t> priorities;
  const auto info = handle.torrent_file();
  const auto& savePath = group->getOption()->get(PREF_DIR);
  size_t index = 0;
  for (const auto& file : group->getDownloadContext()->getFileEntries()) {
    priorities.push_back(file->isRequested() ? lt::default_priority
                                             : lt::dont_download);
    if (info && index < static_cast<size_t>(info->layout().num_files())) {
      auto storagePath = file->getPath();
      if (!savePath.empty() && storagePath.size() > savePath.size() &&
          storagePath.compare(0, savePath.size(), savePath) == 0 &&
          (storagePath[savePath.size()] == '/' ||
           storagePath[savePath.size()] == '\\')) {
        storagePath.erase(0, savePath.size() + 1);
      }
      if (storagePath !=
          info->layout().file_path(lt::file_index_t{static_cast<int>(index)})) {
        handle.rename_file(lt::file_index_t{static_cast<int>(index)},
                           storagePath);
      }
    }
    ++index;
  }
  if (!priorities.empty()) {
    handle.prioritize_files(priorities);
  }

  if (!info) {
    return;
  }
  const bool prioritizeBoundaries =
      group->getOption()->getAsBool(PREF_BT_FIRST_LAST_PIECE_FIRST);

  std::vector<lt::download_priority_t> piecePriorities(
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
      piecePriorities[static_cast<size_t>(piece)] = lt::default_priority;
    }
    const auto boundaryPieces = std::max<int64_t>(
        1, static_cast<int64_t>(std::ceil(static_cast<long double>(fileSize) *
                                          0.01L / pieceLength)));
    for (int64_t offset = 0; offset < boundaryPieces; ++offset) {
      const auto front = static_cast<int64_t>(static_cast<int>(first)) + offset;
      const auto back = static_cast<int64_t>(static_cast<int>(last)) - offset;
      if (prioritizeBoundaries && front <= static_cast<int>(last)) {
        piecePriorities[static_cast<size_t>(front)] = lt::top_priority;
      }
      if (prioritizeBoundaries && back >= static_cast<int>(first)) {
        piecePriorities[static_cast<size_t>(back)] = lt::top_priority;
      }
    }
    ++index;
  }
  handle.prioritize_pieces(piecePriorities);
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
  if (!force && !download->impl_->lastResumeSave.isZero() &&
      download->impl_->lastResumeSave.difference(global::wallclock()) <
          std::chrono::minutes(60)) {
    return;
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
  download->impl_->handle.resume();
}

BtSession::BtSession(const Option* option) : impl_(make_unique<Impl>(option))
{
  auto params = makeSessionParams(option, impl_->lastSessionState);
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

std::unique_ptr<Command>
BtSession::start(const std::shared_ptr<BtDownload>& download,
                 RequestGroup* group, DownloadEngine* engine)
{
  download->prepareStart();
  download->initialize(group);
  download->configure(group->getOption().get());
  download->impl_->params.userdata = download.get();

  if (download->impl_->params.ti) {
    updateDownloadContext(download.get(), group);
    download->impl_->params.file_priorities.clear();
    for (const auto& file : group->getDownloadContext()->getFileEntries()) {
      download->impl_->params.file_priorities.push_back(
          file->isRequested() ? lt::default_priority : lt::dont_download);
    }
  }

  impl_->downloads[group->getGID()] = download;
  if (download->impl_->handle.is_valid() &&
      download->impl_->handle.in_session()) {
    impl_->handles[download->impl_->handle] = download.get();
    download->impl_->handle.clear_error();
    applyDownloadOptions(download, group->getOption().get());
    if (download->impl_->fileSelectionResumePending &&
        !group->getDownloadContext()->getFileEntries().empty()) {
      download->impl_->fileSelectionResumePending = false;
      download->impl_->resumeAfterFilePriority = true;
    }
    else {
      download->impl_->fileSelectionResumePending = false;
      resumeTorrent(download.get());
    }
  }
  else {
    impl_->session->async_add_torrent(download->impl_->params);
  }
  return make_unique<BtDownloadCommand>(engine->newCUID(), download, this,
                                        group, engine);
}

void BtSession::poll()
{
  std::vector<lt::alert*> alerts;
  if (impl_->alertsPending.exchange(false)) {
    impl_->session->pop_alerts(&alerts);
  }
  for (auto* alert : alerts) {
    if (auto* added = lt::alert_cast<lt::add_torrent_alert>(alert)) {
      auto* download = added->params.userdata.get<BtDownload>();
      if (!download) {
        continue;
      }
      if (added->error) {
        download->snapshot_.state = BtSnapshot::State::Error;
        download->snapshot_.errorMessage = added->error.message();
        download->shutdownStage_ = BtDownload::ShutdownStage::Complete;
        continue;
      }
      download->impl_->handle = added->handle;
      impl_->handles[added->handle] = download;
      if (download->stopRequested()) {
        requestStop(impl_->downloads.at(download->group()->getGID()),
                    download->stopReason());
      }
      else {
        applySelection(download->impl_->handle, download->group());
        if (download->group()->getOption()->getAsBool(PREF_CHECK_INTEGRITY) &&
            !download->impl_->initialRecheckStarted) {
          download->impl_->initialRecheckStarted = true;
          download->impl_->handle.force_recheck();
        }
      }
      continue;
    }

    auto findDownload =
        [this](const lt::torrent_handle& handle) -> BtDownload* {
      auto found = impl_->handles.find(handle);
      return found == impl_->handles.end() ? nullptr : found->second;
    };

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
      impl_->dhtNodes = value(impl_->metrics.dhtNodes);
      impl_->dhtReplacements = value(impl_->metrics.dhtReplacements);
      continue;
    }

    if (auto* update = lt::alert_cast<lt::state_update_alert>(alert)) {
      for (const auto& status : update->status) {
        auto* download = findDownload(status.handle);
        if (!download) {
          continue;
        }
        auto& snapshot = download->snapshot_;
        snapshot.state = translateState(status);
        snapshot.name = status.name.empty() ? snapshot.name : status.name;
        snapshot.currentTracker = status.current_tracker;
        snapshot.totalLength = status.total_wanted;
        snapshot.completedLength = status.total_wanted_done;
        snapshot.allTimeDownload = status.all_time_download;
        snapshot.allTimeUpload = status.all_time_upload;
        snapshot.failedBytes = status.total_failed_bytes;
        snapshot.redundantBytes = status.total_redundant_bytes;
        snapshot.downloadSpeed = status.download_payload_rate;
        snapshot.uploadSpeed = status.upload_payload_rate;
        snapshot.numComplete = status.num_complete;
        snapshot.numIncomplete = status.num_incomplete;
        snapshot.progressPpm = status.progress_ppm;
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
        snapshot.finished = status.is_finished;
        snapshot.seeding = status.is_seeding;
        if (status.errc) {
          snapshot.errorMessage = status.errc.message();
        }
      }
      continue;
    }

    if (auto* peers = lt::alert_cast<lt::peer_info_alert>(alert)) {
      auto* download = findDownload(peers->handle);
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
      auto* download = findDownload(progress->handle);
      if (download) {
        const auto count =
            std::min(download->snapshot_.files.size(),
                     static_cast<size_t>(progress->files.size()));
        for (size_t i = 0; i < count; ++i) {
          download->snapshot_.files[i].completedLength =
              progress->files[lt::file_index_t{static_cast<int>(i)}];
        }
      }
      continue;
    }

    if (auto* priorities = lt::alert_cast<lt::file_prio_alert>(alert)) {
      auto* download = findDownload(priorities->handle);
      if (download && download->impl_->resumeAfterFilePriority) {
        download->impl_->resumeAfterFilePriority = false;
        if (priorities->error) {
          download->snapshot_.state = BtSnapshot::State::Error;
          download->snapshot_.errorMessage = priorities->error.message();
          download->recoverableError_ = true;
        }
        else {
          resumeTorrent(download);
        }
      }
      continue;
    }

    if (auto* metadata = lt::alert_cast<lt::metadata_received_alert>(alert)) {
      auto* download = findDownload(metadata->handle);
      if (download && download->group()) {
        const bool pauseForSelection = download->shouldPauseAfterMetadata();
        auto info = metadata->handle.torrent_file();
        if (info) {
          download->impl_->params.ti =
              std::make_shared<lt::torrent_info>(*info);
          download->impl_->params.info_hashes = info->info_hashes();
          for (const auto& tracker : metadata->handle.trackers()) {
            if (download->impl_->customTrackers) {
              break;
            }
            if (std::find(download->impl_->sourceTrackers.begin(),
                          download->impl_->sourceTrackers.end(), tracker.url) ==
                download->impl_->sourceTrackers.end()) {
              download->impl_->sourceTrackers.push_back(tracker.url);
              download->impl_->sourceTrackerTiers.push_back(tracker.tier);
            }
          }
          download->configure(download->group()->getOption().get());
          updateDownloadContext(download, download->group());
          auto managed = impl_->downloads.at(download->group()->getGID());
          if (pauseForSelection) {
            download->group()->setHaltRequested(true, RequestGroup::NONE);
            download->group()->setPauseRequested(true);
            requestStop(managed, BtDownload::StopReason::FileSelection);
          }
          else {
            applyDownloadOptions(managed, download->group()->getOption().get());
            requestResumeCheckpoint(download, true);
          }
        }
      }
      continue;
    }

    if (auto* finished = lt::alert_cast<lt::torrent_finished_alert>(alert)) {
      auto* download = findDownload(finished->handle);
      if (download) {
        requestResumeCheckpoint(download, true);
      }
      continue;
    }

    if (auto* saved = lt::alert_cast<lt::save_resume_data_alert>(alert)) {
      auto* download = findDownload(saved->handle);
      if (download) {
        try {
          saveResume(download->impl_->resumePath, saved->params);
        }
        catch (RecoverableException& error) {
          A2_LOG_ERROR_EX("Failed to save BitTorrent resume data", error);
        }
        const bool saveStopState = download->impl_->stopSavePending;
        finishResumeSave(download);
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
            download->beginRemoving();
            impl_->session->remove_torrent(saved->handle);
          }
        }
      }
      continue;
    }

    if (auto* failed =
            lt::alert_cast<lt::save_resume_data_failed_alert>(alert)) {
      auto* download = findDownload(failed->handle);
      if (download) {
        if (failed->error != lt::errors::resume_data_not_modified) {
          A2_LOG_ERROR(fmt("Failed to save BitTorrent resume data %s: %s",
                           download->impl_->resumePath.c_str(),
                           failed->error.message().c_str()));
        }
        const bool saveStopState = download->impl_->stopSavePending;
        finishResumeSave(download);
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
            download->beginRemoving();
            impl_->session->remove_torrent(failed->handle);
          }
        }
      }
      continue;
    }

    if (auto* removed = lt::alert_cast<lt::torrent_removed_alert>(alert)) {
      auto* download = findDownload(removed->handle);
      if (download) {
        download->finishStopping();
        impl_->handles.erase(removed->handle);
        for (auto it = impl_->downloads.begin();
             it != impl_->downloads.end();) {
          if (it->second.get() == download) {
            it = impl_->downloads.erase(it);
          }
          else {
            ++it;
          }
        }
      }
      continue;
    }

    if (auto* error = lt::alert_cast<lt::file_error_alert>(alert)) {
      auto* download = findDownload(error->handle);
      if (download) {
        download->impl_->resumeAfterFilePriority = false;
        download->snapshot_.state = BtSnapshot::State::Error;
        download->snapshot_.errorMessage = error->message();
        download->recoverableError_ = true;
      }
      A2_LOG_ERROR(error->message());
      continue;
    }

    if (auto* error = lt::alert_cast<lt::torrent_error_alert>(alert)) {
      auto* download = findDownload(error->handle);
      if (download && !download->recoverableError_) {
        download->snapshot_.state = BtSnapshot::State::Error;
        download->snapshot_.errorMessage = error->error.message();
      }
      continue;
    }

    if (auto* moved = lt::alert_cast<lt::storage_moved_alert>(alert)) {
      auto* download = findDownload(moved->handle);
      if (download && download->group()) {
        download->impl_->params.save_path = moved->storage_path();
        download->impl_->previousSavePath.clear();
        requestResumeCheckpoint(download, true);
      }
      continue;
    }

    if (auto* moved = lt::alert_cast<lt::storage_moved_failed_alert>(alert)) {
      auto* download = findDownload(moved->handle);
      if (download && download->group()) {
        auto& option = download->group()->getOption();
        option->put(PREF_DIR, download->impl_->previousSavePath);
        download->impl_->params.save_path = download->impl_->previousSavePath;
        download->updateFilePaths(download->group()->getDownloadContext(),
                                  option.get());
        download->updateSelection(download->group()->getDownloadContext());
        download->impl_->previousSavePath.clear();
        download->snapshot_.errorMessage = moved->error.message();
      }
      A2_LOG_ERROR(moved->message());
      continue;
    }

    if (auto* renamed = lt::alert_cast<lt::file_rename_failed_alert>(alert)) {
      auto* download = findDownload(renamed->handle);
      if (download) {
        download->snapshot_.errorMessage = renamed->error.message();
      }
      A2_LOG_ERROR(renamed->message());
      continue;
    }

    if (auto* tracker = lt::alert_cast<lt::tracker_error_alert>(alert)) {
      A2_LOG_DEBUG(tracker->message());
      continue;
    }

    if (auto* tracker = lt::alert_cast<lt::tracker_warning_alert>(alert)) {
      A2_LOG_WARN(tracker->message());
      continue;
    }

    if (auto* tracker = lt::alert_cast<lt::tracker_reply_alert>(alert)) {
      A2_LOG_DEBUG(tracker->message());
      continue;
    }

    if (auto* connected = lt::alert_cast<lt::peer_connect_alert>(alert)) {
      A2_LOG_DEBUG(connected->message());
      continue;
    }

    if (auto* disconnected =
            lt::alert_cast<lt::peer_disconnected_alert>(alert)) {
      A2_LOG_DEBUG(disconnected->message());
      continue;
    }

    if (auto* peerError = lt::alert_cast<lt::peer_error_alert>(alert)) {
      A2_LOG_DEBUG(peerError->message());
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
      A2_LOG_ERROR(failed->message());
      continue;
    }

    if (auto* external = lt::alert_cast<lt::external_ip_alert>(alert)) {
      impl_->externalAddress = external->external_address.to_string();
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
      impl_->portMappingError = mapped->error.message();
      A2_LOG_WARN(mapped->message());
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
      A2_LOG_ERROR(dropped->message());
      continue;
    }

    if (auto* proxyError = lt::alert_cast<lt::socks5_alert>(alert)) {
      A2_LOG_ERROR(proxyError->message());
      continue;
    }

    if (auto* warning = lt::alert_cast<lt::performance_alert>(alert)) {
      A2_LOG_WARN(warning->message());
      continue;
    }

    if (auto* banned = lt::alert_cast<lt::ip_ban_alert>(alert)) {
      A2_LOG_INFO(banned->message());
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
        if (updatePeers) {
          download->impl_->handle.post_peer_info();
          download->impl_->handle.post_file_progress(
              lt::torrent_handle::piece_granularity);
        }
        if (download->impl_->lastTrackerUpdate.isZero() ||
            download->impl_->lastTrackerUpdate.difference(
                global::wallclock()) >= 10_s) {
          download->snapshot_.trackers = makeTrackers(download->impl_->handle);
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
  auto flags = lt::torrent_handle::save_info_dict;
  if (reason == BtDownload::StopReason::Stop) {
    flags |= lt::torrent_handle::flush_disk_cache;
  }
  download->impl_->handle.save_resume_data(flags);
}

void BtSession::applyGlobalOptions(const Option* option)
{
  impl_->session->apply_settings(makeBtSettings(option));
}

void BtSession::validateGlobalOptions(const Option* option) const
{
  makeBtSettings(option);
}

void BtSession::applyDownloadOptions(
    const std::shared_ptr<BtDownload>& download, const Option* option)
{
  if (!download) {
    return;
  }
  const auto previousSavePath = download->impl_->params.save_path;
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
    return;
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
  applySelection(handle, download->group());
  requestResumeCheckpoint(download.get());
}

void BtSession::forceReannounce(const std::shared_ptr<BtDownload>& download)
{
  if (!download || !download->impl_->handle.is_valid() ||
      !download->impl_->handle.in_session()) {
    throw DL_ABORT_EX("BitTorrent task is not present in the session");
  }
  download->impl_->handle.force_reannounce();
  download->impl_->handle.force_dht_announce();
}

void BtSession::forceRecheck(const std::shared_ptr<BtDownload>& download)
{
  if (!download || !download->impl_->handle.is_valid() ||
      !download->impl_->handle.in_session()) {
    throw DL_ABORT_EX("BitTorrent task is not present in the session");
  }
  download->snapshot_.state = BtSnapshot::State::Checking;
  download->impl_->handle.force_recheck();
}

void BtSession::replaceTrackers(const std::shared_ptr<BtDownload>& download,
                                const std::vector<BtTrackerConfig>& trackers)
{
  if (!download || !download->group()) {
    throw DL_ABORT_EX("BitTorrent task is not available");
  }

  std::vector<std::string> urls;
  std::vector<int> tiers;
  urls.reserve(trackers.size());
  tiers.reserve(trackers.size());
  for (const auto& tracker : trackers) {
    if (tracker.url.empty() || tracker.tier < 0 || tracker.tier > 255) {
      throw DL_ABORT_EX("Invalid BitTorrent tracker entry");
    }
    if (!util::startsWith(tracker.url, "http://") &&
        !util::startsWith(tracker.url, "https://") &&
        !util::startsWith(tracker.url, "udp://")) {
      throw DL_ABORT_EX("BitTorrent trackers must use HTTP, HTTPS, or UDP");
    }
    const auto found = std::find(urls.begin(), urls.end(), tracker.url);
    if (found != urls.end()) {
      continue;
    }
    urls.push_back(tracker.url);
    tiers.push_back(tracker.tier);
  }

  download->impl_->sourceTrackers = std::move(urls);
  download->impl_->sourceTrackerTiers = std::move(tiers);
  download->impl_->customTrackers = true;
  applyDownloadOptions(download, download->group()->getOption().get());
  requestResumeCheckpoint(download.get(), true);
}

void BtSession::discard(const std::shared_ptr<BtDownload>& download)
{
  if (!download) {
    return;
  }
  if (!download->impl_->resumePath.empty()) {
    File(download->impl_->resumePath).remove();
  }
  const auto handle = download->impl_->handle;
  if (!handle.is_valid() || !handle.in_session()) {
    return;
  }
  if (download->group()) {
    impl_->downloads[download->group()->getGID()] = download;
  }
  download->group_ = nullptr;
  download->stopReason_ = BtDownload::StopReason::Stop;
  download->shutdownStage_ = BtDownload::ShutdownStage::Removing;
  impl_->session->remove_torrent(handle, lt::session::delete_partfile);
}

void BtSession::remove(a2_gid_t gid) { impl_->downloads.erase(gid); }

uint16_t BtSession::listenPort() const { return impl_->listenPort; }

uint16_t BtSession::announcePort() const
{
  return impl_->option->getAsInt(PREF_BT_EXTERNAL_PORT) == 0
             ? impl_->listenPort
             : static_cast<uint16_t>(
                   impl_->option->getAsInt(PREF_BT_EXTERNAL_PORT));
}

std::string BtSession::externalAddress() const
{
  return !impl_->option->blank(PREF_BT_EXTERNAL_IP)
             ? impl_->option->get(PREF_BT_EXTERNAL_IP)
             : impl_->externalAddress;
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
  return result;
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
