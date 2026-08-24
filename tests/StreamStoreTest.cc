#include "StreamStore.h"

#include <fstream>

#include "a2doctest.h"

#include "File.h"
#include "TestUtil.h"

namespace aria2 {

class StreamStoreTest {
public:
  void testResumeLookupAndCleanup();
  void testPrunesMissingPayloads();
};

A2_TEST(StreamStoreTest, testResumeLookupAndCleanup)
A2_TEST(StreamStoreTest, testPrunesMissingPayloads)

void StreamStoreTest::testResumeLookupAndCleanup()
{
  const std::string path = A2_TEST_OUT_DIR "/stream-state.db";
  File(path).remove();
  File(path + "-wal").remove();
  File(path + "-shm").remove();

  StreamStore store(path);
  REQUIRE(store.open());

  StreamState saved;
  saved.gid = "0000000000000001";
  saved.uri = "https://example.test/file.bin";
  saved.path = A2_TEST_OUT_DIR "/file.bin";
  saved.etag = "\"revision-one\"";
  saved.totalLength = 4096;
  saved.completedLength = 1024;
  REQUIRE(store.save(saved));

  StreamState restored;
  REQUIRE(store.load(restored, "0000000000000002", saved.path));
  REQUIRE_EQ(saved.gid, restored.gid);
  REQUIRE_EQ(saved.completedLength, restored.completedLength);

  saved.gid = "0000000000000002";
  saved.completedLength = 2048;
  REQUIRE(store.save(saved));
  REQUIRE(!store.load(restored, "0000000000000001", ""));
  REQUIRE(store.removePath(saved.path));
  REQUIRE(!store.load(restored, saved.gid, saved.path));
}

void StreamStoreTest::testPrunesMissingPayloads()
{
  const std::string database = A2_TEST_OUT_DIR "/stream-prune.db";
  const std::string payload = A2_TEST_OUT_DIR "/stream-prune.bin";
  File(database).remove();
  File(database + "-wal").remove();
  File(database + "-shm").remove();
  {
    std::ofstream output(payload, std::ios::binary);
    output.put('x');
  }
  {
    StreamStore store(database);
    REQUIRE(store.open());
    StreamState state;
    state.gid = "0000000000000003";
    state.uri = "https://example.test/prune.bin";
    state.path = payload;
    state.totalLength = 2;
    state.completedLength = 1;
    REQUIRE(store.save(state));
  }
  REQUIRE(File(payload).remove());
  StreamStore reopened(database);
  REQUIRE(reopened.open());
  StreamState restored;
  REQUIRE(!reopened.load(restored, "0000000000000003", payload));
}

} // namespace aria2
