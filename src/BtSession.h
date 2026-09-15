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
#ifndef D_BT_SESSION_H
#define D_BT_SESSION_H

#include "common.h"
#include <libtorrent/fwd.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "BtDownload.h"
#include "GroupId.h"

namespace aria2 {

class Command;
class DownloadEngine;
class Option;
class RequestGroup;

struct BtSessionStatus {
  std::vector<std::pair<std::string, uint64_t>> performanceWarnings;
  std::vector<std::string> listenEndpoints;
  std::string externalAddress;
  std::string portMappingError;
  std::string lastPerformanceWarning;
  uint16_t listenPort = 0;
  uint16_t announcePort = 0;
  uint16_t mappedTcpPort = 0;
  uint16_t mappedUdpPort = 0;
  size_t dhtNodes = 0;
  size_t dhtReplacements = 0;
  size_t dhtActiveRequests = 0;
  size_t droppedAlerts = 0;
  size_t peerSockets = 0;
  size_t establishedPeers = 0;
  size_t handshakingPeers = 0;
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
  bool dhtStateHealthy = false;
};

struct BtTrackerConfig {
  std::string url;
  int tier = 0;
};

class BtSession {
public:
  struct Impl;

private:
  // Alert processing stays on the engine thread and uses native typed events.
  void dispatchAlert(libtorrent::alert* alert);
  void refreshNativeStatus();
  std::shared_ptr<BtDownload>
  findDownload(const libtorrent::torrent_handle& handle) const;
  static std::string gidFor(const std::shared_ptr<BtDownload>& download);
  void handleAlert(libtorrent::add_torrent_alert* alert);
  void handleAlert(libtorrent::log_alert* alert);
  void handleAlert(libtorrent::torrent_log_alert* alert);
  void handleAlert(libtorrent::peer_error_alert* alert);
  void handleAlert(libtorrent::peer_connect_alert* alert);
  void handleAlert(libtorrent::peer_disconnected_alert* alert);
  void handleAlert(libtorrent::tracker_announce_alert* alert);
  void handleAlert(libtorrent::tracker_reply_alert* alert);
  void handleAlert(libtorrent::dht_bootstrap_alert* alert);
  void handleAlert(libtorrent::torrent_checked_alert* alert);
  void handleAlert(libtorrent::fastresume_rejected_alert* alert);
  void handleAlert(libtorrent::hash_failed_alert* alert);
  void handleAlert(libtorrent::state_changed_alert* alert);
  void handleAlert(libtorrent::session_stats_alert* alert);
  void handleAlert(libtorrent::state_update_alert* alert);
  void handleAlert(libtorrent::peer_info_alert* alert);
  void handleAlert(libtorrent::file_progress_alert* alert);
  void handleAlert(libtorrent::file_prio_alert* alert);
  void handleAlert(libtorrent::file_priorities_alert* alert);
  void handleAlert(libtorrent::metadata_received_alert* alert);
  void handleAlert(libtorrent::torrent_finished_alert* alert);
  void handleAlert(libtorrent::save_resume_data_alert* alert);
  void handleAlert(libtorrent::save_resume_data_failed_alert* alert);
  void handleAlert(libtorrent::torrent_removed_alert* alert);
  void handleAlert(libtorrent::torrent_deleted_alert* alert);
  void handleAlert(libtorrent::torrent_delete_failed_alert* alert);
  void handleAlert(libtorrent::file_error_alert* alert);
  void handleAlert(libtorrent::torrent_error_alert* alert);
  void handleAlert(libtorrent::storage_moved_alert* alert);
  void handleAlert(libtorrent::storage_moved_failed_alert* alert);
  void handleAlert(libtorrent::file_rename_failed_alert* alert);
  void handleAlert(libtorrent::tracker_error_alert* alert);
  void handleAlert(libtorrent::tracker_warning_alert* alert);
  void handleAlert(libtorrent::listen_succeeded_alert* alert);
  void handleAlert(libtorrent::listen_failed_alert* alert);
  void handleAlert(libtorrent::external_ip_alert* alert);
  void handleAlert(libtorrent::portmap_alert* alert);
  void handleAlert(libtorrent::portmap_error_alert* alert);
  void handleAlert(libtorrent::dht_stats_alert* alert);
  void handleAlert(libtorrent::alerts_dropped_alert* alert);
  void handleAlert(libtorrent::socks5_alert* alert);
  void handleAlert(libtorrent::performance_alert* alert);
  void handleAlert(libtorrent::ip_ban_alert* alert);

  enum class AttachMode { Running, RestorePaused };
  enum class DeleteIntent { Replace, Permanent };

  std::unique_ptr<Impl> impl_;
  void attach(const std::shared_ptr<BtDownload>& download, RequestGroup* group,
              AttachMode mode);
  void requestResumeCheckpoint(BtDownload* download, bool force = false);
  void finishResumeSave(BtDownload* download);
  bool applyDownloadOptionsInternal(const std::shared_ptr<BtDownload>& download,
                                    const Option* option,
                                    bool synchronizeFileSelection);
  bool synchronizeSelection(BtDownload* download);
  void finishFilePriorityUpdate(BtDownload* download);
  void continueSelectionSynchronization(BtDownload* download);
  void failFilePriorityUpdate(BtDownload* download);
  void requestProgressRefresh(BtDownload* download);
  void resumeTorrent(BtDownload* download);
  void prepareFreshAdd(BtDownload* download);
  void beginNativeDelete(const std::shared_ptr<BtDownload>& download,
                         DeleteIntent intent);
  void forgetHandles(BtDownload* download);
  void finishNativeDelete(const std::string& key,
                          const std::string& error = {});
  bool recoverPartfile(const std::shared_ptr<BtDownload>& download,
                       const BtErrorSnapshot& error);
  void reopenNetworkSockets();

public:
  explicit BtSession(const Option* option);
  ~BtSession();

  BtSession(const BtSession&) = delete;
  BtSession& operator=(const BtSession&) = delete;

  std::unique_ptr<Command> start(const std::shared_ptr<BtDownload>& download,
                                 RequestGroup* group, DownloadEngine* engine);
  void restorePaused(const std::shared_ptr<BtDownload>& download,
                     RequestGroup* group);

  void poll();
  void requestStop(const std::shared_ptr<BtDownload>& download,
                   BtDownload::StopReason reason);
  void validateGlobalOptions(const Option* option) const;
  void applyGlobalOptions(const Option* option);
  void setGlobalDownloadLimit(int limit);
  bool applyDownloadOptions(const std::shared_ptr<BtDownload>& download,
                            const Option* option);
  void forceRecheck(const std::shared_ptr<BtDownload>& download);
  void forceAnnounce(const std::shared_ptr<BtDownload>& download);
  void replaceTrackers(const std::shared_ptr<BtDownload>& download,
                       const std::vector<BtTrackerConfig>& trackers);
  void replaceWebSeeds(const std::shared_ptr<BtDownload>& download,
                       const std::vector<std::string>& webSeeds);
  std::pair<size_t, size_t>
  addPeers(const std::shared_ptr<BtDownload>& download,
           const std::vector<std::string>& peers);
  void discard(const std::shared_ptr<BtDownload>& download);

  uint16_t listenPort() const;
  uint16_t announcePort() const;
  std::string externalAddress() const;
  BtSessionStatus status() const;
  int downloadSpeed() const;

  bool replaceIpFilter(const std::vector<std::string>& rules,
                       std::string& error);
  void loadIpFilter(const std::string& path);
  size_t ipFilterRuleCount() const;
  uint64_t ipFilterRevision() const;
};

} // namespace aria2

#endif // D_BT_SESSION_H
