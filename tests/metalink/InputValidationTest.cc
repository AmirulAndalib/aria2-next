/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
#include <cstddef>
#include <cstdint>
#include "metalink_helper.h"

#include <iostream>

#include "a2doctest.h"

#include "MetalinkParserStateMachine.h"
#include "Exception.h"
#include "ByteArrayDiskWriter.h"
#include "Metalinker.h"
#include "MetalinkEntry.h"
#include "MetalinkResource.h"

namespace aria2 {

TEST_CASE("MetalinkProcessorTest.testMalformedXML")
{
  ByteArrayDiskWriter dw;
  dw.setString("<metalink version=\"3.0\" "
               "xmlns=\"http://www.metalinker.org/\"><files></file></"
               "metalink>");

  try {
    metalink::parseBinaryStream(&dw);
    FAIL("exception must be thrown.");
  }
  catch (Exception& e) {
    std::cerr << e.stackTrace() << std::endl;
  }
}

TEST_CASE("MetalinkProcessorTest.testMalformedXML2")
{
  ByteArrayDiskWriter dw;
  dw.setString("<metalink version=\"3.0\" "
               "xmlns=\"http://www.metalinker.org/\"><files></files>");

  try {
    metalink::parseBinaryStream(&dw);
    FAIL("exception must be thrown.");
  }
  catch (Exception& e) {
    std::cerr << e.stackTrace() << std::endl;
  }
}

TEST_CASE("MetalinkProcessorTest.testBadSize")
{
  ByteArrayDiskWriter dw;
  dw.setString("<metalink version=\"3.0\" xmlns=\"http://www.metalinker.org/\">"
               "<files>"
               "<file name=\"aria2-0.5.2.tar.bz2\">"
               "  <size>abc</size>"
               "  <version>0.5.2</version>"
               "  <language>en-US</language>"
               "  <os>Linux-x86</os>"
               "</file>"
               "</files>"
               "</metalink>");

  try {
    auto m = metalink::parseBinaryStream(&dw);
    auto& e = m->getEntries()[0];
    REQUIRE_EQ(std::string("aria2-0.5.2.tar.bz2"), e->getPath());
    REQUIRE_EQ((int64_t)0LL, e->getLength());
    REQUIRE_EQ(std::string("0.5.2"), e->version);
    REQUIRE_EQ(std::string("en-US"), e->languages[0]);
    REQUIRE_EQ(std::string("Linux-x86"), e->oses[0]);
  }
  catch (Exception& e) {
    FAIL(e.stackTrace());
  }
}

TEST_CASE("MetalinkProcessorTest.testBadMaxConn")
{
  ByteArrayDiskWriter dw;
  dw.setString("<metalink version=\"3.0\" xmlns=\"http://www.metalinker.org/\">"
               "<files>"
               "<file name=\"aria2-0.5.2.tar.bz2\">"
               "  <size>43743838</size>"
               "  <version>0.5.2</version>"
               "  <language>en-US</language>"
               "  <os>Linux-x86</os>"
               "  <resources maxconnections=\"abc\"/>"
               "</file>"
               "</files>"
               "</metalink>");

  try {
    auto m = metalink::parseBinaryStream(&dw);
    auto& e = m->getEntries()[0];
    REQUIRE_EQ((int64_t)43743838LL, e->getLength());
  }
  catch (Exception& e) {
    FAIL(e.stackTrace());
  }
}

TEST_CASE("MetalinkProcessorTest.testNoName")
{
  ByteArrayDiskWriter dw;
  dw.setString("<metalink version=\"3.0\" xmlns=\"http://www.metalinker.org/\">"
               "<files>"
               "<file>"
               "  <size>1024</size>"
               "  <version>0.0.1</version>"
               "  <language>GB</language>"
               "  <os>Linux-x64</os>"
               "</file>"
               "<file name=\"aria2-0.5.2.tar.bz2\">"
               "  <size>43743838</size>"
               "  <version>0.5.2</version>"
               "  <language>en-US</language>"
               "  <os>Linux-x86</os>"
               "</file>"
               "</files>"
               "</metalink>");

  try {
    auto m = metalink::parseBinaryStream(&dw);
    REQUIRE_EQ((size_t)1, m->getEntries().size());
    auto& e = m->getEntries()[0];
    REQUIRE_EQ(std::string("aria2-0.5.2.tar.bz2"), e->getPath());
  }
  catch (Exception& e) {
    FAIL(e.stackTrace());
  }
}

TEST_CASE("MetalinkProcessorTest.testBadURLPrefs")
{
  ByteArrayDiskWriter dw;
  dw.setString("<metalink version=\"3.0\" xmlns=\"http://www.metalinker.org/\">"
               "<files>"
               "<file name=\"aria2-0.5.2.tar.bz2\">"
               "  <size>43743838</size>"
               "  <version>0.5.2</version>"
               "  <language>en-US</language>"
               "  <os>Linux-x86</os>"
               "  <resources>"
               "    <url type=\"sftp\" maxconnections=\"1\" preference=\"xyz\""
               "         location=\"jp\">sftp://mirror/</url>"
               "  </resources>"
               "</file>"
               "</files>"
               "</metalink>");

  try {
    auto m = metalink::parseBinaryStream(&dw);
    auto& e = m->getEntries()[0];
    auto& r = e->resources[0];
    REQUIRE_EQ(MetalinkResource::TYPE_SFTP, r->type);
    REQUIRE_EQ(MetalinkResource::getLowestPriority(), r->priority);
    REQUIRE_EQ(1, r->maxConnections);
    REQUIRE_EQ(std::string("jp"), r->location);
  }
  catch (Exception& e) {
    FAIL(e.stackTrace());
  }
}

TEST_CASE("MetalinkProcessorTest.testBadURLMaxConn")
{
  ByteArrayDiskWriter dw;
  dw.setString("<metalink version=\"3.0\" xmlns=\"http://www.metalinker.org/\">"
               "<files>"
               "<file name=\"aria2-0.5.2.tar.bz2\">"
               "  <size>43743838</size>"
               "  <version>0.5.2</version>"
               "  <language>en-US</language>"
               "  <os>Linux-x86</os>"
               "  <resources>"
               "    <url maxconnections=\"xyz\" type=\"sftp\""
               "         preference=\"100\""
               "         location=\"jp\">sftp://mirror/</url>"
               "  </resources>"
               "</file>"
               "</files>"
               "</metalink>");

  try {
    auto m = metalink::parseBinaryStream(&dw);
    auto& e = m->getEntries()[0];
    auto& r = e->resources[0];
    REQUIRE_EQ(MetalinkResource::TYPE_SFTP, r->type);
    REQUIRE_EQ(1, r->priority);
    REQUIRE_EQ(-1, r->maxConnections);
    REQUIRE_EQ(std::string("jp"), r->location);
  }
  catch (Exception& e) {
    FAIL(e.stackTrace());
  }
}

TEST_CASE("MetalinkProcessorTest.testUnsupportedType")
{
  ByteArrayDiskWriter dw;
  dw.setString("<metalink version=\"3.0\" xmlns=\"http://www.metalinker.org/\">"
               "<files>"
               "<file name=\"aria2-0.5.2.tar.bz2\">"
               "  <size>43743838</size>"
               "  <version>0.5.2</version>"
               "  <language>en-US</language>"
               "  <os>Linux-x86</os>"
               "  <resources>"
               "    <url type=\"sftp\">sftp://mirror/</url>"
               "    <url type=\"magnet\">magnet:xt=XYZ</url>"
               "    <url type=\"http\">http://mirror/</url>"
               "  </resources>"
               "</file>"
               "</files>"
               "</metalink>");

  try {
    auto m = metalink::parseBinaryStream(&dw);
    auto& e = m->getEntries()[0];
    REQUIRE_EQ((size_t)3, e->resources.size());
    auto& r1 = e->resources[0];
    REQUIRE_EQ(MetalinkResource::TYPE_SFTP, r1->type);
    auto& r2 = e->resources[1];
    REQUIRE_EQ(MetalinkResource::TYPE_NOT_SUPPORTED, r2->type);
    auto& r3 = e->resources[2];
    REQUIRE_EQ(MetalinkResource::TYPE_HTTP, r3->type);
  }
  catch (Exception& e) {
    FAIL(e.stackTrace());
  }
}

} // namespace aria2
