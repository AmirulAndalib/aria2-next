/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
#ifndef ARIA2_TRANSPORT_CURL_MULTI_H
#define ARIA2_TRANSPORT_CURL_MULTI_H

#include "common.h"

#include <curl/curl.h>
#include <chrono>
#include <functional>
#include <map>

namespace aria2 {
class DownloadEngine;
class CurlSocketCommand;

// Owns libcurl and its event-loop registrations on the engine thread. The
// owner drains completion messages after socket activity; no transfer policy
// or download state is retained here. Easy handles must be detached before
// destruction. Native callbacks never propagate exceptions through libcurl.
class CurlMulti {
public:
  explicit CurlMulti(std::function<void()> onActivity);
  ~CurlMulti();
  CurlMulti(const CurlMulti&) = delete;
  CurlMulti& operator=(const CurlMulti&) = delete;

  CURLM* get() const { return multi_; }
  CURLSH* share() const { return share_; }
  size_t socketCount() const { return sockets_.size(); }
  void bind(DownloadEngine* engine) { engine_ = engine; }
  void poll();
  void armTimeout();
  void socketAction(curl_socket_t socket, int events);
  void updateTimeout(long timeoutMs);

  // Removes event subscriptions before the owner detaches outstanding handles.
  void shutdown();
  void disable();

private:
  CURLM* multi_ = nullptr;
  CURLSH* share_ = nullptr;
  DownloadEngine* engine_ = nullptr;
  std::function<void()> onActivity_;
  std::map<curl_socket_t, CurlSocketCommand*> sockets_;
  std::chrono::steady_clock::time_point timeoutDeadline_;
  bool timeoutArmed_ = false;
  bool shuttingDown_ = false;
  bool curlInitialized_ = false;

  void updateSocket(curl_socket_t socket, int action,
                    CurlSocketCommand* command);
  void removeSocket(curl_socket_t socket, CurlSocketCommand* command);
  static int socketCallback(CURL*, curl_socket_t, int, void*, void*) noexcept;
  static int timerCallback(CURLM*, long, void*) noexcept;
};

} // namespace aria2
#endif
