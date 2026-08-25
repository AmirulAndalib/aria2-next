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
#include "Ed2kStore.h"

#include <sqlite3.h>

#include <algorithm>
#include <limits>
#include <utility>

#include "Ed2kUploadQueue.h"
#include "File.h"
#include "Log.h"
#include "ed2k_hash.h"
#include "ed2k_server.h"
#include "fmt.h"

namespace aria2 {

namespace ed2k {

namespace {

class Statement {
public:
  Statement(sqlite3* db, const char* sql) : db_(db)
  {
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt_, nullptr) != SQLITE_OK) {
      A2_LOG_ERROR(fmt("Failed to prepare ED2K database statement: %s",
                       sqlite3_errmsg(db_)));
      stmt_ = nullptr;
    }
  }

  ~Statement()
  {
    if (stmt_) {
      sqlite3_finalize(stmt_);
    }
  }

  sqlite3_stmt* get() const { return stmt_; }
  explicit operator bool() const { return stmt_ != nullptr; }

private:
  sqlite3* db_;
  sqlite3_stmt* stmt_ = nullptr;
};

bool bindText(sqlite3_stmt* stmt, int index, const std::string& value)
{
  return sqlite3_bind_text(stmt, index, value.data(),
                           static_cast<int>(value.size()),
                           SQLITE_TRANSIENT) == SQLITE_OK;
}

bool bindBlob(sqlite3_stmt* stmt, int index, const std::string& value)
{
  return sqlite3_bind_blob(stmt, index, value.data(),
                           static_cast<int>(value.size()),
                           SQLITE_TRANSIENT) == SQLITE_OK;
}

std::string columnBlob(sqlite3_stmt* stmt, int index)
{
  const auto data = static_cast<const char*>(sqlite3_column_blob(stmt, index));
  const auto size = sqlite3_column_bytes(stmt, index);
  return data && size > 0 ? std::string(data, static_cast<size_t>(size))
                          : std::string();
}

std::string columnText(sqlite3_stmt* stmt, int index)
{
  const auto data = sqlite3_column_text(stmt, index);
  const auto size = sqlite3_column_bytes(stmt, index);
  return data && size > 0 ? std::string(reinterpret_cast<const char*>(data),
                                        static_cast<size_t>(size))
                          : std::string();
}

bool stepDone(sqlite3* db, sqlite3_stmt* stmt)
{
  const auto result = sqlite3_step(stmt);
  if (result == SQLITE_DONE) {
    return true;
  }
  A2_LOG_ERROR(fmt("Failed to update ED2K database: %s", sqlite3_errmsg(db)));
  return false;
}

} // namespace

Ed2kStore::Ed2kStore(std::string path) : path_(std::move(path)) {}

Ed2kStore::~Ed2kStore()
{
  if (db_) {
    sqlite3_close_v2(db_);
  }
}

