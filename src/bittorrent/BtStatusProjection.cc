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
#include "BtDownload.h"
#include "BtSession.h"
#include "BtSnapshot.h"
#include "DownloadContext.h"
#include <algorithm>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/aux_/time.hpp>
#include <libtorrent/bitfield.hpp>
#include <libtorrent/peer_info.hpp>
#include <libtorrent/socket.hpp>
#include <libtorrent/time.hpp>
#include <libtorrent/torrent_flags.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <libtorrent/torrent_status.hpp>
#include <libtorrent/units.hpp>
#include <limits>
#include <string>
#include <utility>
#include <vector>
#include "support/Encoding.h"
#include "bittorrent/BtSessionInternal.h"

namespace aria2 {
namespace bt_session {
void updatePayloadCounter(DownloadContext* context, int64_t current,
                          int64_t& previous, bool upload)
{
  current = std::max<int64_t>(0, current);
  const auto delta = current >= previous ? current - previous : current;
  previous = current;
  auto remaining = static_cast<uint64_t>(delta);
  while (remaining > 0) {
    const auto bytes = static_cast<size_t>(
        std::min<uint64_t>(remaining, std::numeric_limits<size_t>::max()));
    if (upload) {
      context->updateUpload(bytes);
    }
    else {
      context->updateDownload(bytes);
    }
    remaining -= bytes;
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
        if (minAnnounce >= 0 && (snapshot.minAnnounceSeconds < 0 ||
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
} // namespace bt_session
using namespace bt_session;

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
  result.dhtStateHealthy =
      impl_->dhtNodes > 0 || !impl_->lastSessionState.empty();
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

} // namespace aria2
