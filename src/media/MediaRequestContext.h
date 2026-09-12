/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
#ifndef D_MEDIA_REQUEST_CONTEXT_H
#define D_MEDIA_REQUEST_CONTEXT_H

#include <map>
#include <string>
#include <vector>

namespace aria2::media {

struct RequestContext {
  std::string url;
  std::map<std::string, std::string> headers;
};

// Contexts are immutable task input. Every HTTP hop selects its own origin.
std::vector<RequestContext> parseRequestContexts(const std::string& payload);

} // namespace aria2::media
#endif