bool Ed2kStore::open()
{
  if (db_) {
    return true;
  }
  if (path_.empty()) {
    return false;
  }
  File directory(File(path_).getDirname());
  if (!directory.isDir() && !directory.mkdirs()) {
    A2_LOG_ERROR(fmt("Failed to create ED2K database directory: %s",
                     directory.getPath().c_str()));
    return false;
  }
  if (sqlite3_open_v2(path_.c_str(), &db_,
                      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                          SQLITE_OPEN_FULLMUTEX,
                      nullptr) != SQLITE_OK) {
    A2_LOG_ERROR(fmt("Failed to open ED2K database %s: %s", path_.c_str(),
                     db_ ? sqlite3_errmsg(db_) : "out of memory"));
    if (db_) {
      sqlite3_close_v2(db_);
      db_ = nullptr;
    }
    return false;
  }
  sqlite3_busy_timeout(db_, 5000);
  if (!exec("PRAGMA foreign_keys=ON") || !exec("PRAGMA journal_mode=WAL") ||
      !exec("PRAGMA synchronous=NORMAL")) {
    sqlite3_close_v2(db_);
    db_ = nullptr;
    return false;
  }
  Statement versionQuery(db_, "PRAGMA user_version");
  if (!versionQuery || sqlite3_step(versionQuery.get()) != SQLITE_ROW) {
    sqlite3_close_v2(db_);
    db_ = nullptr;
    return false;
  }
  const auto version = sqlite3_column_int(versionQuery.get(), 0);
  if (version != 0 && version != 2) {
    A2_LOG_WARN(fmt("Resetting incompatible ED2K database schema version %d.",
                    version));
    if (!exec("BEGIN IMMEDIATE") ||
        !exec("DROP TABLE IF EXISTS download_pieces") ||
        !exec("DROP TABLE IF EXISTS downloads") ||
        !exec("DROP TABLE IF EXISTS source_seeds") ||
        !exec("DROP TABLE IF EXISTS credits") ||
        !exec("DROP TABLE IF EXISTS servers") ||
        !exec("DROP TABLE IF EXISTS meta") || !exec("COMMIT")) {
      exec("ROLLBACK");
      sqlite3_close_v2(db_);
      db_ = nullptr;
      return false;
    }
  }
  if (initializeSchema()) {
    return true;
  }
  sqlite3_close_v2(db_);
  db_ = nullptr;
  return false;
}

bool Ed2kStore::exec(const char* sql) const
{
  char* message = nullptr;
  const auto result = sqlite3_exec(db_, sql, nullptr, nullptr, &message);
  if (result == SQLITE_OK) {
    return true;
  }
  A2_LOG_ERROR(
      fmt("ED2K database error: %s", message ? message : sqlite3_errmsg(db_)));
  sqlite3_free(message);
  return false;
}

bool Ed2kStore::initializeSchema()
{
  return exec("CREATE TABLE IF NOT EXISTS meta("
              "key TEXT PRIMARY KEY, value BLOB NOT NULL)") &&
         exec("CREATE TABLE IF NOT EXISTS servers("
              "endpoint TEXT PRIMARY KEY, payload BLOB NOT NULL)") &&
         exec("CREATE TABLE IF NOT EXISTS credits("
              "user_hash BLOB PRIMARY KEY, uploaded INTEGER NOT NULL, "
              "downloaded INTEGER NOT NULL)") &&
         exec("CREATE TABLE IF NOT EXISTS source_seeds("
              "file_hash BLOB NOT NULL, file_size INTEGER NOT NULL, "
              "ordinal INTEGER NOT NULL, host TEXT NOT NULL, port INTEGER NOT "
              "NULL, "
              "crypt_options INTEGER NOT NULL, user_hash BLOB NOT NULL, "
              "PRIMARY KEY(file_hash, file_size, ordinal))") &&
         exec("CREATE TABLE IF NOT EXISTS downloads("
              "gid TEXT PRIMARY KEY, file_hash BLOB NOT NULL, "
              "file_size INTEGER NOT NULL, link TEXT NOT NULL, path TEXT NOT "
              "NULL, "
              "modified_time INTEGER NOT NULL, paused INTEGER NOT NULL, "
              "complete INTEGER NOT NULL, sharing_time INTEGER NOT NULL "
              "CHECK(sharing_time >= 0), bitfield BLOB NOT NULL, "
              "updated_at INTEGER NOT NULL DEFAULT (unixepoch()))") &&
         exec("CREATE TABLE IF NOT EXISTS download_pieces("
              "gid TEXT NOT NULL REFERENCES downloads(gid) ON DELETE CASCADE, "
              "piece_index INTEGER NOT NULL, piece_length INTEGER NOT NULL, "
              "bitfield BLOB NOT NULL, PRIMARY KEY(gid, piece_index))") &&
         exec("PRAGMA user_version=2");
}

bool Ed2kStore::begin() { return exec("SAVEPOINT aria2_ed2k"); }
bool Ed2kStore::commit() { return exec("RELEASE aria2_ed2k"); }
void Ed2kStore::rollback()
{
  exec("ROLLBACK TO aria2_ed2k");
  exec("RELEASE aria2_ed2k");
}

