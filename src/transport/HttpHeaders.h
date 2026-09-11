/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
#ifndef ARIA2_TRANSPORT_HTTP_HEADERS_H
#define ARIA2_TRANSPORT_HTTP_HEADERS_H

#include "common.h"

#include <curl/curl.h>
#include <cstdint>
#include <string>

namespace aria2::http {

std::string trimHeader(std::string value);
bool startsWithHeader(const std::string& line, const char* name);

// Copies the final response field; libcurl owns framing and redirect
// separation.
std::string responseHeader(CURL* easy, const char* name);

// Converts the inclusive HTTP wire range into the half-open interval
// [first,end).
bool parseContentRange(const std::string& value, int64_t& first, int64_t& end,
                       int64_t& total);
bool parseUnsatisfiedContentRange(const std::string& value, int64_t& total);
bool parseContentLength(const std::string& value, int64_t& length);
std::string normalizeStrongEtag(const std::string& value);

// Media requests use an inclusive end; a negative end requests the entire tail.
bool matchesRange(CURL* handle, int64_t begin, int64_t end, int64_t length);

} // namespace aria2::http
#endif
