/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
// Keep the Windows socket ABI consistent before native library headers.
#include "common.h" // IWYU pragma: keep

#include "HttpHeaders.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <curl/curl.h>
#include <curl/header.h>

#include "support/Numbers.h"

namespace aria2::http {

std::string trimHeader(std::string value)
{
  while (!value.empty() && (value.back() == '\r' || value.back() == '\n' ||
                            value.back() == ' ' || value.back() == '\t')) {
    value.pop_back();
  }
  const auto first = value.find_first_not_of(" \t");
  return first == std::string::npos ? std::string() : value.substr(first);
}

bool startsWithHeader(const std::string& line, const char* name)
{
  const auto length = std::strlen(name);
  return line.size() >= length &&
         std::equal(line.begin(), line.begin() + length, name,
                    [](char lhs, char rhs) {
                      return std::tolower(static_cast<unsigned char>(lhs)) ==
                             std::tolower(static_cast<unsigned char>(rhs));
                    });
}

std::string responseHeader(CURL* easy, const char* name)
{
  curl_header* header = nullptr;
  if (curl_easy_header(easy, name, 0, CURLH_HEADER, -1, &header) != CURLHE_OK) {
    return {};
  }
  // Preserve the final field value while letting libcurl handle header
  // framing, folding and separation from CONNECT, 1xx and trailer fields.
  const auto last = header->amount - 1;
  if (last && curl_easy_header(easy, name, last, CURLH_HEADER, -1, &header) !=
                  CURLHE_OK) {
    return {};
  }
  return header->value;
}

bool parseContentRange(const std::string& value, int64_t& first, int64_t& end,
                       int64_t& total)
{
  long long parsedFirst = -1;
  long long parsedLast = -1;
  long long parsedTotal = -1;
  int consumed = 0;
  if (std::sscanf(value.c_str(), "bytes %lld-%lld/%lld%n", &parsedFirst,
                  &parsedLast, &parsedTotal, &consumed) != 3 ||
      consumed != static_cast<int>(value.size()) || parsedFirst < 0 ||
      parsedLast < parsedFirst || parsedTotal <= parsedLast) {
    return false;
  }
  first = parsedFirst;
  end = parsedLast + 1;
  total = parsedTotal;
  return true;
}

bool parseUnsatisfiedContentRange(const std::string& value, int64_t& total)
{
  auto range = value;
  if (range.size() >= 6 &&
      std::equal(range.begin(), range.begin() + 5, "bytes",
                 [](char lhs, char rhs) {
                   return std::tolower(static_cast<unsigned char>(lhs)) ==
                          std::tolower(static_cast<unsigned char>(rhs));
                 }) &&
      std::isspace(static_cast<unsigned char>(range[5]))) {
    range = trimHeader(range.substr(6));
  }
  long long parsedTotal = -1;
  int consumed = 0;
  if (std::sscanf(range.c_str(), "*/%lld%n", &parsedTotal, &consumed) != 1 ||
      consumed != static_cast<int>(range.size()) || parsedTotal < 0) {
    return false;
  }
  total = parsedTotal;
  return true;
}

bool parseContentLength(const std::string& value, int64_t& length)
{
  return util::parseLLIntNoThrow(length, value) && length >= 0;
}

std::string normalizeStrongEtag(const std::string& value)
{
  if (value.empty() || value.compare(0, 2, "W/") == 0) {
    return {};
  }
  const bool quoted =
      value.size() >= 2 && value.front() == '"' && value.back() == '"';
  if (!std::all_of(value.begin() + (quoted ? 1 : 0),
                   value.end() - (quoted ? 1 : 0), [](unsigned char c) {
                     return c == 0x21 || (c >= 0x23 && c <= 0x7e) || c >= 0x80;
                   })) {
    return {};
  }
  // Some origins omit the quotes around an otherwise valid opaque tag.
  // Normalize both responses and persisted identity, including If-Range.
  return quoted ? value : '"' + value + '"';
}

bool matchesRange(CURL* handle, int64_t begin, int64_t end, int64_t length)
{
  int64_t first = 0, limit = 0, total = 0;
  return parseContentRange(responseHeader(handle, "Content-Range"), first,
                           limit, total) &&
         first == begin && (end < 0 ? limit == total : limit - 1 == end) &&
         limit - first == length;
}

} // namespace aria2::http