bool Ed2kStore::loadIdentity(std::string& clientHash,
                             std::string& kadState) const
{
  if (!db_) {
    return false;
  }
  Statement stmt(db_, "SELECT key, value FROM meta WHERE key IN "
                      "('client_hash', 'kad_state')");
  if (!stmt) {
    return false;
  }
  clientHash.clear();
  kadState.clear();
  int result;
  while ((result = sqlite3_step(stmt.get())) == SQLITE_ROW) {
    const auto key = columnText(stmt.get(), 0);
    if (key == "client_hash") {
      clientHash = columnBlob(stmt.get(), 1);
    }
    else if (key == "kad_state") {
      kadState = columnBlob(stmt.get(), 1);
    }
  }
  return result == SQLITE_DONE &&
         (clientHash.empty() || clientHash.size() == HASH_LENGTH);
}

bool Ed2kStore::saveIdentity(const std::string& clientHash,
                             const std::string& kadState)
{
  if (!db_ || clientHash.size() != HASH_LENGTH || !begin()) {
    return false;
  }
  Statement stmt(db_, "INSERT INTO meta(key, value) VALUES(?1, ?2) "
                      "ON CONFLICT(key) DO UPDATE SET value=excluded.value");
  if (!stmt) {
    rollback();
    return false;
  }
  const std::pair<const char*, const std::string*> values[] = {
      {"client_hash", &clientHash}, {"kad_state", &kadState}};
  for (const auto& value : values) {
    sqlite3_reset(stmt.get());
    sqlite3_clear_bindings(stmt.get());
    if (sqlite3_bind_text(stmt.get(), 1, value.first, -1, SQLITE_STATIC) !=
            SQLITE_OK ||
        !bindBlob(stmt.get(), 2, *value.second) || !stepDone(db_, stmt.get())) {
      rollback();
      return false;
    }
  }
  return commit();
}

bool Ed2kStore::loadServers(std::vector<ServerState>& servers) const
{
  if (!db_) {
    return false;
  }
  Statement stmt(db_, "SELECT payload FROM servers ORDER BY endpoint");
  if (!stmt) {
    return false;
  }
  servers.clear();
  int result;
  while ((result = sqlite3_step(stmt.get())) == SQLITE_ROW) {
    ServerState state;
    if (!parseServerStatePayload(state, columnBlob(stmt.get(), 0))) {
      return false;
    }
    state.connecting = false;
    state.connected = false;
    state.handshakeCompleted = false;
    servers.push_back(std::move(state));
  }
  return result == SQLITE_DONE;
}

bool Ed2kStore::replaceServers(const std::vector<ServerState>& servers)
{
  if (!db_ || !begin() || !exec("DELETE FROM servers")) {
    rollback();
    return false;
  }
  Statement stmt(db_, "INSERT INTO servers(endpoint, payload) VALUES(?1, ?2)");
  if (!stmt) {
    rollback();
    return false;
  }
  for (const auto& server : servers) {
    const auto endpoint =
        server.endpoint.host + ":" + std::to_string(server.endpoint.port);
    const auto payload = createServerStatePayload(server);
    sqlite3_reset(stmt.get());
    sqlite3_clear_bindings(stmt.get());
    if (!bindText(stmt.get(), 1, endpoint) ||
        !bindBlob(stmt.get(), 2, payload) || !stepDone(db_, stmt.get())) {
      rollback();
      return false;
    }
  }
  return commit();
}

bool Ed2kStore::loadCredits(std::vector<PeerCreditState>& credits) const
{
  if (!db_) {
    return false;
  }
  Statement stmt(db_, "SELECT user_hash, uploaded, downloaded FROM credits");
  if (!stmt) {
    return false;
  }
  credits.clear();
  int result;
  while ((result = sqlite3_step(stmt.get())) == SQLITE_ROW) {
    PeerCreditState state;
    state.userHash = columnBlob(stmt.get(), 0);
    state.uploaded = static_cast<uint64_t>(sqlite3_column_int64(stmt.get(), 1));
    state.downloaded =
        static_cast<uint64_t>(sqlite3_column_int64(stmt.get(), 2));
    if (state.userHash.size() != HASH_LENGTH) {
      return false;
    }
    credits.push_back(std::move(state));
  }
  return result == SQLITE_DONE;
}

