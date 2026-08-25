#include "BtDownload.h"

#include "a2doctest.h"

#include <libtorrent/create_torrent.hpp>
#include <libtorrent/sha1_hash.hpp>

#include <array>

namespace aria2 {

namespace {
constexpr int64_t PIECE_SIZE = 16 * 1024;

std::string createMetainfo(libtorrent::create_flags_t flags, bool multiFile)
{
  std::vector<libtorrent::create_file_entry> files;
  if (multiFile) {
    files.emplace_back("bundle/a.bin", PIECE_SIZE);
    files.emplace_back("bundle/sub/b.bin", PIECE_SIZE);
  }
  else {
    files.emplace_back("single.bin", PIECE_SIZE);
  }

  libtorrent::create_torrent torrent(std::move(files), PIECE_SIZE, flags);
  if (!(flags & libtorrent::create_torrent::v2_only)) {
    for (const auto piece : torrent.piece_range()) {
      torrent.set_hash(piece, libtorrent::sha1_hash::max());
    }
  }
  if (!(flags & libtorrent::create_torrent::v1_only)) {
    for (const auto file : torrent.file_range()) {
      for (const auto piece : torrent.file_piece_range(file)) {
        torrent.set_hash2(file, piece, libtorrent::sha256_hash::max());
      }
    }
  }
  const auto data = torrent.generate_buf();
  return {data.data(), data.size()};
}
} // namespace

class BtDownloadTest {
public:
  void testMetainfoInspection();
  void testInvalidMetainfo();
};

A2_TEST(BtDownloadTest, testMetainfoInspection)
A2_TEST(BtDownloadTest, testInvalidMetainfo)

void BtDownloadTest::testMetainfoInspection()
{
  struct TestCase {
    libtorrent::create_flags_t flags;
    bool multiFile;
    bool hasV1;
    bool hasV2;
  };
  const std::array<TestCase, 3> cases = {{
      {libtorrent::create_torrent::v1_only, false, true, false},
      {libtorrent::create_torrent::v2_only, false, false, true},
      {libtorrent::create_flags_t{}, true, true, true},
  }};

  for (const auto& test : cases) {
    const auto download =
        BtDownload::fromBuffer(createMetainfo(test.flags, test.multiFile), {});
    const auto metainfo = download->metainfo();
    REQUIRE_EQ(test.multiFile ? BtMetainfo::Mode::Multi
                              : BtMetainfo::Mode::Single,
               metainfo.mode);
    REQUIRE_EQ(test.multiFile ? std::string("bundle")
                              : std::string("single.bin"),
               metainfo.name);
    REQUIRE_EQ(test.multiFile ? (size_t)2 : (size_t)1, metainfo.files.size());
    REQUIRE_EQ(test.multiFile ? PIECE_SIZE * 2 : PIECE_SIZE,
               metainfo.totalLength);
    REQUIRE_EQ(test.hasV1 ? (size_t)40 : (size_t)0, metainfo.infoHashV1.size());
    REQUIRE_EQ(test.hasV2 ? (size_t)64 : (size_t)0, metainfo.infoHashV2.size());
    REQUIRE_EQ((size_t)1, metainfo.files.front().index);
    REQUIRE_EQ(test.multiFile ? std::string("bundle/a.bin")
                              : std::string("single.bin"),
               metainfo.files.front().path);
    REQUIRE_EQ(PIECE_SIZE, metainfo.files.front().length);
    if (test.multiFile) {
      REQUIRE_EQ((size_t)2, metainfo.files.back().index);
      REQUIRE_EQ(std::string("bundle/sub/b.bin"), metainfo.files.back().path);
    }
  }
}

void BtDownloadTest::testInvalidMetainfo()
{
  try {
    BtDownload::fromBuffer("not torrent metadata", {});
    FAIL("Invalid torrent metadata was accepted");
  }
  catch (const BtMetainfoError& error) {
    REQUIRE_EQ(std::string("invalidTorrent"), error.kind());
    REQUIRE(!error.category().empty());
    REQUIRE(error.nativeCode() != 0);
  }
}

} // namespace aria2
