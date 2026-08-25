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
#ifndef D_ED2K_STORE_H
#define D_ED2K_STORE_H

#include "common.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct sqlite3;

namespace aria2 {

namespace ed2k {

struct PeerCreditState;
struct ServerState;
struct Endpoint;

struct PersistedFileSources {
  std::string fileHash;
  int64_t fileSize = 0;
  std::vector<Endpoint> sources;
};

struct PersistedPieceState {
  size_t index = 0;
  int64_t length = 0;
  std::string bitfield;
};

struct PersistedDownloadState {
  std::string gid;
  std::string fileHash;
  int64_t fileSize = 0;
  std::string link;
  std::string path;
  int64_t modifiedTime = 0;
  int64_t sharingTime = 0;
  bool paused = false;
  bool complete = false;
  std::string bitfield;
  std::vector<PersistedPieceState> pieces;
};

class Ed2kStore {
public:
  explicit Ed2kStore(std::string path);
  ~Ed2kStore();

  Ed2kStore(const Ed2kStore&) = delete;
  Ed2kStore& operator=(const Ed2kStore&) = delete;

  bool open();
  bool available() const { return db_ != nullptr; }
  const std::string& path() const { return path_; }

  bool loadIdentity(std::string& clientHash, std::string& kadState) const;
  bool saveIdentity(const std::string& clientHash, const std::string& kadState);

  bool loadServers(std::vector<ServerState>& servers) const;
  bool replaceServers(const std::vector<ServerState>& servers);

  bool loadCredits(std::vector<PeerCreditState>& credits) const;
  bool replaceCredits(const std::vector<PeerCreditState>& credits);

  bool loadFileSources(std::vector<PersistedFileSources>& files) const;
  bool replaceFileSources(const std::vector<PersistedFileSources>& files);

  bool saveRuntime(const std::string& clientHash, const std::string& kadState,
                   const std::vector<ServerState>& servers,
                   const std::vector<PersistedFileSources>& files,
                   const std::vector<PeerCreditState>& credits);

  bool hasDownload(const std::string& gid) const;
  bool loadDownload(PersistedDownloadState& state,
                    const std::string& gid) const;
  bool loadDownloads(std::vector<PersistedDownloadState>& states) const;
  bool saveDownload(const PersistedDownloadState& state);
  bool removeDownload(const std::string& gid);

private:
  bool initializeSchema();
  bool exec(const char* sql) const;
  bool begin();
  bool commit();
  void rollback();

  std::string path_;
  sqlite3* db_ = nullptr;
};

} // namespace ed2k

} // namespace aria2

#endif // D_ED2K_STORE_H
