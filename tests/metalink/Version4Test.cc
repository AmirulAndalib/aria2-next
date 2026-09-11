/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include "metalink_helper.h"

#include <iostream>

#include "a2doctest.h"

#include "MetalinkParserStateMachine.h"
#include "Exception.h"
#include "DefaultDiskWriter.h"
#include "ByteArrayDiskWriter.h"
#include "Metalinker.h"
#include "MetalinkEntry.h"
#include "MetalinkResource.h"
#include "MetalinkMetaurl.h"
#include "MessageDigest.h"
#include "ChunkChecksum.h"
#include "Checksum.h"
#include "Signature.h"
#include "fmt.h"
#include "RecoverableException.h"
#include "support/Text.h"
#include "support/Encoding.h"
#include "a2functional.h"

namespace aria2 {

TEST_CASE("MetalinkProcessorTest.testParseFileV4")
{
  auto m = metalink::parseFile(A2_TEST_DIR "/metalink4.xml");
  REQUIRE_EQ((size_t)1, m->getEntries().size());
  auto& e = m->getEntries()[0];
  REQUIRE_EQ(std::string("example.ext"), e->getPath());
  REQUIRE_EQ((int64_t)786430LL, e->getLength());
  REQUIRE_EQ(-1, e->maxConnections);
  REQUIRE_EQ(std::string("0beec7b5ea3f0fdbc95d0dd47f3c5bc275da8a33"),
             util::toHex(e->checksum->getDigest()));
  REQUIRE(e->checksum);
  REQUIRE_EQ(std::string("sha-1"), e->checksum->getHashType());
  REQUIRE(e->chunkChecksum);
  if (MessageDigest::supports("sha-256")) {
    REQUIRE_EQ(std::string("sha-256"), e->chunkChecksum->getHashType());
    REQUIRE_EQ(262144, e->chunkChecksum->getPieceLength());
    REQUIRE_EQ((size_t)3, e->chunkChecksum->countPieceHash());
    REQUIRE_EQ(
        std::string(
            "0245178074fd042e19b7c3885b360fc21064b30e73f5626c7e3b005d048069c5"),
        util::toHex(e->chunkChecksum->getPieceHash(0)));
    REQUIRE_EQ(
        std::string(
            "487ba2299be7f759d7c7bf6a4ac3a32cee81f1bb9332fc485947e32918864fb2"),
        util::toHex(e->chunkChecksum->getPieceHash(1)));
    REQUIRE_EQ(
        std::string(
            "37290d74ac4d186e3a8e5785d259d2ec04fac91ae28092e7620ec8bc99e830aa"),
        util::toHex(e->chunkChecksum->getPieceHash(2)));
  }
  else {
    REQUIRE_EQ(std::string("sha-1"), e->chunkChecksum->getHashType());
    REQUIRE_EQ(262144, e->chunkChecksum->getPieceLength());
    REQUIRE_EQ((size_t)3, e->chunkChecksum->countPieceHash());
    REQUIRE_EQ(std::string("5bd9f7248df0f3a6a86ab6c95f48787d546efa14"),
               util::toHex(e->chunkChecksum->getPieceHash(0)));
    REQUIRE_EQ(std::string("9413ee70957a09d55704123687478e07f18c7b29"),
               util::toHex(e->chunkChecksum->getPieceHash(1)));
    REQUIRE_EQ(std::string("44213f9f4d59b557314fadcd233232eebcac8012"),
               util::toHex(e->chunkChecksum->getPieceHash(2)));
  }
  REQUIRE(e->getSignature());
  REQUIRE_EQ(std::string("application/pgp-signature"),
             e->getSignature()->getType());
  REQUIRE_EQ(std::string("a signature"), e->getSignature()->getBody());

  REQUIRE_EQ((size_t)2, e->resources.size());
  auto& r = e->resources[0];
  REQUIRE_EQ(std::string("sftp://ftp.example.com/example.ext"), r->url);
  REQUIRE_EQ(std::string("de"), r->location);
  REQUIRE_EQ(1, r->priority);
  REQUIRE_EQ(std::string("sftp"), MetalinkResource::getTypeString(r->type));
  REQUIRE_EQ(-1, r->maxConnections);
#ifdef ENABLE_BITTORRENT
  REQUIRE_EQ((size_t)1, e->metaurls.size());
  auto& mu = e->metaurls[0];
  REQUIRE_EQ(std::string("http://example.com/example.ext.torrent"), mu->url);
  REQUIRE_EQ(2, mu->priority);
  REQUIRE_EQ(std::string("torrent"), mu->mediatype);
#else  // !ENABLE_BITTORRENT
  REQUIRE_EQ((size_t)0, e->metaurls.size());
#endif // !ENABLE_BITTORRENT
}

TEST_CASE("MetalinkProcessorTest.testParseFileV4_attrs")
{
  std::unique_ptr<Metalinker> m;
  ByteArrayDiskWriter dw;
  {
    // Testing file@name
    const char* tmpl = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                       "<metalink xmlns=\"urn:ietf:params:xml:ns:metalink\">"
                       "<file name=\"%s\">"
                       "<url>http://example.org</url>"
                       "</file>"
                       "</metalink>";
    dw.setString(fmt(tmpl, "foo"));
    m = metalink::parseBinaryStream(&dw);
    REQUIRE_EQ((size_t)1, m->getEntries().size());

    // empty name
    dw.setString(fmt(tmpl, ""));
    try {
      metalink::parseBinaryStream(&dw);
      FAIL("exception must be thrown.");
    }
    catch (RecoverableException& e) {
      // success
    }

    // dir traversing
    dw.setString(fmt(tmpl, "../doughnuts"));
    try {
      m = metalink::parseBinaryStream(&dw);
      FAIL("exception must be thrown.");
    }
    catch (RecoverableException& e) {
      // success
    }
  }
  {
    // Testing url@priority
    const char* tmpl = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                       "<metalink xmlns=\"urn:ietf:params:xml:ns:metalink\">"
                       "<file name=\"example.ext\">"
                       "<url priority=\"%s\">http://example.org</url>"
                       "</file>"
                       "</metalink>";
    dw.setString(fmt(tmpl, "0"));
    try {
      metalink::parseBinaryStream(&dw);
      FAIL("exception must be thrown.");
    }
    catch (RecoverableException& e) {
      // success
    }

    dw.setString(fmt(tmpl, "1"));
    m = metalink::parseBinaryStream(&dw);
    REQUIRE_EQ((size_t)1, m->getEntries().size());

    dw.setString(fmt(tmpl, "100"));
    m = metalink::parseBinaryStream(&dw);
    REQUIRE_EQ((size_t)1, m->getEntries().size());

    dw.setString(fmt(tmpl, "999999"));
    m = metalink::parseBinaryStream(&dw);
    REQUIRE_EQ((size_t)1, m->getEntries().size());

    dw.setString(fmt(tmpl, "1000000"));
    try {
      m = metalink::parseBinaryStream(&dw);
      FAIL("exception must be thrown.");
    }
    catch (RecoverableException& e) {
      // success
    }
    dw.setString(fmt(tmpl, "A"));
    try {
      m = metalink::parseBinaryStream(&dw);
      FAIL("exception must be thrown.");
    }
    catch (RecoverableException& e) {
    }
  }
  {
    // Testing metaurl@priority
    const char* tmpl = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                       "<metalink xmlns=\"urn:ietf:params:xml:ns:metalink\">"
                       "<file name=\"example.ext\">"
                       "<metaurl priority=\"%s\" "
                       "mediatype=\"torrent\">http://example.org</metaurl>"
                       "</file>"
                       "</metalink>";
    dw.setString(fmt(tmpl, "0"));
    try {
      m = metalink::parseBinaryStream(&dw);
      FAIL("exception must be thrown.");
    }
    catch (RecoverableException& e) {
      // success
    }

    dw.setString(fmt(tmpl, "1"));
    m = metalink::parseBinaryStream(&dw);
    REQUIRE_EQ((size_t)1, m->getEntries().size());

    dw.setString(fmt(tmpl, "100"));
    m = metalink::parseBinaryStream(&dw);
    REQUIRE_EQ((size_t)1, m->getEntries().size());

    dw.setString(fmt(tmpl, "999999"));
    m = metalink::parseBinaryStream(&dw);
    REQUIRE_EQ((size_t)1, m->getEntries().size());

    dw.setString(fmt(tmpl, "1000000"));
    try {
      m = metalink::parseBinaryStream(&dw);
      FAIL("exception must be thrown.");
    }
    catch (RecoverableException& e) {
      // success
    }
    dw.setString(fmt(tmpl, "A"));
    try {
      m = metalink::parseBinaryStream(&dw);
      FAIL("exception must be thrown.");
    }
    catch (RecoverableException& e) {
    }
  }
  {
    // Testing metaurl@mediatype

    // no mediatype
    dw.setString("<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                 "<metalink xmlns=\"urn:ietf:params:xml:ns:metalink\">"
                 "<file name=\"example.ext\">"
                 "<metaurl>http://example.org</metaurl>"
                 "</file>"
                 "</metalink>");
    try {
      m = metalink::parseBinaryStream(&dw);
      FAIL("exception must be thrown.");
    }
    catch (RecoverableException& e) {
      // success
    }

    const char* tmpl = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                       "<metalink xmlns=\"urn:ietf:params:xml:ns:metalink\">"
                       "<file name=\"example.ext\">"
                       "<metaurl mediatype=\"%s\">http://example.org</metaurl>"
                       "</file>"
                       "</metalink>";

    dw.setString(fmt(tmpl, "torrent"));
    m = metalink::parseBinaryStream(&dw);
    REQUIRE_EQ((size_t)1, m->getEntries().size());

    // empty mediatype
    dw.setString(fmt(tmpl, ""));
    try {
      m = metalink::parseBinaryStream(&dw);
      FAIL("exception must be thrown.");
    }
    catch (RecoverableException& e) {
      // success
    }
  }
  {
    // Testing metaurl@name
    const char* tmpl = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                       "<metalink xmlns=\"urn:ietf:params:xml:ns:metalink\">"
                       "<file name=\"example.ext\">"
                       "<metaurl mediatype=\"torrent\" "
                       "name=\"%s\">http://example.org</metaurl>"
                       "</file>"
                       "</metalink>";

    dw.setString(fmt(tmpl, "foo"));
    m = metalink::parseBinaryStream(&dw);
    REQUIRE_EQ((size_t)1, m->getEntries().size());

    // dir traversing
    dw.setString(fmt(tmpl, "../doughnuts"));
    try {
      m = metalink::parseBinaryStream(&dw);
      FAIL("exception must be thrown.");
    }
    catch (RecoverableException& e) {
      // success
    }
    // empty name
    dw.setString(fmt(tmpl, ""));
    try {
      m = metalink::parseBinaryStream(&dw);
      FAIL("exception must be thrown.");
    }
    catch (RecoverableException& e) {
      // success
    }
  }
  {
    // Testing pieces@length
    // No pieces@length
    dw.setString("<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                 "<metalink xmlns=\"urn:ietf:params:xml:ns:metalink\">"
                 "<file name=\"example.ext\">"
                 "<url>http://example.org</url>"
                 "<pieces type=\"sha-1\">"
                 "<hash>0beec7b5ea3f0fdbc95d0dd47f3c5bc275da8a33</hash>"
                 "</pieces>"
                 "</file>"
                 "</metalink>");
    try {
      m = metalink::parseBinaryStream(&dw);
      FAIL("exception must be thrown.");
    }
    catch (RecoverableException& e) {
    }

    const char* tmpl = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                       "<metalink xmlns=\"urn:ietf:params:xml:ns:metalink\">"
                       "<file name=\"example.ext\">"
                       "<url>http://example.org</url>"
                       "<pieces length=\"%s\" type=\"sha-1\">"
                       "<hash>0beec7b5ea3f0fdbc95d0dd47f3c5bc275da8a33</hash>"
                       "</pieces>"
                       "</file>"
                       "</metalink>";

    dw.setString(fmt(tmpl, "262144"));
    m = metalink::parseBinaryStream(&dw);
    // empty
    try {
      dw.setString(fmt(tmpl, ""));
      m = metalink::parseBinaryStream(&dw);
      FAIL("exception must be thrown.");
    }
    catch (RecoverableException& e) {
    }
    // not a number
    try {
      dw.setString(fmt(tmpl, "A"));
      m = metalink::parseBinaryStream(&dw);
      FAIL("exception must be thrown.");
    }
    catch (RecoverableException& e) {
    }
  }
  {
    // Testing pieces@type
    // No pieces@type
    dw.setString("<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                 "<metalink xmlns=\"urn:ietf:params:xml:ns:metalink\">"
                 "<file name=\"example.ext\">"
                 "<url>http://example.org</url>"
                 "<pieces length=\"262144\">"
                 "<hash>0beec7b5ea3f0fdbc95d0dd47f3c5bc275da8a33</hash>"
                 "</pieces>"
                 "</file>"
                 "</metalink>");
    try {
      m = metalink::parseBinaryStream(&dw);
      FAIL("exception must be thrown.");
    }
    catch (RecoverableException& e) {
    }

    const char* tmpl = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                       "<metalink xmlns=\"urn:ietf:params:xml:ns:metalink\">"
                       "<file name=\"example.ext\">"
                       "<url>http://example.org</url>"
                       "<pieces length=\"262144\" type=\"%s\">"
                       "<hash>0beec7b5ea3f0fdbc95d0dd47f3c5bc275da8a33</hash>"
                       "</pieces>"
                       "</file>"
                       "</metalink>";

    dw.setString(fmt(tmpl, "sha-1"));
    m = metalink::parseBinaryStream(&dw);
    // empty
    try {
      dw.setString(fmt(tmpl, ""));
      m = metalink::parseBinaryStream(&dw);
      FAIL("exception must be thrown.");
    }
    catch (RecoverableException& e) {
    }
  }
  {
    // Testing hash@type
    // No hash@type
    dw.setString("<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                 "<metalink xmlns=\"urn:ietf:params:xml:ns:metalink\">"
                 "<file name=\"example.ext\">"
                 "<url>http://example.org</url>"
                 "<hash>0beec7b5ea3f0fdbc95d0dd47f3c5bc275da8a33</hash>"
                 "</file>"
                 "</metalink>");
    try {
      m = metalink::parseBinaryStream(&dw);
      FAIL("exception must be thrown.");
    }
    catch (RecoverableException& e) {
    }

    const char* tmpl =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<metalink xmlns=\"urn:ietf:params:xml:ns:metalink\">"
        "<file name=\"example.ext\">"
        "<url>http://example.org</url>"
        "<hash type=\"%s\">0beec7b5ea3f0fdbc95d0dd47f3c5bc275da8a33</hash>"
        "</file>"
        "</metalink>";

    dw.setString(fmt(tmpl, "sha-1"));
    m = metalink::parseBinaryStream(&dw);
    // empty
    try {
      dw.setString(fmt(tmpl, ""));
      m = metalink::parseBinaryStream(&dw);
      FAIL("exception must be thrown.");
    }
    catch (RecoverableException& e) {
    }
  }
  {
    // Testing signature@mediatype
    // No hash@type
    dw.setString("<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                 "<metalink xmlns=\"urn:ietf:params:xml:ns:metalink\">"
                 "<file name=\"example.ext\">"
                 "<url>http://example.org</url>"
                 "<signature>sig</signature>"
                 "</file>"
                 "</metalink>");
    try {
      m = metalink::parseBinaryStream(&dw);
      FAIL("exception must be thrown.");
    }
    catch (RecoverableException& e) {
    }

    const char* tmpl = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                       "<metalink xmlns=\"urn:ietf:params:xml:ns:metalink\">"
                       "<file name=\"example.ext\">"
                       "<url>http://example.org</url>"
                       "<signature mediatype=\"%s\">sig</signature>"
                       "</file>"
                       "</metalink>";

    dw.setString(fmt(tmpl, "application/pgp-signature"));
    m = metalink::parseBinaryStream(&dw);
    // empty
    try {
      dw.setString(fmt(tmpl, ""));
      m = metalink::parseBinaryStream(&dw);
      FAIL("exception must be thrown.");
    }
    catch (RecoverableException& e) {
    }
  }
}

TEST_CASE("MetalinkProcessorTest.testBadSizeV4")
{
  ByteArrayDiskWriter dw;

  const char* tmpl = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                     "<metalink xmlns=\"urn:ietf:params:xml:ns:metalink\">"
                     "<file name=\"foo\">"
                     "<size>%s</size>"
                     "<url>http://example.org</url>"
                     "</file>"
                     "</metalink>";

  dw.setString(fmt(tmpl, "9223372036854775807"));
  metalink::parseBinaryStream(&dw);

  dw.setString(fmt(tmpl, "-1"));
  try {
    metalink::parseBinaryStream(&dw);
    FAIL("exception must be thrown.");
  }
  catch (RecoverableException& e) {
  }
}

} // namespace aria2
