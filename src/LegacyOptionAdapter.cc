/* <!-- copyright */
/*
 * aria2 - The high speed download utility
 *
 * Copyright (C) 2026 The aria2-next contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
/* copyright --> */
#include "LegacyOptionAdapter.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string_view>
#include <tuple>

#include "DlAbortEx.h"
#include "Log.h"
#include "Option.h"
#include "error_code.h"
#include "fmt.h"
#include "prefs.h"
#include "util.h"

namespace aria2 {

namespace {

enum class ArgumentKind { Required, OptionalBoolean };

struct LegacyOptionSpec {
  const char* name;
  ArgumentKind argument;
  bool sensitive = false;
};

constexpr std::array<LegacyOptionSpec, 39> LEGACY_OPTIONS{{
    {"bt-detach-seed-only", ArgumentKind::OptionalBoolean},
    {"bt-enable-hook-after-hash-check", ArgumentKind::OptionalBoolean},
    {"bt-force-encryption", ArgumentKind::OptionalBoolean},
    {"bt-hash-check-seed", ArgumentKind::OptionalBoolean},
    {"bt-keep-alive-interval", ArgumentKind::Required},
    {"bt-load-saved-metadata", ArgumentKind::OptionalBoolean},
    {"bt-lpd-interface", ArgumentKind::Required},
    {"bt-metadata-only", ArgumentKind::OptionalBoolean},
    {"bt-min-crypto-level", ArgumentKind::Required},
    {"bt-prioritize-piece", ArgumentKind::Required},
    {"bt-remove-unselected-file", ArgumentKind::OptionalBoolean},
    {"bt-request-peer-speed-limit", ArgumentKind::Required},
    {"bt-request-timeout", ArgumentKind::Required},
    {"bt-require-crypto", ArgumentKind::OptionalBoolean},
    {"bt-save-metadata", ArgumentKind::OptionalBoolean},
    {"bt-stop-timeout", ArgumentKind::Required},
    {"bt-timeout", ArgumentKind::Required},
    {"bt-tracker-connect-timeout", ArgumentKind::Required},
    {"bt-tracker-interval", ArgumentKind::Required},
    {"bt-tracker-timeout", ArgumentKind::Required},
    {"dht-entry-point", ArgumentKind::Required},
    {"dht-entry-point-host", ArgumentKind::Required},
    {"dht-entry-point-host6", ArgumentKind::Required},
    {"dht-entry-point-port", ArgumentKind::Required},
    {"dht-entry-point-port6", ArgumentKind::Required},
    {"dht-entry-point6", ArgumentKind::Required},
    {"dht-file-path", ArgumentKind::Required},
    {"dht-file-path6", ArgumentKind::Required},
    {"dht-listen-addr", ArgumentKind::Required},
    {"dht-listen-addr6", ArgumentKind::Required},
    {"dht-listen-port", ArgumentKind::Required},
    {"dht-message-timeout", ArgumentKind::Required},
    {"enable-async-dns6", ArgumentKind::OptionalBoolean},
    {"enable-dht6", ArgumentKind::OptionalBoolean},
    {"peer-agent", ArgumentKind::Required},
    {"peer-connection-timeout", ArgumentKind::Required},
    {"peer-id-prefix", ArgumentKind::Required},
    {"rpc-passwd", ArgumentKind::Required, true},
    {"rpc-user", ArgumentKind::Required, true},
}};

const LegacyOptionSpec* findLegacyOption(std::string_view name)
{
  const auto result = std::find_if(
      LEGACY_OPTIONS.begin(), LEGACY_OPTIONS.end(),
      [name](const LegacyOptionSpec& spec) { return name == spec.name; });
  return result == LEGACY_OPTIONS.end() ? nullptr : &*result;
}

const char* sourceName(LegacyOptionSource source)
{
  switch (source) {
  case LegacyOptionSource::CommandLine:
    return "command line";
  case LegacyOptionSource::Configuration:
    return "configuration";
  case LegacyOptionSource::Session:
    return "session";
  case LegacyOptionSource::Rpc:
    return "RPC";
  case LegacyOptionSource::Library:
    return "libaria2";
  }
  return "input";
}

void logAdaptation(LegacyOptionSource source, const std::string& name,
                   const std::string& result)
{
  static std::mutex mutex;
  static std::set<std::string> emitted;
  const auto key = std::to_string(static_cast<int>(source)) + ':' + name;
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (!emitted.insert(key).second) {
      return;
    }
  }
  A2_LOG_WARN(fmt("Legacy option input from %s: %s; %s", sourceName(source),
                  name.c_str(), result.c_str()));
}

bool normalizeLegacyValue(const std::string& name, const std::string& value,
                          std::string& normalized)
{
  if ((name == "log-level" || name == "console-log-level") &&
      value == "notice") {
    normalized = V_INFO;
    return true;
  }
  if (name == "bt-encryption" && value == "enabled") {
    normalized = V_PREFERRED;
    return true;
  }
  return false;
}

bool parseBoolean(const std::string& name, const std::string& value)
{
  if (value == A2_V_TRUE) {
    return true;
  }
  if (value == A2_V_FALSE) {
    return false;
  }
  throw DL_ABORT_EX2(
      fmt("Invalid legacy option value: %s=%s", name.c_str(), value.c_str()),
      error_code::OPTION_ERROR);
}

void validateNumber(const std::string& name, const std::string& value,
                    int64_t minimum, int64_t maximum)
{
  int64_t number;
  if (!util::parseLLIntNoThrow(number, value) || number < minimum ||
      (maximum >= 0 && number > maximum)) {
    throw DL_ABORT_EX2(
        fmt("Invalid legacy option value: %s=%s", name.c_str(), value.c_str()),
        error_code::OPTION_ERROR);
  }
}

void validateUnitNumber(const std::string& name, const std::string& value)
{
  try {
    if (util::getRealSize(value) < 0) {
      throw DL_ABORT_EX("Negative size");
    }
  }
  catch (Exception&) {
    throw DL_ABORT_EX2(
        fmt("Invalid legacy option value: %s=%s", name.c_str(), value.c_str()),
        error_code::OPTION_ERROR);
  }
}

void validatePrioritizePiece(const std::string& name, const std::string& value)
{
  try {
    std::vector<size_t> pieces;
    util::parsePrioritizePieceRange(
        pieces, value, std::vector<std::shared_ptr<FileEntry>>(), 1024);
  }
  catch (Exception&) {
    throw DL_ABORT_EX2(
        fmt("Invalid legacy option value: %s=%s", name.c_str(), value.c_str()),
        error_code::OPTION_ERROR);
  }
}

std::string joinValues(const std::vector<std::string>& values)
{
  std::string result;
  std::set<std::string> seen;
  for (const auto& value : values) {
    if (value.empty() || !seen.insert(value).second) {
      continue;
    }
    if (!result.empty()) {
      result += ',';
    }
    result += value;
  }
  return result;
}

} // namespace

