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

#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <curl/curl.h>

#include "DiskWriter.h"
#include "RangePlanner.h"
#include "TimerA2.h"

namespace aria2 {

class RequestGroup;
class CurlDownload;

struct CurlHandle {
  CurlDownload* download = nullptr;
  CURL* value = nullptr;
  curl_slist* headers = nullptr;
  RangeLease lease;
  int64_t writeOffset = 0;
  int64_t downloaded = 0;
  int64_t appliedLimit = -1;
  int64_t bufferOffset = 0;
  size_t bufferLimit = 0;
  std::chrono::steady_clock::time_point startedAt;
  std::chrono::steady_clock::time_point lastProgressAt;
  int64_t responseRangeEnd = -1;
  int64_t responseTotalLength = -1;
  int64_t unsatisfiedTotalLength = -1;
  long responseCode = 0;
  bool ranged = false;
  bool rangeAccepted = false;
  bool fullResponseAccepted = false;
  bool headersComplete = false;
  bool primary = false;
  bool validatorMismatch = false;
  bool invalidRange = false;
  std::string responseEtag;
  std::string responseLastModified;
  std::string range;
  std::vector<unsigned char> writeBuffer;
};

struct CurlDownloadImpl {
  std::vector<std::string> uris;
  size_t preferredUriIndex = 0;
  std::string path;
  std::string currentUri;
  std::string etag;
  std::string lastModified;
  std::unique_ptr<DiskWriter> writer;
  std::vector<std::unique_ptr<CurlHandle>> handles;
  RangePlanner planner;
  RequestGroup* group = nullptr;
  int maxConnections = 1;
  int fileNotFoundCount = 0;
  long httpVersion = 0;
  bool dryRun = false;
  bool http = false;
  bool rangeValidated = false;
  bool plannerConfigured = false;
  bool kickPending = false;
  bool stopRequested = false;
  Timer lastCheckpoint = Timer::zero();
};

} // namespace aria2

#endif // D_CURL_DOWNLOAD_IMPL_H
