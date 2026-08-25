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
#include "StreamStore.h"

#include <sqlite3.h>

#include <utility>
#include <vector>

#include "File.h"
#include "Log.h"
#include "fmt.h"

namespace aria2 {

namespace {

class Statement {
public:
  Statement(sqlite3* db, const char* sql)
  {
    if (sqlite3_prepare_v2(db, sql, -1, &value_, nullptr) != SQLITE_OK) {
      value_ = nullptr;
    }
  }

  ~Statement()
  {
    if (value_) {
      sqlite3_finalize(value_);
    }
  }

  sqlite3_stmt* get() const { return value_; }
  explicit operator bool() const { return value_ != nullptr; }

private:
  sqlite3_stmt* value_ = nullptr;
};

bool bindText(sqlite3_stmt* statement, int index, const std::string& value)
{
  return sqlite3_bind_text(statement, index, value.data(),
                           static_cast<int>(value.size()),
                           SQLITE_TRANSIENT) == SQLITE_OK;
}

std::string textColumn(sqlite3_stmt* statement, int index)
{
  const auto value = sqlite3_column_text(statement, index);
  const auto size = sqlite3_column_bytes(statement, index);
  return value && size > 0 ? std::string(reinterpret_cast<const char*>(value),
                                         static_cast<size_t>(size))
                           : std::string();
}

std::string encodeRanges(
    const std::vector<std::pair<int64_t, int64_t>>& ranges)
{
  std::string result;
  for (const auto& range : ranges) {
    if (!result.empty()) {
      result += ',';
    }
    result += std::to_string(range.first);
    result += '-';
    result += std::to_string(range.second);
  }
  return result;
}

bool decodeRanges(std::vector<std::pair<int64_t, int64_t>>& ranges,
                  const std::string& value)
{
  ranges.clear();
  size_t offset = 0;
  while (offset < value.size()) {
    const auto separator = value.find('-', offset);
    const auto end = value.find(',', separator);
    if (separator == std::string::npos) {
      return false;
    }
    try {
      const auto first = std::stoll(value.substr(offset, separator - offset));
      const auto last = std::stoll(
          value.substr(separator + 1, end - separator - 1));
      if (first < 0 || last <= first ||
          (!ranges.empty() && ranges.back().second > first)) {
        return false;
      }
      ranges.emplace_back(first, last);
    }
    catch (const std::exception&) {
      return false;
    }
    if (end == std::string::npos) {
      break;
    }
    offset = end + 1;
  }
  return true;
}

} // namespace

StreamStore::StreamStore(std::string path) : path_(std::move(path)) {}

StreamStore::~StreamStore()
{
  if (db_) {
    sqlite3_close_v2(db_);
  }
}

bool StreamStore::open()
{
  if (db_) {
    return true;
  }
  if (path_.empty()) {
    return false;
  }
  File directory(File(path_).getDirname());
  if ((!directory.isDir() && !directory.mkdirs()) ||
      sqlite3_open_v2(path_.c_str(), &db_,
                      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                          SQLITE_OPEN_FULLMUTEX,
                      nullptr) != SQLITE_OK) {
    A2_LOG_ERROR(fmt("Unable to open stream state database %s: %s",
                     path_.c_str(),
                     db_ ? sqlite3_errmsg(db_) : "out of memory"));
    if (db_) {
      sqlite3_close_v2(db_);
      db_ = nullptr;
    }
    return false;
  }
  sqlite3_busy_timeout(db_, 5000);
  int version = 0;
  {
    Statement versionQuery(db_, "PRAGMA user_version");
    if (versionQuery && sqlite3_step(versionQuery.get()) == SQLITE_ROW) {
      version = sqlite3_column_int(versionQuery.get(), 0);
    }
  }
  if (version != 0 && version != 2 &&
      sqlite3_exec(db_, "DROP TABLE IF EXISTS downloads", nullptr, nullptr,
                   nullptr) != SQLITE_OK) {
    A2_LOG_ERROR("Unable to reset the stream state database");
    sqlite3_close_v2(db_);
    db_ = nullptr;
    return false;
  }
  const char* schema =
      "PRAGMA journal_mode=WAL;"
      "PRAGMA synchronous=NORMAL;"
      "PRAGMA auto_vacuum=INCREMENTAL;"
      "CREATE TABLE IF NOT EXISTS downloads("
      "gid TEXT PRIMARY KEY, uri TEXT NOT NULL, path TEXT NOT NULL, "
      "etag TEXT NOT NULL, last_modified TEXT NOT NULL, "
      "total_length INTEGER NOT NULL, completed_length INTEGER NOT NULL, "
      "completed_ranges TEXT NOT NULL, "
      "updated_at INTEGER NOT NULL DEFAULT (unixepoch()));"
      "PRAGMA user_version=2;";
  char* message = nullptr;
  if (sqlite3_exec(db_, schema, nullptr, nullptr, &message) != SQLITE_OK) {
    A2_LOG_ERROR(fmt("Unable to initialize stream state database: %s",
                     message ? message : sqlite3_errmsg(db_)));
    sqlite3_free(message);
    sqlite3_close_v2(db_);
    db_ = nullptr;
    return false;
  }
  pruneMissingFiles();
  return true;
}

void StreamStore::pruneMissingFiles()
{
  Statement query(db_, "SELECT path FROM downloads");
  if (!query) {
    return;
  }
  std::vector<std::string> missing;
  while (sqlite3_step(query.get()) == SQLITE_ROW) {
    auto path = textColumn(query.get(), 0);
    if (!path.empty() && !File(path).exists()) {
      missing.push_back(std::move(path));
    }
  }
  for (const auto& path : missing) {
    removePath(path);
  }
  if (!missing.empty()) {
    sqlite3_exec(db_,
                 "PRAGMA wal_checkpoint(PASSIVE);"
                 "PRAGMA incremental_vacuum;",
                 nullptr, nullptr, nullptr);
  }
}

bool StreamStore::load(StreamState& state, const std::string& gid,
                       const std::string& path) const
{
  if (!db_) {
    return false;
  }
  Statement statement(
      db_,
      "SELECT gid,uri,path,etag,last_modified,total_length,completed_length "
      ",completed_ranges "
      "FROM downloads WHERE gid=?1 OR path=?2 "
      "ORDER BY gid=?1 DESC,updated_at DESC LIMIT 1");
  if (!statement || !bindText(statement.get(), 1, gid) ||
      !bindText(statement.get(), 2, path) ||
      sqlite3_step(statement.get()) != SQLITE_ROW) {
    return false;
  }
  StreamState value;
  value.gid = textColumn(statement.get(), 0);
  value.uri = textColumn(statement.get(), 1);
  value.path = textColumn(statement.get(), 2);
  value.etag = textColumn(statement.get(), 3);
  value.lastModified = textColumn(statement.get(), 4);
  value.totalLength = sqlite3_column_int64(statement.get(), 5);
  value.completedLength = sqlite3_column_int64(statement.get(), 6);
  if (!decodeRanges(value.completedRanges, textColumn(statement.get(), 7))) {
    return false;
  }
  int64_t rangeLength = 0;
  for (const auto& range : value.completedRanges) {
    if (value.totalLength > 0 && range.second > value.totalLength) {
      return false;
    }
    rangeLength += range.second - range.first;
  }
  if (value.uri.empty() || value.path.empty() || value.totalLength < 0 ||
      value.completedLength < 0 ||
      (value.totalLength > 0 && value.completedLength > value.totalLength) ||
      rangeLength != value.completedLength) {
    return false;
  }
  state = std::move(value);
  return true;
}

bool StreamStore::save(const StreamState& state)
{
  if (!db_ || state.gid.empty() || state.uri.empty() || state.path.empty() ||
      state.totalLength < 0 || state.completedLength < 0) {
    return false;
  }
  Statement removeStale(db_, "DELETE FROM downloads WHERE path=?1 AND gid<>?2");
  if (!removeStale || !bindText(removeStale.get(), 1, state.path) ||
      !bindText(removeStale.get(), 2, state.gid) ||
      sqlite3_step(removeStale.get()) != SQLITE_DONE) {
    return false;
  }
  Statement statement(
      db_,
      "INSERT INTO downloads(gid,uri,path,etag,last_modified,total_length,"
      "completed_length,completed_ranges,updated_at) "
      "VALUES(?1,?2,?3,?4,?5,?6,?7,?8,unixepoch()) "
      "ON CONFLICT(gid) DO UPDATE SET uri=excluded.uri,path=excluded.path,"
      "etag=excluded.etag,last_modified=excluded.last_modified,"
      "total_length=excluded.total_length,"
      "completed_length=excluded.completed_length,"
      "completed_ranges=excluded.completed_ranges,updated_at=excluded.updated_"
      "at");
  return statement && bindText(statement.get(), 1, state.gid) &&
         bindText(statement.get(), 2, state.uri) &&
         bindText(statement.get(), 3, state.path) &&
         bindText(statement.get(), 4, state.etag) &&
         bindText(statement.get(), 5, state.lastModified) &&
         sqlite3_bind_int64(statement.get(), 6, state.totalLength) ==
             SQLITE_OK &&
         sqlite3_bind_int64(statement.get(), 7, state.completedLength) ==
             SQLITE_OK &&
         bindText(statement.get(), 8, encodeRanges(state.completedRanges)) &&
         sqlite3_step(statement.get()) == SQLITE_DONE;
}

bool StreamStore::remove(const std::string& gid)
{
  if (!db_) {
    return false;
  }
  Statement statement(db_, "DELETE FROM downloads WHERE gid=?1");
  return statement && bindText(statement.get(), 1, gid) &&
         sqlite3_step(statement.get()) == SQLITE_DONE;
}

bool StreamStore::removePath(const std::string& path)
{
  if (!db_) {
    return false;
  }
  Statement statement(db_, "DELETE FROM downloads WHERE path=?1");
  return statement && bindText(statement.get(), 1, path) &&
         sqlite3_step(statement.get()) == SQLITE_DONE;
}

} // namespace aria2
