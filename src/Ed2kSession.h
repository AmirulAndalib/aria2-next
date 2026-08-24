/* <!-- copyright */
/*
 * aria2 - The high speed download utility
 *
 * Copyright (C) 2026 aria2-next contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
/* copyright --> */
#ifndef D_ED2K_SESSION_H
#define D_ED2K_SESSION_H

#include <memory>
#include <string>
#include <vector>

#include "Ed2kKadState.h"
#include "Ed2kStore.h"
#include "ed2k_server.h"

namespace aria2 {

class Option;
class RequestGroup;

namespace ed2k {

class KadRoutingTable;
class UploadQueue;

class Ed2kSession {
public:
  Ed2kSession(UploadQueue* uploadQueue, std::string databasePath);
  ~Ed2kSession();

  void registerDownload(RequestGroup* group);
  void unregisterDownload(RequestGroup* group);
  void unregisterAllDownloads();
  void unregisterStoppedDownloads();
  void synchronizeNetworkState();

  const std::vector<RequestGroup*>& downloads() const { return downloads_; }
  RequestGroup* networkDownload() const;
  bool empty() const { return downloads_.empty(); }

  const std::string& databasePath() const { return databasePath_; }
  bool hasDownloadState(RequestGroup* group) const;
  bool loadDownloadState(RequestGroup* group);
  bool saveDownloadState(RequestGroup* group);
  bool removeDownloadState(RequestGroup* group);
  size_t restoreDownloads(
      const Option* option,
      std::vector<std::shared_ptr<RequestGroup>>& requestGroups);
  RequestGroup* findAlternativeDownload(RequestGroup* current,
                                        const Endpoint& peer) const;

private:
  std::vector<RequestGroup*> downloads_;
  std::shared_ptr<KadRoutingTable> routingTable_;
  std::string clientHash_;
  uint32_t kadUdpVerifyKey_ = 0;
  UploadQueue* uploadQueue_;
  KadRoutingSnapshot kadSnapshot_;
  bool hasKadSnapshot_ = false;
  std::vector<ServerState> serverStates_;
  std::vector<PersistedFileSources> fileSources_;
  std::string databasePath_;
  std::unique_ptr<Ed2kStore> store_;

  void captureNetworkState(RequestGroup* group);
  void applyPersistedState(RequestGroup* group);
  void load();
  void save();
};

} // namespace ed2k

} // namespace aria2

#endif // D_ED2K_SESSION_H
