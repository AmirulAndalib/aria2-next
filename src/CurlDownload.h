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
#ifndef D_CURL_DOWNLOAD_H
#define D_CURL_DOWNLOAD_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "error_code.h"

namespace aria2 {

class RequestGroup;
class CurlSession;
class CurlDownloadCommand;
struct CurlDownloadImpl;

struct CurlSnapshot {
  enum class State { Waiting, Active, Paused, Complete, Error, Stopped };

  State state = State::Waiting;
  int64_t totalLength = 0;
  int64_t completedLength = 0;
  int64_t sessionDownloadLength = 0;
  int connections = 0;
  error_code::Value errorCode = error_code::UNDEFINED;
  std::string currentUri;
  std::string error;
};

class CurlDownload {
public:
  explicit CurlDownload(std::vector<std::string> uris);
  ~CurlDownload();

  const CurlSnapshot& snapshot() const { return snapshot_; }
  bool failed() const { return snapshot_.state == CurlSnapshot::State::Error; }
  bool stopped() const
  {
    return snapshot_.state == CurlSnapshot::State::Paused ||
           snapshot_.state == CurlSnapshot::State::Complete ||
           snapshot_.state == CurlSnapshot::State::Error ||
           snapshot_.state == CurlSnapshot::State::Stopped;
  }
  void synchronizeUris(const std::vector<std::string>& uris);

private:
  friend class CurlSession;
  friend class CurlDownloadCommand;
  friend class CurlSessionTest;

  std::unique_ptr<CurlDownloadImpl> impl_;
  CurlSnapshot snapshot_;
};

} // namespace aria2

#endif // D_CURL_DOWNLOAD_H