bool Ed2kStore::replaceCredits(const std::vector<PeerCreditState>& credits)
{
  if (!db_ || !begin() || !exec("DELETE FROM credits")) {
    rollback();
    return false;
  }
  Statement stmt(db_, "INSERT INTO credits(user_hash, uploaded, downloaded) "
                      "VALUES(?1, ?2, ?3)");
  if (!stmt) {
    rollback();
    return false;
  }
  for (const auto& credit : credits) {
    if (credit.userHash.size() != HASH_LENGTH ||
        credit.uploaded >
            static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) ||
        credit.downloaded >
            static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
      rollback();
      return false;
    }
    sqlite3_reset(stmt.get());
    sqlite3_clear_bindings(stmt.get());
    if (!bindBlob(stmt.get(), 1, credit.userHash) ||
        sqlite3_bind_int64(stmt.get(), 2,
                           static_cast<int64_t>(credit.uploaded)) !=
            SQLITE_OK ||
        sqlite3_bind_int64(stmt.get(), 3,
                           static_cast<int64_t>(credit.downloaded)) !=
            SQLITE_OK ||
        !stepDone(db_, stmt.get())) {
      rollback();
      return false;
    }
  }
  return commit();
}

bool Ed2kStore::loadFileSources(std::vector<PersistedFileSources>& files) const
{
  if (!db_) {
    return false;
  }
  Statement stmt(db_, "SELECT file_hash, file_size, host, port, crypt_options, "
                      "user_hash FROM source_seeds ORDER BY file_hash, "
                      "file_size, ordinal");
  if (!stmt) {
    return false;
  }
  files.clear();
  int result;
  while ((result = sqlite3_step(stmt.get())) == SQLITE_ROW) {
    const auto fileHash = columnBlob(stmt.get(), 0);
    const auto fileSize = sqlite3_column_int64(stmt.get(), 1);
    if (fileHash.size() != HASH_LENGTH || fileSize <= 0) {
      return false;
    }
    if (files.empty() || files.back().fileHash != fileHash ||
        files.back().fileSize != fileSize) {
      PersistedFileSources file;
      file.fileHash = fileHash;
      file.fileSize = fileSize;
      files.push_back(std::move(file));
    }
    Endpoint source;
    source.host = columnText(stmt.get(), 2);
    source.port = static_cast<uint16_t>(sqlite3_column_int(stmt.get(), 3));
    source.cryptOptions =
        static_cast<uint16_t>(sqlite3_column_int(stmt.get(), 4));
    source.userHash = columnBlob(stmt.get(), 5);
    if (source.host.empty() || source.port == 0 ||
        (!source.userHash.empty() && source.userHash.size() != HASH_LENGTH)) {
      return false;
    }
    files.back().sources.push_back(std::move(source));
  }
  return result == SQLITE_DONE;
}

bool Ed2kStore::replaceFileSources(
    const std::vector<PersistedFileSources>& files)
{
  if (!db_ || !begin() || !exec("DELETE FROM source_seeds")) {
    rollback();
    return false;
  }
  Statement stmt(
      db_,
      "INSERT INTO source_seeds(file_hash, file_size, ordinal, host, "
      "port, crypt_options, user_hash) VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7)");
  if (!stmt) {
    rollback();
    return false;
  }
  for (const auto& file : files) {
    for (size_t i = 0; i < file.sources.size(); ++i) {
      const auto& source = file.sources[i];
      sqlite3_reset(stmt.get());
      sqlite3_clear_bindings(stmt.get());
      if (!bindBlob(stmt.get(), 1, file.fileHash) ||
          sqlite3_bind_int64(stmt.get(), 2, file.fileSize) != SQLITE_OK ||
          sqlite3_bind_int64(stmt.get(), 3, static_cast<int64_t>(i)) !=
              SQLITE_OK ||
          !bindText(stmt.get(), 4, source.host) ||
          sqlite3_bind_int(stmt.get(), 5, source.port) != SQLITE_OK ||
          sqlite3_bind_int(stmt.get(), 6, source.cryptOptions) != SQLITE_OK ||
          !bindBlob(stmt.get(), 7, source.userHash) ||
          !stepDone(db_, stmt.get())) {
        rollback();
        return false;
      }
    }
  }
  return commit();
}

