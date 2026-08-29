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
#include "BtStateStore.h"

#include "ApplicationStatePath.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <sstream>

#include "BufferedFile.h"
#include "DlAbortEx.h"
#include "File.h"
#include "Log.h"
#include "MessageDigest.h"
#include "fmt.h"
#include "message_digest_helper.h"
#include "util.h"

namespace aria2 {

namespace {

bool isHex(const std::string& value, size_t length)
{
  if (value.size() != length) {
    return false;
  }
  return std::all_of(value.begin(), value.end(), [](unsigned char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
  });
}

bool isManagedName(const std::string& name)
{
  constexpr size_t TORRENT_SUFFIX_LENGTH = 8;
  constexpr size_t RESUME_SUFFIX_LENGTH = 11;
  if (util::endsWith(name, ".torrent")) {
    return name.size() == 40 + TORRENT_SUFFIX_LENGTH &&
           isHex(name.substr(0, 40), 40);
  }
  if (util::endsWith(name, ".fastresume")) {
    const auto identity = name.substr(0, name.size() - RESUME_SUFFIX_LENGTH);
    return isHex(identity, 40) || isHex(identity, 64);
  }
  return name.compare(0, 12, ".aria2-next-") == 0 &&
         util::endsWith(name, ".tmp");
}

std::string normalizedPath(const std::filesystem::path& path)
{
  std::error_code error;
  auto normalized = std::filesystem::weakly_canonical(path, error);
  if (error) {
    normalized = std::filesystem::absolute(path, error);
  }
  return error ? path.u8string() : normalized.u8string();
}

std::string sha1(const std::string& data)
{
  unsigned char hash[20];
  message_digest::digest(hash, sizeof(hash), MessageDigest::sha1().get(),
                         data.data(), data.size());
  return util::toHex(hash, sizeof(hash));
}

} // namespace

BtStateStore::BtStateStore(const Option* option)
    : directory_(state::btTorrentDirectory(option))
{
}

std::string BtStateStore::metadataPath(const std::string& data) const
{
  return directory_.empty()
             ? std::string()
             : util::applyDir(directory_, sha1(data) + ".torrent");
}

std::string BtStateStore::resumePath(const std::string& identity) const
{
  return directory_.empty()
             ? std::string()
             : util::applyDir(directory_, identity + ".fastresume");
}

std::string BtStateStore::storeMetadata(const std::string& data) const
{
  const auto path = metadataPath(data);
  if (path.empty()) {
    throw DL_ABORT_EX("BitTorrent state directory is unavailable");
  }
  writeAtomic(path, data.data(), data.size());
  return path;
}

bool BtStateStore::ownsMetadata(const std::string& path) const
{
  if (directory_.empty() || path.empty()) {
    return false;
  }
  const auto candidate = std::filesystem::u8path(path);
  const auto name = candidate.filename().u8string();
  if (!util::endsWith(name, ".torrent") ||
      !isHex(name.substr(0, name.size() - 8), 40)) {
    return false;
  }
  return normalizedPath(candidate.parent_path()) ==
         normalizedPath(std::filesystem::u8path(directory_));
}

std::string BtStateStore::readResume(const std::string& path)
{
  BufferedFile file(path.c_str(), BufferedFile::READ);
  if (!file) {
    return {};
  }
  std::stringstream data;
  file.transfer(data);
  return data.str();
}

void BtStateStore::writeResume(const std::string& path, const char* data,
                               size_t size)
{
  writeAtomic(path, data, size);
}

void BtStateStore::writeAtomic(const std::string& path, const char* data,
                               size_t size)
{
  if (path.empty()) {
    throw DL_ABORT_EX("BitTorrent state path is unavailable");
  }
  File directory(File(path).getDirname());
  if (!directory.isDir() && !directory.mkdirs()) {
    throw DL_ABORT_EX("Unable to create BitTorrent state directory");
  }

  std::array<unsigned char, 12> random{};
  util::generateRandomData(random.data(), random.size());
  const auto temporary = util::applyDir(
      directory.getPath(),
      ".aria2-next-" + util::toHex(random.data(), random.size()) + ".tmp");
  File temporaryFile(temporary);
  bool written = false;
  {
    BufferedFile file(temporary.c_str(), BufferedFile::WRITE);
    if (file && file.write(data, size) == size && file.close() != EOF) {
      written = true;
    }
  }
  if (!written) {
    temporaryFile.remove();
    throw DL_ABORT_EX("Unable to write BitTorrent state");
  }
  if (!temporaryFile.renameTo(path)) {
    temporaryFile.remove();
    throw DL_ABORT_EX("Unable to replace BitTorrent state");
  }
}

void BtStateStore::collect(const std::set<std::string>& referencedPaths) const
{
  if (directory_.empty()) {
    return;
  }
  const auto root = std::filesystem::u8path(directory_);
  std::error_code error;
  if (!std::filesystem::is_directory(root, error)) {
    return;
  }

  std::set<std::string> referenced;
  for (const auto& path : referencedPaths) {
    if (!path.empty()) {
      referenced.insert(normalizedPath(std::filesystem::u8path(path)));
    }
  }

  std::filesystem::directory_iterator iterator(root, error);
  const std::filesystem::directory_iterator end;
  while (!error && iterator != end) {
    const auto entry = *iterator;
    iterator.increment(error);
    std::error_code typeError;
    if (!entry.is_regular_file(typeError)) {
      continue;
    }
    const auto name = entry.path().filename().u8string();
    if (!isManagedName(name)) {
      continue;
    }
    const auto path = normalizedPath(entry.path());
    if (referenced.find(path) == referenced.end()) {
      std::error_code removeError;
      std::filesystem::remove(entry.path(), removeError);
      if (removeError) {
        A2_LOG_WARN(fmt("Unable to remove orphaned BitTorrent state %s: %s",
                        path.c_str(), removeError.message().c_str()));
      }
    }
  }
  if (error) {
    A2_LOG_WARN(fmt("Unable to scan BitTorrent state directory %s: %s",
                    directory_.c_str(), error.message().c_str()));
  }
}

} // namespace aria2
