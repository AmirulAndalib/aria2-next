/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
#include "MediaRequestContext.h"
#include "MediaError.h"
#include "ValueBase.h"
#include "ValueBaseJsonParser.h"
#include "transport/CurlOptions.h"
#include <algorithm>
#include <curl/urlapi.h>
#include <memory>
#include <string_view>

namespace aria2::media {
namespace {
[[noreturn]] void invalid()
{
  throw Failure(FailureCode::UnsupportedSource,
                "Invalid media-request-contexts");
}

std::string stringField(const Dict* object, const char* name, size_t maximum)
{
  const auto* field = object ? downcast<String>(object->get(name)) : nullptr;
  if (!field || field->s().size() > maximum)
    invalid();
  return field->s();
}

bool validUrl(const std::string& value)
{
  std::unique_ptr<CURLU, decltype(&curl_url_cleanup)> url(curl_url(),
                                                          curl_url_cleanup);
  if (!url || value.find('\0') != std::string::npos ||
      value.find_first_of("\r\n") != std::string::npos ||
      curl_url_set(url.get(), CURLUPART_URL, value.c_str(),
                   CURLU_DISALLOW_USER) != CURLUE_OK)
    return false;
  char* scheme = nullptr;
  if (curl_url_get(url.get(), CURLUPART_SCHEME, &scheme, 0) != CURLUE_OK)
    return false;
  const bool valid =
      std::string_view(scheme) == "http" || std::string_view(scheme) == "https";
  curl_free(scheme);
  return valid;
}

bool validHeader(const std::string& name, const std::string& value)
{
  constexpr std::string_view token =
      "!#$%&'*+-.^_`|~0123456789abcdefghijklmnopqrstuvwxyz";
  if (name.empty() || name.find_first_not_of(token) != std::string::npos ||
      std::any_of(value.begin(), value.end(), [](unsigned char c) {
        return (c < 32 && c != '\t') || c == 127;
      }))
    return false;
  for (const auto* owned :
       {"host", "connection", "content-length", "transfer-encoding", "range",
        "accept-encoding", "keep-alive", "te", "trailer", "upgrade"})
    if (name == owned)
      return false;
  return name.rfind("proxy-", 0) != 0 && name.rfind("if-", 0) != 0;
}
} // namespace

std::vector<RequestContext> parseRequestContexts(const std::string& payload)
{
  if (payload.empty())
    return {};
  if (payload.size() > 256 * 1024)
    invalid();
  json::ValueBaseJsonParser parser;
  ssize_t consumed = 0;
  const auto document =
      parser.parseFinal(payload.data(), payload.size(), consumed);
  const auto* list = downcast<List>(document);
  if (!list || consumed != static_cast<ssize_t>(payload.size()) ||
      list->size() > 8)
    invalid();
  std::vector<RequestContext> contexts;
  for (const auto& entry : *list) {
    const auto* object = downcast<Dict>(entry);
    RequestContext context;
    context.url = stringField(object, "url", 16384);
    const auto* headers =
        object ? downcast<List>(object->get("headers")) : nullptr;
    if (!validUrl(context.url) || !headers || object->size() != 2 ||
        headers->size() > 32 ||
        std::any_of(contexts.begin(), contexts.end(),
                    [&](const auto& previous) {
                      return http::sameOrigin(previous.url, context.url);
                    }))
      invalid();
    size_t bytes = 0;
    for (const auto& entry : *headers) {
      const auto* field = downcast<Dict>(entry);
      auto name = stringField(field, "name", 128);
      auto value = stringField(field, "value", 8192);
      std::transform(name.begin(), name.end(), name.begin(),
                     [](unsigned char c) {
                       return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
                     });
      bytes += name.size() + value.size();
      if (field->size() != 2 || bytes > 16384 || !validHeader(name, value) ||
          !context.headers.emplace(name, value).second)
        invalid();
    }
    contexts.push_back(std::move(context));
  }
  return contexts;
}
} // namespace aria2::media
