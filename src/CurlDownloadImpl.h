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
#include <string>
#include <vector>

#include <curl/curl.h>

#include "TimerA2.h"

namespace aria2 {

class RequestGroup;

struct CurlDownloadImpl {
  std::vector<std::string> uris;
  size_t uriIndex = 0;
  size_t attempts = 0;
  std::string path;
  std::string currentUri;
  std::string etag;
  std::string lastModified;
  FILE* file = nullptr;
  CURL* handle = nullptr;
  curl_slist* headers = nullptr;
  RequestGroup* group = nullptr;
  int64_t resumeOffset = 0;
  int64_t appliedLimit = -1;
  bool dryRun = false;
  bool http = false;
  bool rangeAccepted = false;
  bool headersComplete = false;
  bool resumed = false;
  bool restartAttempted = false;
  bool stopRequested = false;
  Timer lastCheckpoint = Timer::zero();
};

} // namespace aria2

#endif // D_CURL_DOWNLOAD_IMPL_H