bool Ed2kStore::saveRuntime(const std::string& clientHash,
                            const std::string& kadState,
                            const std::vector<ServerState>& servers,
                            const std::vector<PersistedFileSources>& files,
                            const std::vector<PeerCreditState>& credits)
{
  if (!db_ || !begin()) {
    return false;
  }
  if (!saveIdentity(clientHash, kadState) || !replaceServers(servers) ||
      !replaceFileSources(files) || !replaceCredits(credits)) {
    rollback();
    return false;
  }
  return commit();
}

bool Ed2kStore::hasDownload(const std::string& gid) const
{
  if (!db_) {
    return false;
  }
  Statement stmt(db_, "SELECT 1 FROM downloads WHERE gid=?1");
  return stmt && bindText(stmt.get(), 1, gid) &&
         sqlite3_step(stmt.get()) == SQLITE_ROW;
}

bool Ed2kStore::loadDownload(PersistedDownloadState& state,
                             const std::string& gid) const
{
  if (!db_) {
    return false;
  }
  Statement stmt(db_, "SELECT file_hash, file_size, link, path, modified_time, "
                      "paused, complete, sharing_time, bitfield "
                      "FROM downloads WHERE gid=?1");
  if (!stmt || !bindText(stmt.get(), 1, gid) ||
      sqlite3_step(stmt.get()) != SQLITE_ROW) {
    return false;
  }
  PersistedDownloadState loaded;
  loaded.gid = gid;
  loaded.fileHash = columnBlob(stmt.get(), 0);
  loaded.fileSize = sqlite3_column_int64(stmt.get(), 1);
  loaded.link = columnText(stmt.get(), 2);
  loaded.path = columnText(stmt.get(), 3);
  loaded.modifiedTime = sqlite3_column_int64(stmt.get(), 4);
  loaded.paused = sqlite3_column_int(stmt.get(), 5) != 0;
  loaded.complete = sqlite3_column_int(stmt.get(), 6) != 0;
  loaded.sharingTime = sqlite3_column_int64(stmt.get(), 7);
  loaded.bitfield = columnBlob(stmt.get(), 8);
  if (loaded.fileHash.size() != HASH_LENGTH || loaded.fileSize <= 0 ||
      loaded.path.empty() || loaded.sharingTime < 0) {
    return false;
  }

  Statement pieces(
      db_, "SELECT piece_index, piece_length, bitfield FROM download_pieces "
           "WHERE gid=?1 ORDER BY piece_index");
  if (!pieces || !bindText(pieces.get(), 1, gid)) {
    return false;
  }
  int result;
  while ((result = sqlite3_step(pieces.get())) == SQLITE_ROW) {
    PersistedPieceState piece;
    piece.index = static_cast<size_t>(sqlite3_column_int64(pieces.get(), 0));
    piece.length = sqlite3_column_int64(pieces.get(), 1);
    piece.bitfield = columnBlob(pieces.get(), 2);
    if (piece.length <= 0 || piece.bitfield.empty()) {
      return false;
    }
    loaded.pieces.push_back(std::move(piece));
  }
  if (result != SQLITE_DONE) {
    return false;
  }
  state = std::move(loaded);
  return true;
}

