/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
// Keep the Windows socket ABI consistent before native library headers.
#include "common.h" // IWYU pragma: keep

#include "CurlMulti.h"

#include <algorithm>
#include <curl/curl.h>
#include <curl/multi.h>
#include <chrono>
#include <exception>
#include <memory>
#include <functional>
#include <iterator>
#include <string>
#include <utility>

#include "Command.h"
#include "DownloadEngine.h"
#include "DlAbortEx.h"
#include "Log.h"
#include "fmt.h"
#include "spdlog/common.h"

namespace aria2 {

class CurlSocketCommand final : public Command {
public:
  CurlSocketCommand(cuid_t cuid, curl_socket_t socket, CurlMulti* session,
                    DownloadEngine* engine)
      : Command(cuid), socket_(socket), session_(session), engine_(engine)
  {
  }

  bool execute() override
  {
    if (removed_) {
      return true;
    }
    int events = 0;
    if (readEventEnabled()) {
      events |= CURL_CSELECT_IN;
    }
    if (writeEventEnabled()) {
      events |= CURL_CSELECT_OUT;
    }
    if (errorEventEnabled() || hupEventEnabled()) {
      events |= CURL_CSELECT_ERR;
    }
    session_->socketAction(socket_, events);
    if (removed_) {
      return true;
    }
    setStatusInactive();
    engine_->addCommand(std::unique_ptr<Command>(this));
    return false;
  }

  void update(int action)
  {
    const bool nextRead = action == CURL_POLL_IN || action == CURL_POLL_INOUT;
    const bool nextWrite = action == CURL_POLL_OUT || action == CURL_POLL_INOUT;
    if (read_ && !nextRead) {
      engine_->deleteSocketForReadCheck(socket_, this);
    }
    if (write_ && !nextWrite) {
      engine_->deleteSocketForWriteCheck(socket_, this);
    }
    if (!read_ && nextRead) {
      engine_->addSocketForReadCheck(socket_, this);
    }
    if (!write_ && nextWrite) {
      engine_->addSocketForWriteCheck(socket_, this);
    }
    read_ = nextRead;
    write_ = nextWrite;
  }

