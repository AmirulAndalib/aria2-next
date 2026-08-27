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
#ifndef D_CURL_SESSION_H
#define D_CURL_SESSION_H

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <curl/curl.h>

#include "RangePlanner.h"
#include "StreamStore.h"
#include "error_code.h"

namespace aria2 {

class Command;
class CurlDownload;
class DownloadEngine;
class Option;
class RequestGroup;
class CurlSocketCommand;
struct CurlHandle;

class CurlSession {
public:
  explicit CurlSession(const Option* option);
  ~CurlSession();

  std::unique_ptr<Command> start(const std::shared_ptr<CurlDownload>& download,
                                 RequestGroup* group, DownloadEngine* engine);
  size_t activeCount() const { return tasks_.size(); }
  void setGlobalDownloadLimit(int64_t limit);
  void poll();
  void armTimeout();
  void advance(const std::shared_ptr<CurlDownload>& download);
  void stop(const std::shared_ptr<CurlDownload>& download, bool retainState);

private:
  CURLM* multi_ = nullptr;
  CURLSH* share_ = nullptr;
  const Option* option_;
  DownloadEngine* engine_ = nullptr;
  int64_t globalDownloadLimit_ = 0;
  StreamStore store_;
  std::map<CURL*, std::pair<std::shared_ptr<CurlDownload>, CurlHandle*>>
      downloads_;
  std::map<CurlDownload*, std::shared_ptr<CurlDownload>> tasks_;
  std::map<curl_socket_t, CurlSocketCommand*> sockets_;
  std::chrono::steady_clock::time_point timeoutDeadline_;
  bool timeoutArmed_ = false;
  bool shuttingDown_ = false;
  bool curlInitialized_ = false;
  uint64_t loggingRevision_ = 0;

  bool prepare(const std::shared_ptr<CurlDownload>& download,
               RequestGroup* group);
  bool createHandle(const std::shared_ptr<CurlDownload>& download,
                    const RangeLease& lease, bool primary, bool ranged);
  void finish(const std::shared_ptr<CurlDownload>& download, CurlHandle* handle,
              CURLcode result);
  void checkpoint(const std::shared_ptr<CurlDownload>& download, bool force);
  void configurePlanner(const std::shared_ptr<CurlDownload>& download,
                        const RangeLease* retainedLease = nullptr);
  void schedule(const std::shared_ptr<CurlDownload>& download);
  bool rebalanceEndgame(const std::shared_ptr<CurlDownload>& download,
                        int64_t pieceLength);
  bool retryRange(const std::shared_ptr<CurlDownload>& download,
                  const RangeLease& lease, curl_off_t retryAfter);
  std::vector<RangeLease>
  activeLeases(const std::shared_ptr<CurlDownload>& download) const;
  void finalize(const std::shared_ptr<CurlDownload>& download,
                curl_off_t reportedFileTime);
  void failTask(const std::shared_ptr<CurlDownload>& download,
                error_code::Value errorCode, const std::string& message,
                bool retainState = true);
  void cancelHandles(const std::shared_ptr<CurlDownload>& download);
  bool openOutput(const std::shared_ptr<CurlDownload>& download,
                  bool preserveExisting);
  void closeOutput(CurlDownload* download) noexcept;
  static void fail(CurlDownload* download, error_code::Value errorCode,
                   const std::string& message) noexcept;
  static std::string gid(const CurlDownload* download);
  void rebalanceLimits();
  void socketAction(curl_socket_t socket, int events);
  void
  refreshConnectionCount(const std::shared_ptr<CurlDownload>& download) const;
  void processMessages();
  void updateSocket(curl_socket_t socket, int action,
                    CurlSocketCommand* command);
  void removeSocket(curl_socket_t socket, CurlSocketCommand* command);
  void updateTimeout(long timeoutMs);
  static int socketCallback(CURL* easy, curl_socket_t socket, int action,
                            void* userData, void* socketData) noexcept;
  static int timerCallback(CURLM* multi, long timeoutMs,
                           void* userData) noexcept;
  static int socketOptionCallback(void* userData, curl_socket_t socket,
                                  curlsocktype purpose) noexcept;
  static size_t writeData(char* data, size_t size, size_t count,
                          void* userData) noexcept;
  static size_t receiveHeader(char* data, size_t size, size_t count,
                              void* userData) noexcept;
  static int updateProgress(void* userData, curl_off_t downloadTotal,
                            curl_off_t downloaded, curl_off_t uploadTotal,
                            curl_off_t uploaded) noexcept;
  static int debugCallback(CURL* easy, curl_infotype type, char* data,
                           size_t size, void* userData) noexcept;

  friend class CurlSocketCommand;
  friend class CurlSessionTest;
};

} // namespace aria2

#endif // D_CURL_SESSION_H