bool Ed2kStore::loadDownloads(std::vector<PersistedDownloadState>& states) const
{
  if (!db_) {
    return false;
  }
  Statement stmt(db_, "SELECT gid FROM downloads ORDER BY updated_at, gid");
  if (!stmt) {
    return false;
  }
  std::vector<std::string> gids;
  int result;
  while ((result = sqlite3_step(stmt.get())) == SQLITE_ROW) {
    gids.push_back(columnText(stmt.get(), 0));
  }
  if (result != SQLITE_DONE) {
    return false;
  }
  states.clear();
  states.reserve(gids.size());
  for (const auto& gid : gids) {
    PersistedDownloadState state;
    if (!loadDownload(state, gid)) {
      return false;
    }
    states.push_back(std::move(state));
  }
  return true;
}

bool Ed2kStore::saveDownload(const PersistedDownloadState& state)
{
  if (!db_ || state.gid.empty() || state.fileHash.size() != HASH_LENGTH ||
      state.fileSize <= 0 || state.path.empty() || state.sharingTime < 0 ||
      !begin()) {
    return false;
  }
  Statement download(
      db_,
      "INSERT INTO downloads(gid, file_hash, file_size, link, path, "
      "modified_time, paused, complete, sharing_time, bitfield, updated_at) "
      "VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, unixepoch()) "
      "ON CONFLICT(gid) DO UPDATE SET file_hash=excluded.file_hash, "
      "file_size=excluded.file_size, link=excluded.link, path=excluded.path, "
      "modified_time=excluded.modified_time, paused=excluded.paused, "
      "complete=excluded.complete, sharing_time=excluded.sharing_time, "
      "bitfield=excluded.bitfield, "
      "updated_at=excluded.updated_at");
  if (!download || !bindText(download.get(), 1, state.gid) ||
      !bindBlob(download.get(), 2, state.fileHash) ||
      sqlite3_bind_int64(download.get(), 3, state.fileSize) != SQLITE_OK ||
      !bindText(download.get(), 4, state.link) ||
      !bindText(download.get(), 5, state.path) ||
      sqlite3_bind_int64(download.get(), 6, state.modifiedTime) != SQLITE_OK ||
      sqlite3_bind_int(download.get(), 7, state.paused ? 1 : 0) != SQLITE_OK ||
      sqlite3_bind_int(download.get(), 8, state.complete ? 1 : 0) !=
          SQLITE_OK ||
      sqlite3_bind_int64(download.get(), 9, state.sharingTime) != SQLITE_OK ||
      !bindBlob(download.get(), 10, state.bitfield) ||
      !stepDone(db_, download.get())) {
    rollback();
    return false;
  }

  Statement removePieces(db_, "DELETE FROM download_pieces WHERE gid=?1");
  if (!removePieces || !bindText(removePieces.get(), 1, state.gid) ||
      !stepDone(db_, removePieces.get())) {
    rollback();
    return false;
  }
  Statement piece(
      db_,
      "INSERT INTO download_pieces(gid, piece_index, piece_length, bitfield) "
      "VALUES(?1, ?2, ?3, ?4)");
  if (!piece) {
    rollback();
    return false;
  }
  for (const auto& value : state.pieces) {
    sqlite3_reset(piece.get());
    sqlite3_clear_bindings(piece.get());
    if (!bindText(piece.get(), 1, state.gid) ||
        sqlite3_bind_int64(piece.get(), 2, static_cast<int64_t>(value.index)) !=
            SQLITE_OK ||
        sqlite3_bind_int64(piece.get(), 3, value.length) != SQLITE_OK ||
        !bindBlob(piece.get(), 4, value.bitfield) ||
        !stepDone(db_, piece.get())) {
      rollback();
      return false;
    }
  }
  return commit();
}

bool Ed2kStore::removeDownload(const std::string& gid)
{
  if (!db_) {
    return false;
  }
  Statement stmt(db_, "DELETE FROM downloads WHERE gid=?1");
  return stmt && bindText(stmt.get(), 1, gid) && stepDone(db_, stmt.get());
}

} // namespace ed2k

} // namespace aria2
