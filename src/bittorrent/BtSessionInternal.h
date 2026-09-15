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
#ifndef ARIA2_BT_SESSION_INTERNAL_H
#define ARIA2_BT_SESSION_INTERNAL_H
#include "BtSession.h"
#include "BtPeerBlocklist.h"
#include "BtSettings.h"
#include "TimerA2.h"
#include <libtorrent/alert_types.hpp>
#include <libtorrent/bitfield.hpp>
#include <libtorrent/download_priority.hpp>
#include <libtorrent/ip_filter.hpp>
#include <libtorrent/peer_info.hpp>
#include <libtorrent/socket.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <libtorrent/torrent_status.hpp>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace aria2 {
namespace lt = libtorrent;

// The engine thread owns session bookkeeping. Native asynchronous completion
// is applied only while draining alerts; no handler retains an alert pointer.
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
  explicit Impl(const Option* option);
  ~Impl();
};
namespace bt_session {
struct SelectionPlan {
  std::vector<lt::download_priority_t> files;
  std::vector<lt::download_priority_t> pieces;
  bool pieceOverrides = false;
};

bool writeAtomic(const std::string& path, const char* data, size_t size);
std::string readStateFile(const std::string& path);
bool hasDhtNodes(const lt::session_params& params);
lt::session_params makeSessionParams(const Option* option,
                                     const BtConfig& config,
                                     std::string& loadedState);
void saveSessionState(BtSession::Impl* impl);
void saveResume(const std::string& path, const lt::add_torrent_params& params);
void updatePayloadCounter(DownloadContext* context, int64_t current,
                          int64_t& previous, bool upload);
BtSnapshot::State translateState(const lt::torrent_status& status);
bool progressVerificationInProgress(const lt::torrent_status& status);
std::string bitfield(const lt::typed_bitfield<lt::piece_index_t>& pieces);
std::string peerFlags(const lt::peer_info& peer);
std::vector<std::string> peerSources(const lt::peer_info& peer);
BtPeerSnapshot makePeer(const lt::peer_info& peer, int64_t totalLength);
std::string endpointName(const lt::tcp::endpoint& endpoint);
std::vector<BtTrackerSnapshot> makeTrackers(const lt::torrent_handle& handle,
                                            const BtDownload* download);
std::vector<lt::download_priority_t> makeFilePriorities(RequestGroup* group);
SelectionPlan makeSelectionPlan(const lt::torrent_handle& handle,
                                RequestGroup* group,
                                bool restoreNativePiecePriorities);
void updateDownloadContext(BtDownload* download, RequestGroup* group);
lt::ip_filter makeIpFilter(const BtPeerBlocklist& blocklist);
std::string hashKey(const lt::info_hash_t& hashes);
} // namespace bt_session
} // namespace aria2
#endif