  void remove()
  {
    if (removed_) {
      return;
    }
    if (read_) {
      engine_->deleteSocketForReadCheck(socket_, this);
    }
    if (write_) {
      engine_->deleteSocketForWriteCheck(socket_, this);
    }
    read_ = false;
    write_ = false;
    removed_ = true;
  }

private:
  curl_socket_t socket_;
  CurlMulti* session_;
  DownloadEngine* engine_;
  bool read_ = false;
  bool write_ = false;
  bool removed_ = false;
};

CurlMulti::CurlMulti(std::function<void()> onActivity)
    : onActivity_(std::move(onActivity))
{
  const auto globalResult = curl_global_init(CURL_GLOBAL_DEFAULT);
  if (globalResult != CURLE_OK) {
    A2_LOG_ERROR(fmt("component=stream event=session_init_failed curl=%d "
                     "message=%s",
                     static_cast<int>(globalResult),
                     curl_easy_strerror(globalResult)));
    return;
  }
  curlInitialized_ = true;
  multi_ = curl_multi_init();
  if (!multi_) {
    A2_LOG_ERROR("component=stream event=session_init_failed "
                 "message=curl_multi_init returned null");
    return;
  }
  share_ = curl_share_init();
  if (share_) {
    const auto cookie =
        curl_share_setopt(share_, CURLSHOPT_SHARE, CURL_LOCK_DATA_COOKIE);
    const auto dns =
        curl_share_setopt(share_, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
    const auto tls =
        curl_share_setopt(share_, CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION);
    if (cookie != CURLSHE_OK || dns != CURLSHE_OK || tls != CURLSHE_OK) {
      A2_LOG_WARN(fmt("component=stream event=share_disabled cookie=%d dns=%d "
                      "tls=%d",
                      static_cast<int>(cookie), static_cast<int>(dns),
                      static_cast<int>(tls)));
      curl_share_cleanup(share_);
      share_ = nullptr;
    }
  }
  const CURLMcode options[] = {
      curl_multi_setopt(multi_, CURLMOPT_SOCKETFUNCTION, socketCallback),
      curl_multi_setopt(multi_, CURLMOPT_SOCKETDATA, this),
      curl_multi_setopt(multi_, CURLMOPT_TIMERFUNCTION, timerCallback),
      curl_multi_setopt(multi_, CURLMOPT_TIMERDATA, this)};
  const auto failed =
      std::find_if(std::begin(options), std::end(options),
                   [](CURLMcode value) { return value != CURLM_OK; });
  if (failed != std::end(options)) {
    A2_LOG_ERROR(fmt("component=stream event=session_init_failed curlm=%d "
                     "message=%s",
                     static_cast<int>(*failed), curl_multi_strerror(*failed)));
    curl_multi_cleanup(multi_);
    multi_ = nullptr;
    return;
  }
}

CurlMulti::~CurlMulti()
{
  shutdown();
  if (multi_)
    curl_multi_cleanup(multi_);
  if (share_)
    curl_share_cleanup(share_);
  if (curlInitialized_)
    curl_global_cleanup();
}

void CurlMulti::shutdown()
{
  shuttingDown_ = true;
  for (auto& entry : sockets_)
    entry.second->remove();
  sockets_.clear();
}

void CurlMulti::poll()
{
  if (timeoutArmed_ && std::chrono::steady_clock::now() >= timeoutDeadline_) {
    timeoutArmed_ = false;
    socketAction(CURL_SOCKET_TIMEOUT, 0);
  }
}

void CurlMulti::armTimeout()
{
  if (!timeoutArmed_ || !engine_) {
    return;
  }
  const auto now = std::chrono::steady_clock::now();
  auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
      timeoutDeadline_ - now);
  if (remaining.count() <= 0) {
    engine_->setNoWait(true);
    remaining = std::chrono::milliseconds(0);
  }
  engine_->setRefreshInterval(remaining);
}

void CurlMulti::socketAction(curl_socket_t socket, int events)
{
  if (!multi_) {
    return;
  }
  int running = 0;
  const auto result =
      curl_multi_socket_action(multi_, socket, events, &running);
  if (result != CURLM_OK && result != CURLM_BAD_SOCKET) {
    A2_LOG_ERROR(fmt("libcurl multi socket action failed: %s",
                     curl_multi_strerror(result)));
  }
  onActivity_();
}

void CurlMulti::updateSocket(curl_socket_t socket, int action,
                             CurlSocketCommand* command)
{
  if (!engine_ || shuttingDown_) {
    return;
  }
  if (!command) {
    auto next = std::unique_ptr<CurlSocketCommand>(
        new CurlSocketCommand(engine_->newCUID(), socket, this, engine_));
    command = next.get();
    sockets_[socket] = command;
    const auto result = curl_multi_assign(multi_, socket, command);
    if (result != CURLM_OK) {
      throw DL_ABORT_EX(std::string("Unable to assign libcurl socket: ") +
                        curl_multi_strerror(result));
    }
    command->update(action);
    engine_->addCommand(std::move(next));
    return;
  }
  command->update(action);
}

void CurlMulti::removeSocket(curl_socket_t socket, CurlSocketCommand* command)
{
  if (command) {
    command->remove();
  }
  sockets_.erase(socket);
  if (multi_) {
    const auto result = curl_multi_assign(multi_, socket, nullptr);
    if (result != CURLM_OK) {
      A2_LOG_WARN(fmt("component=stream event=socket_unassign_failed curlm=%d "
                      "message=%s",
                      static_cast<int>(result), curl_multi_strerror(result)));
    }
  }
}

void CurlMulti::updateTimeout(long timeoutMs)
{
  if (timeoutMs < 0) {
    timeoutArmed_ = false;
    return;
  }
  timeoutArmed_ = true;
  timeoutDeadline_ =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
  armTimeout();
}

int CurlMulti::socketCallback(CURL*, curl_socket_t socket, int action,
                              void* userData, void* socketData) noexcept
{
  try {
    auto* session = static_cast<CurlMulti*>(userData);
    auto* command = static_cast<CurlSocketCommand*>(socketData);
    if (action == CURL_POLL_REMOVE) {
      session->removeSocket(socket, command);
    }
    else {
      session->updateSocket(socket, action, command);
    }
    return 0;
  }
  catch (const std::exception& error) {
    try {
      logging::tryWrite(
          spdlog::level::err, __FILE__, __LINE__,
          fmt("component=stream event=socket_callback_failed message=%s",
              logging::sanitizeText(error.what()).c_str()));
    }
    catch (...) {
    }
  }
  catch (...) {
    logging::tryWrite(spdlog::level::err, __FILE__, __LINE__,
                      "component=stream event=socket_callback_failed");
  }
  return -1;
}

int CurlMulti::timerCallback(CURLM*, long timeoutMs, void* userData) noexcept
{
  try {
    static_cast<CurlMulti*>(userData)->updateTimeout(timeoutMs);
    return 0;
  }
  catch (const std::exception& error) {
    try {
      logging::tryWrite(
          spdlog::level::err, __FILE__, __LINE__,
          fmt("component=stream event=timer_callback_failed message=%s",
              logging::sanitizeText(error.what()).c_str()));
    }
    catch (...) {
    }
  }
  catch (...) {
    logging::tryWrite(spdlog::level::err, __FILE__, __LINE__,
                      "component=stream event=timer_callback_failed");
  }
  return -1;
}

void CurlMulti::disable()
{
  if (multi_) {
    curl_multi_cleanup(multi_);
    multi_ = nullptr;
  }
}

} // namespace aria2
