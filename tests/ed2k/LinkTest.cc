/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
#include "RecoverableException.h"
#include <cstdint>
#include "ed2k_aich.h"
#include "ed2k_link.h"
#include "ed2k_endpoint.h"

#include "a2doctest.h"
#include <cstring>

#include "base32.h"
#include "support/Encoding.h"

namespace aria2::ed2k {

TEST_CASE("Ed2kHelperTest.testParseFileLink")
{
  auto link = parseLink("ed2k://|file|aria2%20next.bin|12345|"
                        "0123456789ABCDEF0123456789ABCDEF|/");

  REQUIRE_EQ(LinkType::FILE, link.type);
  REQUIRE_EQ(std::string("aria2 next.bin"), link.name);
  REQUIRE_EQ((int64_t)12345, link.size);
  REQUIRE_EQ(std::string("0123456789abcdef0123456789abcdef"),
             util::toHex(link.hash));

  auto unsafeName = parseLink("ed2k://|file|aria2%2Fnext%5Ctest.bin|12345|"
                              "0123456789ABCDEF0123456789ABCDEF|/");
  REQUIRE_EQ(std::string("aria2_next_test.bin"), unsafeName.name);

  auto encodedSeparators =
      parseLink("ed2k://%7Cfile%7Caria2%20next.bin%7C12345%7C"
                "0123456789ABCDEF0123456789ABCDEF%7C/");
  REQUIRE_EQ(LinkType::FILE, encodedSeparators.type);
  REQUIRE_EQ(std::string("aria2 next.bin"), encodedSeparators.name);
}

TEST_CASE("Ed2kHelperTest.testParseFileLinkWithOptions")
{
  auto link = parseLink("ed2k://|file|movie.mkv|9728001|"
                        "0123456789abcdef0123456789abcdef|"
                        "p=11111111111111111111111111111111:"
                        "22222222222222222222222222222222|"
                        "h=ABCDEFGHIJKLMNOPQRSTUVWXYZ234567|"
                        "sources,192.0.2.1:4662,198.51.100.7:7777|/");

  REQUIRE_EQ((size_t)2, link.pieceHashes.size());
  REQUIRE_EQ(std::string(16, '\x11'), link.pieceHashes[0]);
  REQUIRE_EQ(std::string(16, '\x22'), link.pieceHashes[1]);
  std::string aichRoot("ABCDEFGHIJKLMNOPQRSTUVWXYZ234567");
  REQUIRE_EQ(base32::decode(aichRoot.begin(), aichRoot.end()), link.aichHash);
  REQUIRE_EQ((size_t)2, link.sources.size());
  REQUIRE_EQ(std::string("192.0.2.1"), link.sources[0].host);
  REQUIRE_EQ((uint16_t)4662, link.sources[0].port);
  REQUIRE_EQ(std::string("198.51.100.7"), link.sources[1].host);
  REQUIRE_EQ((uint16_t)7777, link.sources[1].port);
}

TEST_CASE("Ed2kHelperTest.testParseFileLinkWithSourceCryptOptions")
{
  auto link = parseLink("ed2k://|file|shared.bin|123|"
                        "0123456789abcdef0123456789abcdef|/|"
                        "sources,203.0.113.1:4662:131:"
                        "11111111111111111111111111111111,"
                        "peer.example.test:7777:1|/");

  REQUIRE_EQ((size_t)2, link.sources.size());
  REQUIRE_EQ(std::string("203.0.113.1"), link.sources[0].host);
  REQUIRE_EQ((uint16_t)4662, link.sources[0].port);
  REQUIRE_EQ((uint16_t)131, link.sources[0].cryptOptions);
  REQUIRE_EQ(std::string(16, '\x11'), link.sources[0].userHash);
  REQUIRE_EQ(std::string("peer.example.test"), link.sources[1].host);
  REQUIRE_EQ((uint16_t)7777, link.sources[1].port);
  REQUIRE_EQ((uint16_t)1, link.sources[1].cryptOptions);
  REQUIRE(link.sources[1].userHash.empty());

  auto reparsed = parseLink(toFileLink(link));
  REQUIRE_EQ((uint16_t)131, reparsed.sources[0].cryptOptions);
  REQUIRE_EQ(std::string(16, '\x11'), reparsed.sources[0].userHash);
  REQUIRE_EQ((uint16_t)1, reparsed.sources[1].cryptOptions);
}

TEST_CASE("Ed2kHelperTest.testParseServerLink")
{
  auto server = parseLink("ed2k://|server|203.0.113.10|4232|/");

  REQUIRE_EQ(LinkType::SERVER, server.type);
  REQUIRE_EQ(std::string("203.0.113.10"), server.server.host);
  REQUIRE_EQ((uint16_t)4232, server.server.port);

  auto serverList =
      parseLink("ed2k://|serverlist|http%3A%2F%2Fexample.test%2Fserver.met|/");
  REQUIRE_EQ(LinkType::SERVER_LIST, serverList.type);
  REQUIRE_EQ(std::string("http://example.test/server.met"), serverList.url);

  auto nodes =
      parseLink("ed2k://|nodeslist|https%3A%2F%2Fexample.test%2Fnodes.dat|/");
  REQUIRE_EQ(LinkType::NODES_LIST, nodes.type);
  REQUIRE_EQ(std::string("https://example.test/nodes.dat"), nodes.url);
}

TEST_CASE("Ed2kHelperTest.testParseSearchLink")
{
  auto search = parseLink("ed2k://|search|linux%20iso|/");

  REQUIRE_EQ(LinkType::SEARCH, search.type);
  REQUIRE_EQ(std::string("linux iso"), search.name);
}

TEST_CASE("Ed2kHelperTest.testParseRejectsMalformedLinks")
{
  REQUIRE_THROWS_AS(parseLink("http://example.test/file"),
                    RecoverableException);
  REQUIRE_THROWS_AS(
      parseLink("ed2k://|file|bad.bin|x|0123456789abcdef0123456789abcdef|/"),
      RecoverableException);
  REQUIRE_THROWS_AS(parseLink("ed2k://|file|bad.bin|1|not-a-hash|/"),
                    RecoverableException);
  REQUIRE_THROWS_AS(
      parseLink("ed2k://|file|empty.bin|0|0123456789abcdef0123456789abcdef|/"),
      RecoverableException);
  REQUIRE_THROWS_AS(parseLink("ed2k://|file|huge.bin|274877906944|"
                              "0123456789abcdef0123456789abcdef|/"),
                    RecoverableException);
  REQUIRE_THROWS_AS(parseLink("ed2k://|file|bad-parts.bin|1|"
                              "0123456789abcdef0123456789abcdef|p=|/"),
                    RecoverableException);
  REQUIRE_THROWS_AS(parseLink("ed2k://|file|bad-aich.bin|1|"
                              "0123456789abcdef0123456789abcdef|h=ABC|/"),
                    RecoverableException);
  REQUIRE_THROWS_AS(parseLink("ed2k://|server|127.0.0.1|70000|/"),
                    RecoverableException);
}

TEST_CASE("Ed2kHelperTest.testSerializeFileLink")
{
  auto link = parseLink("ed2k://|file|aria2%20next.bin|9728001|"
                        "0123456789abcdef0123456789abcdef|"
                        "p=11111111111111111111111111111111:"
                        "22222222222222222222222222222222|"
                        "h=ABCDEFGHIJKLMNOPQRSTUVWXYZ234567|"
                        "sources,192.0.2.1:4662|/");

  auto reparsed = parseLink(toFileLink(link));

  REQUIRE_EQ(link.name, reparsed.name);
  REQUIRE_EQ(link.size, reparsed.size);
  REQUIRE_EQ(link.hash, reparsed.hash);
  REQUIRE_EQ(link.pieceHashes[0], reparsed.pieceHashes[0]);
  REQUIRE_EQ(link.aichHash, reparsed.aichHash);
  REQUIRE_EQ(std::string("192.0.2.1"), reparsed.sources[0].host);
  REQUIRE_EQ((uint16_t)4662, reparsed.sources[0].port);
}

} // namespace aria2::ed2k