bool isLegacyOption(const std::string& name)
{
  return findLegacyOption(name) != nullptr;
}

KeyVals adaptLegacyOptions(const KeyVals& options, LegacyOptionSource source)
{
  KeyVals result;
  std::map<std::string, std::string> legacy;
  std::set<std::string> canonical;
  std::set<std::string> explicitCanonical;
  for (const auto& option : options) {
    std::string normalized;
    if (!isLegacyOption(option.first) &&
        !normalizeLegacyValue(option.first, option.second, normalized)) {
      explicitCanonical.insert(option.first);
    }
  }
  for (const auto& option : options) {
    if (isLegacyOption(option.first)) {
      legacy[option.first] = option.second;
    }
    else {
      std::string normalized;
      if (normalizeLegacyValue(option.first, option.second, normalized)) {
        if (explicitCanonical.count(option.first) != 0) {
          logAdaptation(source, option.first + '=' + option.second,
                        "ignored because a current value for " + option.first +
                            " is already present");
        }
        else {
          result.emplace_back(option.first, normalized);
          logAdaptation(source, option.first + '=' + option.second,
                        "mapped to " + option.first + '=' + normalized);
        }
      }
      else {
        result.push_back(option);
      }
      canonical.insert(option.first);
    }
  }
  if (legacy.empty()) {
    return result;
  }

  for (const auto& spec : LEGACY_OPTIONS) {
    if (spec.argument != ArgumentKind::OptionalBoolean) {
      continue;
    }
    if (const auto item = legacy.find(spec.name); item != legacy.end()) {
      parseBoolean(item->first, item->second);
    }
  }

  for (const auto& spec :
       std::array<std::tuple<const char*, int64_t, int64_t>, 9>{{
           {"bt-keep-alive-interval", 1, 120},
           {"bt-request-timeout", 1, 600},
           {"bt-stop-timeout", 0, -1},
           {"bt-timeout", 1, 600},
           {"bt-tracker-interval", 0, -1},
           {"dht-entry-point-port", 1, UINT16_MAX},
           {"dht-entry-point-port6", 1, UINT16_MAX},
           {"dht-message-timeout", 1, 60},
           {"peer-connection-timeout", 1, 600},
       }}) {
    const auto item = legacy.find(std::get<0>(spec));
    if (item != legacy.end()) {
      validateNumber(item->first, item->second, std::get<1>(spec),
                     std::get<2>(spec));
    }
  }
  if (const auto item = legacy.find("bt-request-peer-speed-limit");
      item != legacy.end()) {
    validateUnitNumber(item->first, item->second);
  }
  if (const auto item = legacy.find("bt-prioritize-piece");
      item != legacy.end()) {
    validatePrioritizePiece(item->first, item->second);
  }

  std::set<std::string> mapped;
  auto add = [&](const std::string& legacyName, const std::string& target,
                 const std::string& value, const std::string& detail) {
    if (canonical.count(target) != 0 || mapped.count(target) != 0) {
      logAdaptation(source, legacyName,
                    "ignored because the canonical option " + target +
                        " is already present");
      return;
    }
    result.emplace_back(target, value);
    mapped.insert(target);
    logAdaptation(source, legacyName,
                  "mapped to " + target + "=" + value + detail);
  };
  auto ignore = [&](const std::string& name, const std::string& reason) {
    logAdaptation(source, name, "accepted without a setting change; " + reason);
  };

  for (const auto* name : {"rpc-user", "rpc-passwd"}) {
    const auto item = legacy.find(name);
    if (item == legacy.end()) {
      continue;
    }
    if (!item->second.empty()) {
      throw DL_ABORT_EX2(
          "Legacy RPC Basic authentication cannot be translated safely; "
          "replace rpc-user and rpc-passwd with rpc-secret",
          error_code::OPTION_ERROR);
    }
    ignore(name, "empty RPC Basic authentication input has no effect");
  }

  if (auto item = legacy.find("bt-detach-seed-only"); item != legacy.end()) {
    add(item->first, "detach-share-only",
        parseBoolean(item->first, item->second) ? A2_V_TRUE : A2_V_FALSE, "");
  }

  if (canonical.count("bt-encryption") == 0) {
    bool hasEncryption = false;
    bool required = false;
    if (auto item = legacy.find("bt-force-encryption"); item != legacy.end()) {
      hasEncryption = true;
      required = parseBoolean(item->first, item->second);
    }
    if (auto item = legacy.find("bt-require-crypto"); item != legacy.end()) {
      hasEncryption = true;
      required = required || parseBoolean(item->first, item->second);
    }
    if (auto item = legacy.find("bt-min-crypto-level"); item != legacy.end()) {
      hasEncryption = true;
      if (item->second == "arc4") {
        required = true;
      }
      else if (item->second != "plain") {
        throw DL_ABORT_EX2(fmt("Invalid legacy option value: %s=%s",
                               item->first.c_str(), item->second.c_str()),
                           error_code::OPTION_ERROR);
      }
    }
    if (hasEncryption) {
      result.emplace_back("bt-encryption", required ? V_REQUIRED : V_PREFERRED);
      mapped.insert("bt-encryption");
      for (const auto* name : {"bt-force-encryption", "bt-require-crypto",
                               "bt-min-crypto-level"}) {
        if (legacy.count(name) != 0) {
          logAdaptation(source, name,
                        std::string("mapped to bt-encryption=") +
                            (required ? V_REQUIRED : V_PREFERRED) +
                            "; RC4-only payload selection is not retained");
        }
      }
    }
  }
  else {
    for (const auto* name :
         {"bt-force-encryption", "bt-require-crypto", "bt-min-crypto-level"}) {
      if (legacy.count(name) != 0) {
        logAdaptation(source, name,
                      "ignored because the canonical option bt-encryption is "
                      "already present");
      }
    }
  }

  if (auto item = legacy.find("bt-lpd-interface"); item != legacy.end()) {
    add(item->first, "bt-interface", item->second,
        "; libtorrent uses one interface set for all BitTorrent traffic");
  }
  std::vector<std::string> legacyInterfaces;
  for (const auto* name : {"dht-listen-addr", "dht-listen-addr6"}) {
    if (auto item = legacy.find(name); item != legacy.end()) {
      legacyInterfaces.push_back(item->second);
    }
  }
  if (!legacyInterfaces.empty() && canonical.count("bt-interface") == 0 &&
      mapped.count("bt-interface") == 0) {
    const auto value = joinValues(legacyInterfaces);
    result.emplace_back("bt-interface", value);
    mapped.insert("bt-interface");
    for (const auto* name : {"dht-listen-addr", "dht-listen-addr6"}) {
      if (legacy.count(name) != 0) {
        logAdaptation(source, name,
                      "mapped to bt-interface=" + value +
                          "; libtorrent uses unified BitTorrent sockets");
      }
    }
  }
  else if (!legacyInterfaces.empty()) {
    for (const auto* name : {"dht-listen-addr", "dht-listen-addr6"}) {
      if (legacy.count(name) != 0) {
        logAdaptation(source, name,
                      "ignored because bt-interface is already selected");
      }
    }
  }

  if (auto item = legacy.find("bt-metadata-only"); item != legacy.end()) {
    add(item->first, "pause-metadata",
        parseBoolean(item->first, item->second) ? A2_V_TRUE : A2_V_FALSE,
        "; metadata-only requests pause the same GID after metadata arrives");
  }
  if (auto item = legacy.find("bt-prioritize-piece"); item != legacy.end()) {
    add(item->first, "bt-first-last-piece-first", A2_V_TRUE,
        "; legacy byte ranges are reduced to file boundary priority");
  }
  if (auto item = legacy.find("bt-tracker-connect-timeout");
      item != legacy.end()) {
    add(item->first, "bt-tracker-completion-timeout", item->second,
        "; libtorrent exposes one total request timeout");
  }
  if (auto item = legacy.find("bt-tracker-timeout"); item != legacy.end()) {
    add(item->first, "bt-tracker-receive-timeout", item->second, "");
  }
  if (auto item = legacy.find("dht-listen-port"); item != legacy.end()) {
    add(item->first, "listen-port", item->second,
        "; libtorrent shares the BitTorrent and DHT port");
  }
  if (auto item = legacy.find("enable-dht6"); item != legacy.end()) {
    if (parseBoolean(item->first, item->second)) {
      add(item->first, "enable-dht", A2_V_TRUE,
          "; libtorrent manages IPv4 and IPv6 DHT together");
    }
    else {
      ignore(item->first, "IPv6 DHT is controlled by the unified enable-dht "
                          "and disable-ipv6 settings");
    }
  }
  if (auto item = legacy.find("enable-async-dns6"); item != legacy.end()) {
    ignore(item->first,
           "asynchronous DNS already follows the configured address families");
  }

  if (canonical.count("bt-dht-bootstrap-nodes") == 0) {
    std::vector<std::string> bootstrap;
    for (const auto* name : {"dht-entry-point", "dht-entry-point6"}) {
      if (auto item = legacy.find(name); item != legacy.end()) {
        bootstrap.push_back(item->second);
      }
    }
    if (!bootstrap.empty()) {
      const auto value = joinValues(bootstrap);
      result.emplace_back("bt-dht-bootstrap-nodes", value);
      mapped.insert("bt-dht-bootstrap-nodes");
      for (const auto* name : {"dht-entry-point", "dht-entry-point6"}) {
        if (legacy.count(name) != 0) {
          logAdaptation(source, name,
                        "mapped to bt-dht-bootstrap-nodes=" + value);
        }
      }
    }
  }
  else {
    for (const auto* name : {"dht-entry-point", "dht-entry-point6"}) {
      if (legacy.count(name) != 0) {
        logAdaptation(source, name,
                      "ignored because the canonical option "
                      "bt-dht-bootstrap-nodes is already present");
      }
    }
  }

  static constexpr std::array<const char*, 12> NATIVE_OWNED{{
      "bt-keep-alive-interval",
      "bt-load-saved-metadata",
      "bt-request-peer-speed-limit",
      "bt-request-timeout",
      "bt-timeout",
      "bt-tracker-interval",
      "dht-file-path",
      "dht-file-path6",
      "dht-message-timeout",
      "peer-agent",
      "peer-connection-timeout",
      "peer-id-prefix",
  }};
  for (const auto* name : NATIVE_OWNED) {
    if (legacy.count(name) != 0) {
      ignore(name, "the native libtorrent policy owns this behavior");
    }
  }
  for (const auto* name :
       {"bt-enable-hook-after-hash-check", "bt-hash-check-seed",
        "bt-remove-unselected-file", "bt-save-metadata", "bt-stop-timeout"}) {
    if (legacy.count(name) != 0) {
      ignore(name, "the maintained engine has no equivalent setting");
    }
  }
  for (const auto* name : {"dht-entry-point-host", "dht-entry-point-host6",
                           "dht-entry-point-port", "dht-entry-point-port6"}) {
    if (legacy.count(name) != 0) {
      ignore(name,
             "this was an internal component of the DHT entry-point option");
    }
  }

  return result;
}

