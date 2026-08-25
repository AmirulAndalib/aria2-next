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
#ifndef D_CURL_DOWNLOAD_IMPL_H
#define D_CURL_DOWNLOAD_IMPL_H

#include <cstdio>
#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <curl/curl.h>

#include "TimerA2.h"

namespace aria2 {

class RequestGroup;
class CurlDownload;

struct CurlHandle {
  CurlDownload* download = nullptr;
  CURL* value = nullptr;
  curl_slist* headers = nullptr;
  int64_t rangeStart = 0;
  int64_t rangeEnd = -1;
  int64_t writeOffset = 0;
  int64_t downloaded = 0;
  int64_t appliedLimit = -1;
  long responseCode = 0;
  bool ranged = false;
  bool rangeAccepted = false;
  bool headersComplete = false;
  bool primary = false;
  bool validatorMismatch = false;
  std::string range;
};

struct CurlDownloadImpl {
  std::vector<std::string> uris;
  size_t uriIndex = 0;
  size_t attempts = 0;
  std::string path;
  std::string currentUri;
  std::string etag;
  std::string lastModified;
  FILE* file = nullptr;
  std::vector<std::unique_ptr<CurlHandle>> handles;
  std::vector<std::pair<int64_t, int64_t>> completedRanges;
  RequestGroup* group = nullptr;
  int64_t resumeOffset = 0;
  int64_t nextRangeOffset = 0;
  int64_t rangeChunkSize = 0;
  int maxConnections = 1;
  int fileNotFoundCount = 0;
  int64_t pendingRetryAfter = 0;
  bool dryRun = false;
  bool http = false;
  bool resumed = false;
  bool scheduleRanges = false;
  bool segmented = false;
  bool restartAttempted = false;
  bool retryPending = false;
  bool stopRequested = false;
  std::chrono::steady_clock::time_point retryDeadline;
  Timer lastCheckpoint = Timer::zero();
};

} // namespace aria2

#endif // D_CURL_DOWNLOAD_IMPL_H
