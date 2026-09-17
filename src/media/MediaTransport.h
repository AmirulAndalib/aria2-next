/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
#ifndef D_MEDIA_TRANSPORT_H
#define D_MEDIA_TRANSPORT_H
#include "MediaDownload.h"
#include "MediaRequestContext.h"
#include "MediaInput.h"
#include <curl/curl.h>
#include <memory>
#include <string>
#include <map>
#include <set>
#include <stdexcept>

namespace aria2 {
class Option;
namespace media {
struct HttpError : Failure {
  HttpError(long status, CURLcode result, curl_off_t retryAfter);
  long status;
  bool retryable;
  curl_off_t retryAfter;
};
struct Resource {
  std::string path, url, mime;
  int64_t size = 0;
  int64_t utcStart = 0;
  std::map<std::string, std::string> headers;
};
class Transport {
public:
  Transport(const Option* option, std::string source, std::string directory,
            std::shared_ptr<Control> control);
  ~Transport();
  Transport(const Transport&) = delete;
  Transport& operator=(const Transport&) = delete;
  Resource get(const std::string& url, int64_t begin = 0, int64_t end = -1,
               bool cached = true);
  std::string decrypt(const std::string& path, const std::string& keyUrl,
                      const unsigned char* iv, bool cacheKey,
                      const std::string& init = {});
  bool hasCustomKeys() const { return !input_.keys.empty(); }
  static std::string fingerprint(const std::string& value);
  static std::string digest(const std::string& path);
  void retain(const std::string& path);
  void invalidate() { retained_.clear(); }

private:
  const Option* option_;
  std::string source_, directory_;
  std::shared_ptr<Control> control_;
  CURLM* multi_ = nullptr;
  CURLSH* share_ = nullptr;
  std::map<std::string, std::string> retained_;
  std::vector<RequestContext> contexts_;
  InputPlan input_;
  std::set<std::string> suppliedManifests_;
  std::map<std::string, InputKey> verifiedKeys_;
  Resource request(const std::string& url, int64_t begin, int64_t end,
                   const std::string& temporary);
};
} // namespace media
} // namespace aria2
#endif