std::vector<std::string> adaptLegacyCommandLine(int argc, char* argv[],
                                                LegacyOptionSource source)
{
  KeyVals legacy;
  std::vector<std::string> passthrough;
  passthrough.reserve(static_cast<size_t>(argc));
  passthrough.emplace_back(argc > 0 ? argv[0] : "aria2-next");

  bool endOfOptions = false;
  for (int index = 1; index < argc; ++index) {
    std::string argument(argv[index]);
    if (endOfOptions || argument == "--") {
      endOfOptions = true;
      passthrough.push_back(std::move(argument));
      continue;
    }
    if (argument.size() < 3 || argument[0] != '-' || argument[1] != '-') {
      passthrough.push_back(std::move(argument));
      continue;
    }
    const auto equals = argument.find('=');
    const auto name = argument.substr(
        2, equals == std::string::npos ? std::string::npos : equals - 2);
    const auto* spec = findLegacyOption(name);
    if (!spec) {
      std::string value;
      if (equals != std::string::npos) {
        value = argument.substr(equals + 1);
      }
      else if (index + 1 < argc) {
        value = argv[index + 1];
      }
      std::string normalized;
      if (value.empty() || !normalizeLegacyValue(name, value, normalized)) {
        passthrough.push_back(std::move(argument));
        continue;
      }
      if (equals == std::string::npos) {
        ++index;
      }
      legacy.emplace_back(name, std::move(value));
      continue;
    }

    std::string value;
    char* sensitiveValue = nullptr;
    if (equals != std::string::npos) {
      value = argument.substr(equals + 1);
      sensitiveValue = argv[index] + equals + 1;
    }
    else if (spec->argument == ArgumentKind::OptionalBoolean) {
      value = A2_V_TRUE;
    }
    else if (index + 1 < argc) {
      value = argv[++index];
      sensitiveValue = argv[index];
    }
    else {
      throw DL_ABORT_EX2("Missing argument for legacy option --" + name,
                         error_code::OPTION_ERROR);
    }
    if (spec->sensitive && sensitiveValue) {
      std::fill(sensitiveValue, sensitiveValue + strlen(sensitiveValue), '*');
    }
    legacy.emplace_back(name, std::move(value));
  }

  if (legacy.empty()) {
    return {};
  }
  const auto mapped = adaptLegacyOptions(legacy, source);
  std::vector<std::string> result;
  result.reserve(passthrough.size() + mapped.size());
  result.push_back(passthrough.front());
  for (const auto& option : mapped) {
    result.push_back("--" + option.first + '=' + option.second);
  }
  result.insert(result.end(), std::next(passthrough.begin()),
                passthrough.end());
  return result;
}

