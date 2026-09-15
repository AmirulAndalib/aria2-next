#include "Ed2kStore.h"

#include <sqlite3.h>

#include "File.h"
#include "a2doctest.h"
#include "ed2k_hash.h"

namespace aria2 {

namespace ed2k {

namespace {

void removeDatabase(const std::string& path)
{
  File(path).remove();
  File(path + "-wal").remove();
  File(path + "-shm").remove();
}

} // namespace

class Ed2kStoreTest {
public:
  void testRebuildsIncompatibleSchemaAtomically();
};

A2_TEST(Ed2kStoreTest, testRebuildsIncompatibleSchemaAtomically)

void Ed2kStoreTest::testRebuildsIncompatibleSchemaAtomically()
{
  const std::string path = A2_TEST_OUT_DIR "/ed2k-store-schema.db";
  removeDatabase(path);

  sqlite3* legacy = nullptr;
  REQUIRE_EQ(SQLITE_OK, sqlite3_open(path.c_str(), &legacy));
  REQUIRE_EQ(SQLITE_OK,
             sqlite3_exec(legacy, "CREATE TABLE downloads(legacy TEXT);"
                                  "PRAGMA user_version=1;",
                          nullptr, nullptr, nullptr));
  REQUIRE_EQ(SQLITE_OK, sqlite3_close(legacy));

  {
    Ed2kStore store(path);
    REQUIRE(store.open());

    PersistedDownloadState state;
    state.gid = "0000000000000001";
    state.fileHash = std::string(HASH_LENGTH, '\x31');
    state.fileSize = 1;
    state.link =
        "ed2k://|file|state.bin|1|31313131313131313131313131313131|/";
    state.path = A2_TEST_OUT_DIR "/state.bin";
    state.bitfield = std::string(1, '\0');
    REQUIRE(store.saveDownload(state));

    PersistedDownloadState loaded;
    REQUIRE(store.loadDownload(loaded, state.gid) ==
            DownloadStateLoadResult::Loaded);
    REQUIRE_EQ(state.fileHash, loaded.fileHash);
    REQUIRE_EQ(state.path, loaded.path);
    REQUIRE(store.loadDownload(loaded, "0000000000000002") ==
            DownloadStateLoadResult::Missing);
  }

  removeDatabase(path);
}

} // namespace ed2k

} // namespace aria2
