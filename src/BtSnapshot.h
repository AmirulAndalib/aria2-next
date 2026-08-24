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
#ifndef D_BT_SNAPSHOT_H
#define D_BT_SNAPSHOT_H

#include "common.h"

#include <cstdint>
#include <string>
#include <vector>

namespace aria2 {

struct BtFileSnapshot {
  std::string path;
  int64_t length = 0;
  int64_t completedLength = 0;
  int priority = 1;
  bool selected = true;
};

struct BtPeerSnapshot {
  std::string peerId;
  std::string clientName;
  std::string ip;
  uint16_t port = 0;
  std::string bitfield;
  std::string flags;
  std::string state;
  std::string transport;
  std::string encryption;
  std::vector<std::string> sources;
  int64_t downloaded = 0;
  int64_t uploaded = 0;
  int64_t completedLength = 0;
  int downloadSpeed = 0;
  int uploadSpeed = 0;
  int progressPpm = 0;
  bool amChoking = false;
  bool amInterested = false;
  bool peerChoking = false;
  bool peerInterested = false;
  bool incoming = false;
  bool snubbed = false;
  bool optimisticUnchoke = false;
  bool seeder = false;
};

struct BtTrackerEndpointSnapshot {
  std::string localEndpoint;
  std::string protocol;
  std::string status;
  std::string message;
  int failures = 0;
  int seeders = -1;
  int leechers = -1;
  int downloads = -1;
  int64_t nextAnnounceSeconds = -1;
  int64_t minAnnounceSeconds = -1;
  bool updating = false;
  bool verified = false;
};

struct BtTrackerSnapshot {
  std::string url;
  std::string source;
  std::string status;
  std::string message;
  int tier = 0;
  int failures = 0;
  int seeders = -1;
  int leechers = -1;
  int downloads = -1;
  int64_t nextAnnounceSeconds = -1;
  int64_t minAnnounceSeconds = -1;
  bool updating = false;
  bool verified = false;
  std::vector<BtTrackerEndpointSnapshot> endpoints;
};

struct BtErrorSnapshot {
  bool present = false;
  bool recoverable = false;
  int code = 0;
  std::string kind;
  std::string category;
  std::string message;
  std::string operation;
  std::string file;
};

struct BtSnapshot {
  enum class State {
    Adding,
    DownloadingMetadata,
    Checking,
    Downloading,
    Recovering,
    Finished,
    Seeding,
    Paused,
    Stopping,
    Stopped,
    Error,
  };

  enum class FileSelectionState { None, Awaiting, Ready, Applying };

  State state = State::Adding;
  FileSelectionState fileSelectionState = FileSelectionState::None;
  BtErrorSnapshot error;
  std::string name;
  std::string infoHashV1;
  std::string infoHashV2;
  std::string magnetLink;
  std::string currentTracker;
  std::string bitfield;
  std::vector<std::string> webSeeds;
  std::vector<std::vector<std::string>> announceList;
  std::vector<BtFileSnapshot> files;
  std::vector<BtPeerSnapshot> peers;
  std::vector<BtTrackerSnapshot> trackers;
  int64_t totalLength = 0;
  int64_t completedLength = 0;
  int64_t allTimeDownload = 0;
  int64_t allTimeUpload = 0;
  int64_t failedBytes = 0;
  int64_t redundantBytes = 0;
  int downloadSpeed = 0;
  int uploadSpeed = 0;
  int numPeers = 0;
  int connectingPeers = 0;
  int handshakingPeers = 0;
  int numSeeds = 0;
  int numComplete = -1;
  int numIncomplete = -1;
  int progressPpm = 0;
  int queuePosition = -1;
  int seedingTime = 0;
  int activeTime = 0;
  int finishedTime = 0;
  int connectCandidates = 0;
  int numUploads = 0;
  int availabilityPpm = -1;
  bool privateTorrent = false;
  bool hasMetadata = false;
  bool selectedComplete = false;
  bool complete = false;
};

const char* btStateName(BtSnapshot::State state);
const char* btFileSelectionStateName(BtSnapshot::FileSelectionState state);

} // namespace aria2

#endif // D_BT_SNAPSHOT_H
