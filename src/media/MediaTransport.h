/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
#ifndef D_MEDIA_TRANSPORT_H
#define D_MEDIA_TRANSPORT_H
#include "MediaDownload.h"
#include <curl/curl.h>
#include <memory>
#include <string>
#include <map>

namespace aria2 {
class Option;
namespace media {
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
                      const unsigned char* iv);
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
};
} // namespace media
} // namespace aria2
#endif