bool projectLegacyOption(const Option* option, const std::string& name,
                         std::string& value)
{
  if (!option || !isLegacyOption(name)) {
    return false;
  }
  const auto encryption = option->get(PREF_BT_ENCRYPTION);
  if (name == "bt-detach-seed-only") {
    value = option->get(PREF_DETACH_SHARE_ONLY);
  }
  else if (name == "bt-force-encryption") {
    value = encryption == V_REQUIRED ? A2_V_TRUE : A2_V_FALSE;
  }
  else if (name == "bt-require-crypto") {
    value = encryption == V_REQUIRED ? A2_V_TRUE : A2_V_FALSE;
  }
  else if (name == "bt-min-crypto-level") {
    value = "plain";
  }
  else if (name == "bt-lpd-interface" || name == "dht-listen-addr" ||
           name == "dht-listen-addr6") {
    value = option->get(PREF_BT_INTERFACE);
  }
  else if (name == "bt-metadata-only") {
    value = option->get(PREF_PAUSE_METADATA);
  }
  else if (name == "bt-prioritize-piece") {
    value =
        option->getAsBool(PREF_BT_FIRST_LAST_PIECE_FIRST) ? "head,tail" : "";
  }
  else if (name == "bt-tracker-connect-timeout") {
    value = option->get(PREF_BT_TRACKER_COMPLETION_TIMEOUT);
  }
  else if (name == "bt-tracker-timeout") {
    value = option->get(PREF_BT_TRACKER_RECEIVE_TIMEOUT);
  }
  else if (name == "dht-entry-point" || name == "dht-entry-point6") {
    value = option->get(PREF_BT_DHT_BOOTSTRAP_NODES);
  }
  else if (name == "dht-listen-port") {
    value = option->get(PREF_LISTEN_PORT);
  }
  else if (name == "enable-dht6") {
    value = option->getAsBool(PREF_ENABLE_DHT) &&
                    !option->getAsBool(PREF_DISABLE_IPV6)
                ? A2_V_TRUE
                : A2_V_FALSE;
  }
  else {
    return false;
  }
  return true;
}

KeyVals projectLegacyOptions(const Option* option)
{
  KeyVals result;
  for (const auto& spec : LEGACY_OPTIONS) {
    std::string value;
    if (projectLegacyOption(option, spec.name, value)) {
      result.emplace_back(spec.name, std::move(value));
    }
  }
  return result;
}

} // namespace aria2
