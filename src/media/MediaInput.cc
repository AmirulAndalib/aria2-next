/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
#include "MediaInput.h"
#include "MediaError.h"
#include "MediaRequestContext.h"
#include "ValueBase.h"
#include "ValueBaseJsonParser.h"
#include <set>
#include <openssl/crypto.h>

namespace aria2::media {
namespace {
[[noreturn]] void invalid()
{
  throw Failure(FailureCode::UnsupportedSource, "Invalid media input plan");
}
std::string field(const Dict* value, const char* key, size_t maximum)
{
  auto text = value ? downcast<String>(value->get(key)) : nullptr;
  if (!text || text->s().size() > maximum ||
      text->s().find('\0') != std::string::npos)
    invalid();
  return text->s();
}

} // namespace
std::array<unsigned char, 16> decodeMediaKey(const std::string& hex)
{
  std::array<unsigned char, 16> value{};
  size_t length = 0;
  if (hex.size() != 32 ||
      !OPENSSL_hexstr2buf_ex(value.data(), value.size(), &length, hex.c_str(),
                             '\0') ||
      length != 16)
    invalid();
  return value;
}
InputPlan parseInputPlan(const std::string& payload)
{
  InputPlan result;
  if (payload.empty())
    return result;
  if (payload.size() > 4 * 1024 * 1024)
    invalid();
  json::ValueBaseJsonParser parser;
  ssize_t consumed = 0;
  auto document = parser.parseFinal(payload.data(), payload.size(), consumed);
  auto object = downcast<Dict>(document);
  if (!object || consumed != static_cast<ssize_t>(payload.size()) ||
      object->size() != 3)
    invalid();
  auto manifests = downcast<List>(object->get("manifests"));
  auto tracks = downcast<List>(object->get("tracks"));
  auto keys = downcast<List>(object->get("keys"));
  if (!manifests || manifests->size() > 32 || !tracks || tracks->size() > 32 ||
      !keys || keys->size() > 64)
    invalid();
  for (const auto& entry : *manifests) {
    auto item = downcast<Dict>(entry);
    auto url = field(item, "url", 16384);
    auto content = field(item, "content", 2 * 1024 * 1024);
    if (item->size() != 2 || !isMediaHttpUrl(url) || content.empty() ||
        !result.manifests.emplace(url, content).second)
      invalid();
  }
  std::set<std::string> ids;
  for (const auto& entry : *tracks) {
    auto item = downcast<Dict>(entry);
    InputTrack track{field(item, "id", 128), field(item, "type", 16), {}};
    auto urls = downcast<List>(item->get("urls"));
    if ((item->size() != 3 && !(item->size() == 4 && item->get("offsetMs"))) ||
        track.id.empty() || !ids.insert(track.id).second || !urls ||
        urls->empty() || urls->size() > 10000 ||
        (track.type != "video" && track.type != "audio" &&
         track.type != "subtitle" && track.type != "muxed"))
      invalid();
    if (item->get("offsetMs")) {
      auto offset = downcast<Integer>(item->get("offsetMs"));
      if (!offset || offset->i() < 0 || offset->i() > 31536000000LL)
        invalid();
      track.offsetMs = offset->i();
    }
    for (const auto& resource : *urls) {
      auto url = downcast<String>(resource);
      if (!url || url->s().size() > 16384 || !isMediaHttpUrl(url->s()))
        invalid();
      track.urls.push_back(url->s());
    }
    result.tracks.push_back(std::move(track));
  }
  for (const auto& entry : *keys) {
    auto item = downcast<Dict>(entry);
    InputKey key{field(item, "url", 16384), field(item, "key", 32),
                 field(item, "iv", 32)};
    if (item->size() != 3 || (!key.url.empty() && !isMediaHttpUrl(key.url)))
      invalid();
    decodeMediaKey(key.key);
    if (!key.iv.empty())
      decodeMediaKey(key.iv);
    result.keys.push_back(std::move(key));
  }
  return result;
}
} // namespace aria2::media
