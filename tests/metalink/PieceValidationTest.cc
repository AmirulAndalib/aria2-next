/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
#include <cstddef>
#include <cstdint>
#include "metalink_helper.h"

#include "a2doctest.h"

#include "MetalinkParserStateMachine.h"
#include "Exception.h"
#include "ByteArrayDiskWriter.h"
#include "Metalinker.h"
#include "MetalinkEntry.h"
#include "ChunkChecksum.h"
#include "a2functional.h"

namespace aria2 {

TEST_CASE("MetalinkProcessorTest.testMultiplePieces")
{
  ByteArrayDiskWriter dw;
  dw.setString("<metalink version=\"3.0\" xmlns=\"http://www.metalinker.org/\">"
               "<files>"
               "<file name=\"aria2.tar.bz2\">"
               "  <verification>"
               "    <pieces length=\"1024\" type=\"sha1\">"
               "    </pieces>"
               "    <pieces length=\"512\" type=\"md5\">"
               "    </pieces>"
               "  </verification>"
               "</file>"
               "</files>"
               "</metalink>");

  try {
    // aria2 prefers sha1
    auto m = metalink::parseBinaryStream(&dw);
    auto& e = m->getEntries()[0];
    auto& c = e->chunkChecksum;
    REQUIRE_EQ(std::string("sha-1"), c->getHashType());
    REQUIRE_EQ((int32_t)1_k, c->getPieceLength());
  }
  catch (Exception& e) {
    FAIL(e.stackTrace());
  }
}

TEST_CASE("MetalinkProcessorTest.testBadPieceNo")
{
  ByteArrayDiskWriter dw;
  dw.setString(
      "<metalink version=\"3.0\" xmlns=\"http://www.metalinker.org/\">"
      "<files>"
      "<file name=\"aria2.tar.bz2\">"
      "  <verification>"
      "    <pieces length=\"512\" type=\"sha1\">"
      "      <hash piece=\"0\">44213f9f4d59b557314fadcd233232eebcac8012</hash>"
      "      <hash "
      "piece=\"xyz\">44213f9f4d59b557314fadcd233232eebcac8012</hash>"
      "    </pieces>"
      "    <pieces length=\"1024\" type=\"sha1\">"
      "      <hash piece=\"0\">44213f9f4d59b557314fadcd233232eebcac8012</hash>"
      "    </pieces>"
      "  </verification>"
      "</file>"
      "</files>"
      "</metalink>");

  try {
    auto m = metalink::parseBinaryStream(&dw);
    auto& e = m->getEntries()[0];
    auto& c = e->chunkChecksum;
    REQUIRE(c);
    REQUIRE_EQ((int32_t)1_k, c->getPieceLength());
    REQUIRE_EQ(std::string("sha-1"), c->getHashType());
  }
  catch (Exception& e) {
    FAIL(e.stackTrace());
  }
}

TEST_CASE("MetalinkProcessorTest.testBadPieceLength")
{
  ByteArrayDiskWriter dw;
  dw.setString(
      "<metalink version=\"3.0\" xmlns=\"http://www.metalinker.org/\">"
      "<files>"
      "<file name=\"aria2.tar.bz2\">"
      "  <verification>"
      "    <pieces length=\"xyz\" type=\"sha1\">"
      "      <hash piece=\"0\">44213f9f4d59b557314fadcd233232eebcac8012</hash>"
      "    </pieces>"
      "    <pieces length=\"1024\" type=\"sha1\">"
      "      <hash piece=\"0\">44213f9f4d59b557314fadcd233232eebcac8012</hash>"
      "    </pieces>"
      "  </verification>"
      "</file>"
      "</files>"
      "</metalink>");

  try {
    auto m = metalink::parseBinaryStream(&dw);
    REQUIRE_EQ((size_t)1, m->getEntries().size());
    auto& e = m->getEntries()[0];
    auto& c = e->chunkChecksum;
    REQUIRE(c);
    REQUIRE_EQ((int32_t)1_k, c->getPieceLength());
    REQUIRE_EQ(std::string("sha-1"), c->getHashType());
  }
  catch (Exception& e) {
    FAIL(e.stackTrace());
  }
}

TEST_CASE("MetalinkProcessorTest.testUnsupportedType_piece")
{
  ByteArrayDiskWriter dw;
  dw.setString(
      "<metalink version=\"3.0\" xmlns=\"http://www.metalinker.org/\">"
      "<files>"
      "<file name=\"aria2.tar.bz2\">"
      "  <verification>"
      "    <pieces length=\"512\" type=\"ARIA2\">"
      "      <hash piece=\"0\">44213f9f4d59b557314fadcd233232eebcac8012</hash>"
      "    </pieces>"
      "    <pieces length=\"1024\" type=\"sha1\">"
      "      <hash piece=\"0\">44213f9f4d59b557314fadcd233232eebcac8012</hash>"
      "    </pieces>"
      "  </verification>"
      "</file>"
      "</files>"
      "</metalink>");

  try {
    auto m = metalink::parseBinaryStream(&dw);
    auto& e = m->getEntries()[0];
    auto& c = e->chunkChecksum;
    REQUIRE(c);
    REQUIRE_EQ((int32_t)1_k, c->getPieceLength());
    REQUIRE_EQ(std::string("sha-1"), c->getHashType());
  }
  catch (Exception& e) {
    FAIL(e.stackTrace());
  }
}

} // namespace aria2
