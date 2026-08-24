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

#include <map>
#include <memory>

#include <curl/curl.h>

#include "StreamStore.h"

namespace aria2 {

class Command;
class CurlDownload;
class DownloadEngine;
class Option;
class RequestGroup;

class CurlSession {
public:
  explicit CurlSession(const Option* option);
  ~CurlSession();

  std::unique_ptr<Command> start(const std::shared_ptr<CurlDownload>& download,
                                 RequestGroup* group, DownloadEngine* engine);
  size_t activeCount() const { return downloads_.size(); }
  void setGlobalDownloadLimit(int64_t limit);
  void poll();
  void stop(const std::shared_ptr<CurlDownload>& download, bool retainState);

private:
  CURLM* multi_ = nullptr;
  const Option* option_;
  DownloadEngine* engine_ = nullptr;
  int64_t globalDownloadLimit_ = 0;
  StreamStore store_;
  std::map<CURL*, std::shared_ptr<CurlDownload>> downloads_;

  bool prepare(const std::shared_ptr<CurlDownload>& download,
               RequestGroup* group);
  bool createHandle(const std::shared_ptr<CurlDownload>& download);
  void finish(const std::shared_ptr<CurlDownload>& download, CURLcode result);
  void checkpoint(const std::shared_ptr<CurlDownload>& download, bool force);
  bool restartWithoutResume(const std::shared_ptr<CurlDownload>& download);
  bool retry(const std::shared_ptr<CurlDownload>& download);
  void rebalanceLimits();
  static size_t writeData(char* data, size_t size, size_t count,
                          void* userData);
  static size_t receiveHeader(char* data, size_t size, size_t count,
                              void* userData);
  static int updateProgress(void* userData, curl_off_t downloadTotal,
                            curl_off_t downloaded, curl_off_t uploadTotal,
                            curl_off_t uploaded);
};

} // namespace aria2

#endif // D_CURL_SESSION_H
