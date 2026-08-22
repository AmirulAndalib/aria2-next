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

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "BtDownload.h"
#include "GroupId.h"

namespace aria2 {

class Command;
class DownloadEngine;
class Option;
class RequestGroup;

struct BtSessionStatus {
  std::vector<std::string> listenEndpoints;
  std::string externalAddress;
  std::string portMappingError;
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
  uint64_t networkEpoch = 0;
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
  std::unique_ptr<Impl> impl_;
  void requestResumeCheckpoint(BtDownload* download, bool force = false);
  void finishResumeSave(BtDownload* download);
  void discardRemovedResume(BtDownload* download);
  void resumeTorrent(BtDownload* download);
  void activateDiscovery(BtDownload* download);
  void syncDiscovery(BtDownload* download);
  void advanceNetworkEpoch();

public:
  explicit BtSession(const Option* option);
  ~BtSession();

  BtSession(const BtSession&) = delete;
  BtSession& operator=(const BtSession&) = delete;

  std::unique_ptr<Command> start(const std::shared_ptr<BtDownload>& download,
                                 RequestGroup* group, DownloadEngine* engine);

  void poll();
  void requestStop(const std::shared_ptr<BtDownload>& download,
                   BtDownload::StopReason reason);
  void validateGlobalOptions(const Option* option) const;
  void applyGlobalOptions(const Option* option);
  void applyDownloadOptions(const std::shared_ptr<BtDownload>& download,
                            const Option* option);
  void forceRecheck(const std::shared_ptr<BtDownload>& download);
  void replaceTrackers(const std::shared_ptr<BtDownload>& download,
                       const std::vector<BtTrackerConfig>& trackers);
  void discard(const std::shared_ptr<BtDownload>& download);
  void remove(a2_gid_t gid);

  uint16_t listenPort() const;
  uint16_t announcePort() const;
  std::string externalAddress() const;
  BtSessionStatus status() const;

  bool replaceIpFilter(const std::vector<std::string>& rules,
                       std::string& error);
  void loadIpFilter(const std::string& path);
  size_t ipFilterRuleCount() const;
  uint64_t ipFilterRevision() const;
};

} // namespace aria2

#endif // D_BT_SESSION_H
