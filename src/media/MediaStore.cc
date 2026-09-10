/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
#include "MediaStore.h"

#include <sqlite3.h>
#include <filesystem>
#include <stdexcept>

namespace aria2 {
namespace media {
namespace {
class Transaction {
public:
  explicit Transaction(sqlite3* db) : db_(db) { execute("BEGIN IMMEDIATE"); }
  ~Transaction()
  {
    if (!committed_)
      sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
  }
  void commit()
  {
    execute("COMMIT");
    committed_ = true;
  }

private:
  void execute(const char* sql)
  {
    if (sqlite3_exec(db_, sql, nullptr, nullptr, nullptr) != SQLITE_OK)
      throw std::runtime_error(sqlite3_errmsg(db_));
  }
  sqlite3* db_;
  bool committed_ = false;
};
class Statement {
public:
  Statement(sqlite3* db, const char* sql) : db_(db)
  {
    if (sqlite3_prepare_v2(db, sql, -1, &stmt_, nullptr) != SQLITE_OK)
      throw std::runtime_error(sqlite3_errmsg(db));
  }
  ~Statement() { sqlite3_finalize(stmt_); }
  void bind(int index, const std::string& value)
  {
    check(sqlite3_bind_text(stmt_, index, value.c_str(), -1, SQLITE_TRANSIENT));
  }
  void bind(int index, int64_t value)
  {
    check(sqlite3_bind_int64(stmt_, index, value));
  }
  bool step()
  {
    auto rc = sqlite3_step(stmt_);
    if (rc != SQLITE_ROW && rc != SQLITE_DONE)
      throw std::runtime_error(sqlite3_errmsg(db_));
    return rc == SQLITE_ROW;
  }
  int64_t number(int column) { return sqlite3_column_int64(stmt_, column); }
  std::string text(int column)
  {
    auto p = sqlite3_column_text(stmt_, column);
    return p ? reinterpret_cast<const char*>(p) : "";
  }

private:
  void check(int rc)
  {
    if (rc != SQLITE_OK)
      throw std::runtime_error(sqlite3_errmsg(db_));
  }
  sqlite3* db_;
  sqlite3_stmt* stmt_ = nullptr;
};
} // namespace

Store::Store(const std::string& directory, std::string gid)
    : gid_(std::move(gid))
{
  std::filesystem::create_directories(std::filesystem::u8path(directory));
  auto path = (std::filesystem::u8path(directory) / "state.db").u8string();
  if (sqlite3_open_v2(path.c_str(), &db_,
                      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                          SQLITE_OPEN_FULLMUTEX,
                      nullptr) != SQLITE_OK) {
    std::string error = db_ ? sqlite3_errmsg(db_) : "Cannot open media state";
    sqlite3_close(db_);
    db_ = nullptr;
    throw std::runtime_error(error);
  }
  sqlite3_busy_timeout(db_, 5000);
  const char* sql =
      "PRAGMA journal_mode=WAL; PRAGMA synchronous=FULL;"
      "CREATE TABLE IF NOT EXISTS media_tasks("
      "gid TEXT PRIMARY KEY,path TEXT NOT NULL,protocol TEXT NOT NULL,live "
      "INTEGER NOT NULL,"
      "duration INTEGER NOT NULL,completed_duration INTEGER NOT NULL,completed "
      "INTEGER NOT NULL);"
      "CREATE TABLE IF NOT EXISTS media_segments("
      "gid TEXT NOT NULL,period INTEGER NOT NULL,track INTEGER NOT NULL,number "
      "INTEGER NOT NULL,"
      "start INTEGER NOT NULL,duration INTEGER NOT NULL,path TEXT NOT "
      "NULL,init TEXT NOT NULL,type TEXT NOT NULL,"
      "PRIMARY KEY(gid,period,track,number));"
      "CREATE TABLE IF NOT EXISTS media_identity(gid TEXT PRIMARY KEY,value "
      "TEXT NOT NULL);"
      "CREATE TABLE IF NOT EXISTS media_manifests(gid TEXT NOT NULL,name TEXT "
      "NOT NULL,digest TEXT NOT NULL,PRIMARY KEY(gid,name));"
      "CREATE TABLE IF NOT EXISTS media_tracks(gid TEXT NOT NULL,id TEXT NOT "
      "NULL,type TEXT NOT NULL,language TEXT NOT NULL,codec TEXT NOT NULL,"
      "width INTEGER NOT NULL,height INTEGER NOT NULL,bandwidth INTEGER NOT "
      "NULL,selected INTEGER NOT NULL,PRIMARY KEY(gid,id));";
  if (sqlite3_exec(db_, sql, nullptr, nullptr, nullptr) != SQLITE_OK) {
    std::string error = sqlite3_errmsg(db_);
    sqlite3_close(db_);
    db_ = nullptr;
    throw std::runtime_error(error);
  }
}
Store::~Store() { sqlite3_close(db_); }
void Store::discard(const std::string& directory, const std::string& gid)
{
  if (directory.empty() || gid.empty())
    return;
  const auto root = std::filesystem::u8path(directory);
  if (std::filesystem::exists(root / "state.db")) {
    Store store(directory, gid);
    store.remove();
  }
  std::filesystem::remove_all(root / "tasks" / gid);
}
void Store::save(const Snapshot& s)
{
  Transaction transaction(db_);
  Statement q(db_, "INSERT OR REPLACE INTO media_tasks VALUES(?,?,?,?,?,?,?)");
  q.bind(1, gid_);
  q.bind(2, s.path);
  q.bind(3, s.protocol);
  q.bind(4, s.live);
  q.bind(5, s.duration);
  q.bind(6, s.completedDuration);
  q.bind(7, s.downloadedLength);
  q.step();
  Statement remove(db_, "DELETE FROM media_tracks WHERE gid=?");
  remove.bind(1, gid_);
  remove.step();
  for (const auto& track : s.tracks) {
    Statement item(db_, "INSERT INTO media_tracks VALUES(?,?,?,?,?,?,?,?,?)");
    item.bind(1, gid_);
    item.bind(2, track.id);
    item.bind(3, track.type);
    item.bind(4, track.language);
    item.bind(5, track.codec);
    item.bind(6, track.width);
    item.bind(7, track.height);
    item.bind(8, track.bandwidth);
    item.bind(9, track.selected);
    item.step();
  }
  transaction.commit();
}
bool Store::load(Snapshot& s)
{
  Statement q(db_,
              "SELECT path,protocol,live,duration,completed_duration,completed "
              "FROM media_tasks WHERE gid=?");
  q.bind(1, gid_);
  if (!q.step())
    return false;
  s.path = q.text(0);
  s.protocol = q.text(1);
  s.live = q.number(2) != 0;
  s.duration = q.number(3);
  s.completedDuration = q.number(4);
  s.downloadedLength = q.number(5);
  Statement tracks(
      db_, "SELECT id,type,language,codec,width,height,bandwidth,selected FROM "
           "media_tracks WHERE gid=? ORDER BY id");
  tracks.bind(1, gid_);
  s.tracks.clear();
  while (tracks.step())
    s.tracks.push_back({tracks.text(0), tracks.text(1), tracks.text(2),
                        tracks.text(3), static_cast<int>(tracks.number(4)),
                        static_cast<int>(tracks.number(5)), tracks.number(6),
                        tracks.number(7) != 0});
  return true;
}
void Store::commit(const Segment& s)
{
  Statement q(
      db_, "INSERT OR REPLACE INTO media_segments VALUES(?,?,?,?,?,?,?,?,?)");
  q.bind(1, gid_);
  q.bind(2, s.period);
  q.bind(3, s.track);
  q.bind(4, s.number);
  q.bind(5, s.start);
  q.bind(6, s.duration);
  q.bind(7, s.path);
  q.bind(8, s.init);
  q.bind(9, s.type);
  q.step();
}
std::vector<Segment> Store::segments()
{
  Statement q(db_,
              "SELECT period,track,number,start,duration,path,init,type FROM "
              "media_segments WHERE gid=? ORDER BY period,track,number");
  q.bind(1, gid_);
  std::vector<Segment> rows;
  while (q.step())
    rows.push_back({q.number(0), static_cast<int>(q.number(1)), q.number(2),
                    q.number(3), q.number(4), q.text(5), q.text(6), q.text(7)});
  return rows;
}
void Store::remove()
{
  Transaction transaction(db_);
  Statement segments(db_, "DELETE FROM media_segments WHERE gid=?");
  segments.bind(1, gid_);
  segments.step();
  Statement tasks(db_, "DELETE FROM media_tasks WHERE gid=?");
  tasks.bind(1, gid_);
  tasks.step();
  for (auto table : {"media_tracks", "media_identity", "media_manifests"}) {
    auto sql = std::string("DELETE FROM ") + table + " WHERE gid=?";
    Statement q(db_, sql.c_str());
    q.bind(1, gid_);
    q.step();
  }
  transaction.commit();
}
void Store::clearSegments()
{
  Statement q(db_, "DELETE FROM media_segments WHERE gid=?");
  q.bind(1, gid_);
  q.step();
}
bool Store::identity(const std::string& value)
{
  bool changed = false;
  {
    Statement q(db_, "SELECT value FROM media_identity WHERE gid=?");
    q.bind(1, gid_);
    changed = q.step() && q.text(0) != value;
  }
  Statement q(db_, "INSERT OR REPLACE INTO media_identity VALUES(?,?)");
  q.bind(1, gid_);
  q.bind(2, value);
  q.step();
  if (changed)
    clearSegments();
  return changed;
}
bool Store::manifest(const std::string& name, const std::string& digest)
{
  bool changed = false;
  {
    Statement q(db_,
                "SELECT digest FROM media_manifests WHERE gid=? AND name=?");
    q.bind(1, gid_);
    q.bind(2, name);
    changed = q.step() && q.text(0) != digest;
  }
  Statement q(db_, "INSERT OR REPLACE INTO media_manifests VALUES(?,?,?)");
  q.bind(1, gid_);
  q.bind(2, name);
  q.bind(3, digest);
  q.step();
  if (changed)
    clearSegments();
  return changed;
}
} // namespace media
} // namespace aria2
