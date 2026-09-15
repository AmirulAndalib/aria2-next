/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include "metalink_helper.h"

#include "a2doctest.h"

#include "MetalinkParserStateMachine.h"
#include "Exception.h"
#include "DefaultDiskWriter.h"
#include "ByteArrayDiskWriter.h"
#include "Metalinker.h"
#include "MetalinkEntry.h"
#include "MetalinkResource.h"
#include "ChunkChecksum.h"
#include "Checksum.h"
#include "Signature.h"
#include "support/Encoding.h"

namespace aria2 {

TEST_CASE("MetalinkProcessorTest.testParseFile")
{
  try {
    auto metalinker = metalink::parseFile(A2_TEST_DIR "/test.xml");
    auto entryItr = std::begin(metalinker->getEntries());

    auto& entry1 = *entryItr;
    REQUIRE_EQ(std::string("aria2-0.5.2.tar.bz2"), entry1->getPath());
    REQUIRE_EQ((int64_t)0LL, entry1->getLength());
    REQUIRE_EQ(std::string("0.5.2"), entry1->version);
    REQUIRE_EQ(std::string("en-US"), entry1->languages[0]);
    REQUIRE_EQ(std::string("Linux-x86"), entry1->oses[0]);
    REQUIRE_EQ(1, entry1->maxConnections);
    REQUIRE_EQ(std::string("a96cf3f0266b91d87d5124cf94326422800b627d"),
               util::toHex(entry1->checksum->getDigest()));
    REQUIRE_EQ(std::string("sha-1"), entry1->checksum->getHashType());
    REQUIRE(entry1->getSignature());
    REQUIRE_EQ(std::string("pgp"), entry1->getSignature()->getType());
    REQUIRE_EQ(std::string("aria2-0.5.2.tar.bz2.sig"),
               entry1->getSignature()->getFile());
    // Note that we don't strip anything
    REQUIRE_EQ(
        std::string(
            "\n-----BEGIN PGP SIGNATURE-----\n"
            "Version: GnuPG v1.4.9 (GNU/Linux)\n"
            "\n"
            "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff\n"
            "ffffffffffffffffffffffff\n"
            "fffff\n"
            "-----END PGP SIGNATURE-----\n"
            "\t"),
        entry1->getSignature()->getBody());

    auto resourceItr1 = std::begin(entry1->resources);
    auto& resource1 = *resourceItr1;
    REQUIRE_EQ(MetalinkResource::TYPE_SFTP, resource1->type);
    REQUIRE_EQ(std::string("jp"), resource1->location);
    REQUIRE_EQ(1, resource1->priority);
    REQUIRE_EQ(std::string("sftp://ftphost/aria2-0.5.2.tar.bz2"),
               resource1->url);
    REQUIRE_EQ(1, resource1->maxConnections);

    ++resourceItr1;
    auto& resource2 = *resourceItr1;
    REQUIRE_EQ(MetalinkResource::TYPE_HTTP, resource2->type);
    REQUIRE_EQ(std::string("us"), resource2->location);
    REQUIRE_EQ(1, resource2->priority);
    REQUIRE_EQ(std::string("http://httphost/aria2-0.5.2.tar.bz2"),
               resource2->url);
    REQUIRE_EQ(-1, resource2->maxConnections);

    ++entryItr;

    auto& entry2 = *entryItr;
    REQUIRE_EQ(std::string("aria2-0.5.1.tar.bz2"), entry2->getPath());
    REQUIRE_EQ((int64_t)345689LL, entry2->getLength());
    REQUIRE_EQ(std::string("0.5.1"), entry2->version);
    REQUIRE_EQ(std::string("ja-JP"), entry2->languages[0]);
    REQUIRE_EQ(std::string("Linux-m68k"), entry2->oses[0]);
    REQUIRE_EQ(-1, entry2->maxConnections);
    REQUIRE_EQ(std::string("4c255b0ed130f5ea880f0aa061c3da0487e251cc"),
               util::toHex(entry2->checksum->getDigest()));
    REQUIRE_EQ((size_t)2, entry2->chunkChecksum->countPieceHash());
    REQUIRE_EQ(262144, entry2->chunkChecksum->getPieceLength());
    REQUIRE_EQ(std::string("179463a88d79cbf0b1923991708aead914f26142"),
               util::toHex(entry2->chunkChecksum->getPieceHash(0)));
    REQUIRE_EQ(std::string("fecf8bc9a1647505fe16746f94e97a477597dbf3"),
               util::toHex(entry2->chunkChecksum->getPieceHash(1)));
    REQUIRE_EQ(std::string("sha-1"), entry2->checksum->getHashType());
    // See that signature is null
    REQUIRE(!entry2->getSignature());

    ++entryItr;

    // test case: verification hash is not provided
    auto& entry3 = *entryItr;
    REQUIRE_EQ(std::string("NoVerificationHash"), entry3->getPath());
    REQUIRE(!entry3->checksum);
    REQUIRE(!entry3->chunkChecksum);

    ++entryItr;

    // test case: unsupported verification hash is included
    auto& entry4 = *entryItr;
    REQUIRE_EQ(std::string("UnsupportedVerificationHashTypeIncluded"),
               entry4->getPath());
    REQUIRE_EQ(std::string("sha-1"), entry4->checksum->getHashType());
    REQUIRE_EQ(std::string("4c255b0ed130f5ea880f0aa061c3da0487e251cc"),
               util::toHex(entry4->checksum->getDigest()));
    REQUIRE_EQ(std::string("sha-1"), entry4->chunkChecksum->getHashType());
  }
  catch (Exception& e) {
    FAIL(e.stackTrace());
  }
}

TEST_CASE("MetalinkProcessorTest.testParseFile_dirtraversal")
{
  auto metalinker =
      metalink::parseFile(A2_TEST_DIR "/metalink3-dirtraversal.xml");
  REQUIRE_EQ((size_t)1, metalinker->getEntries().size());
  auto& e = metalinker->getEntries()[0];
  REQUIRE_EQ(std::string("aria2-0.5.3.tar.bz2"), e->getPath());
  REQUIRE(e->getSignature());
  REQUIRE_EQ(std::string(""), e->getSignature()->getFile());
}

TEST_CASE("MetalinkProcessorTest.testParseBinaryStream")
{
  DefaultDiskWriter dw(A2_TEST_DIR "/test.xml");
  dw.enableReadOnly();
  dw.openExistingFile();

  try {
    auto m = metalink::parseBinaryStream(&dw);
    auto& entry1 = m->getEntries()[0];
    REQUIRE_EQ(std::string("aria2-0.5.2.tar.bz2"), entry1->getPath());
  }
  catch (Exception& e) {
    FAIL(e.stackTrace());
  }
}

TEST_CASE("MetalinkProcessorTest.testLargeFileSize")
{
  ByteArrayDiskWriter dw;
  dw.setString("<metalink version=\"3.0\" xmlns=\"http://www.metalinker.org/\">"
               "<files>"
               "<file name=\"dvd.iso\">"
               "  <size>9223372036854775807</size>"
               "  <resources>"
               "    <url type=\"http\">sftp://mirror/</url>"
               "  </resources>"
               "</file>"
               "</files>"
               "</metalink>");
  try {
    auto m = metalink::parseBinaryStream(&dw);
    auto& e = m->getEntries()[0];
    REQUIRE_EQ((int64_t)9223372036854775807LL, e->getLength());
  }
  catch (Exception& e) {
    FAIL(e.stackTrace());
  }
}

TEST_CASE("MetalinkProcessorTest.testXmlPrefixV3")
{
  ByteArrayDiskWriter dw;
  dw.setString(
      "<m:metalink version=\"3.0\" xmlns:m=\"http://www.metalinker.org/\">"
      "<m:files>"
      "<m:file name=\"dvd.iso\">"
      "  <m:size>9223372036854775807</m:size>"
      "  <m:resources>"
      "    <m:url type=\"http\">sftp://mirror/</m:url>"
      "  </m:resources>"
      "</m:file>"
      "</m:files>"
      "</m:metalink>");

  try {
    auto m = metalink::parseBinaryStream(&dw);
    REQUIRE_EQ((size_t)1, m->getEntries().size());
    auto& e = m->getEntries()[0];
    REQUIRE_EQ((int64_t)9223372036854775807LL, e->getLength());
  }
  catch (Exception& e) {
    FAIL(e.stackTrace());
  }
}

} // namespace aria2
