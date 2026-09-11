/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
#include <cstdint>
#include <iterator>
#include <string>
#include <vector>
#include "ed2k_aich.h"
#include "ed2k_hash.h"
#include "ed2k_kad_search.h"
#include "ed2k_link.h"
#include "ed2k_packet.h"
#include "ed2k_search.h"
#include "ed2k_endpoint.h"

#include "a2doctest.h"
#include <cstring>

#include "support/Encoding.h"

namespace aria2::ed2k {

TEST_CASE("Ed2kHelperTest.testSearchResultPayload")
{
  std::string fileHashHex("0123456789abcdef0123456789abcdef");
  auto fileHash = util::fromHex(fileHashHex.begin(), fileHashHex.end());

  std::string tags;
  tags += packUInt32(7);
  tags.push_back(static_cast<char>(0x02 | 0x80));
  tags.push_back('\x01');
  tags += packUInt16(9);
  tags += "video.mkv";
  tags.push_back(static_cast<char>(0x03 | 0x80));
  tags.push_back('\x02');
  tags += packUInt32(5);
  tags.push_back(static_cast<char>(0x03 | 0x80));
  tags.push_back('\x3a');
  tags += packUInt32(1);
  tags.push_back(static_cast<char>(0x02 | 0x80));
  tags.push_back('\x03');
  tags += packUInt16(5);
  tags += "Video";
  tags.push_back(static_cast<char>(0x03 | 0x80));
  tags.push_back('\x15');
  tags += packUInt32(42);
  tags.push_back(static_cast<char>(0x03 | 0x80));
  tags.push_back('\x30');
  tags += packUInt32(7);
  tags.push_back('\x02');
  tags += packUInt16(5);
  tags += "codec";
  tags += packUInt16(4);
  tags += "H264";

  std::string payload;
  payload += packUInt32(1);
  payload += fileHash;
  payload += packUInt32(0x04030201);
  payload += packUInt16(4662);
  payload += tags;
  payload.push_back('\x01');

  SearchResult result;
  REQUIRE(parseSearchResultPayload(result, payload, "server"));
  REQUIRE(result.moreResults);
  REQUIRE_EQ((size_t)1, result.entries.size());
  REQUIRE_EQ(fileHash, result.entries[0].hash);
  REQUIRE_EQ(std::string("video.mkv"), result.entries[0].name);
  REQUIRE_EQ((int64_t)0x100000005LL, result.entries[0].size);
  REQUIRE_EQ(std::string("Video"), result.entries[0].fileType);
  REQUIRE_EQ((uint32_t)42, result.entries[0].sourceCount);
  REQUIRE_EQ((uint32_t)7, result.entries[0].completeSourceCount);
  REQUIRE_EQ(std::string("H264"), result.entries[0].mediaCodec);
  REQUIRE_EQ(std::string("server"), result.entries[0].sourceNetwork);
  REQUIRE_EQ((size_t)1, result.entries[0].sources.size());
  REQUIRE_EQ(std::string("1.2.3.4"), result.entries[0].sources[0].host);
  REQUIRE_EQ((uint16_t)4662, result.entries[0].sources[0].port);
  REQUIRE_EQ(std::string("ed2k://|file|video.mkv|4294967301|"
                         "0123456789abcdef0123456789abcdef|"
                         "sources,1.2.3.4:4662|/"),
             result.entries[0].ed2kLink);
}

TEST_CASE("Ed2kHelperTest.testSearchRequestPayload")
{
  SearchQuery query;
  query.keyword = "ubuntu iso";
  query.fileType = "Pro";
  query.extension = "iso";
  query.minSize = 0x100000001LL;
  query.maxSize = 0x200000001LL;
  query.minSourceCount = 5;
  query.minCompleteSourceCount = 2;

  auto payload = createSearchRequestPayload(query, true);

  std::string expected;
  expected.push_back('\0');
  expected.push_back('\0');
  expected.push_back('\x01');
  expected += packUInt16(10);
  expected += "ubuntu iso";
  expected.push_back('\0');
  expected.push_back('\0');
  expected.push_back('\x02');
  expected += packUInt16(3);
  expected += "Pro";
  expected += packUInt16(1);
  expected.push_back('\x03');
  expected.push_back('\0');
  expected.push_back('\0');
  expected.push_back('\x08');
  expected += packUInt64(0x100000001LL);
  expected.push_back('\x01');
  expected += packUInt16(1);
  expected.push_back('\x02');
  expected.push_back('\0');
  expected.push_back('\0');
  expected.push_back('\x08');
  expected += packUInt64(0x200000001LL);
  expected.push_back('\x02');
  expected += packUInt16(1);
  expected.push_back('\x02');
  expected.push_back('\0');
  expected.push_back('\0');
  expected.push_back('\x03');
  expected += packUInt32(5);
  expected.push_back('\x01');
  expected += packUInt16(1);
  expected.push_back('\x15');
  expected.push_back('\0');
  expected.push_back('\0');
  expected.push_back('\x03');
  expected += packUInt32(2);
  expected.push_back('\x01');
  expected += packUInt16(1);
  expected.push_back('\x30');
  expected.push_back('\x02');
  expected += packUInt16(3);
  expected += "iso";
  expected += packUInt16(1);
  expected.push_back('\x04');

  REQUIRE_EQ(util::toHex(expected), util::toHex(payload));

  auto clamped = createSearchRequestPayload(query, false);
  REQUIRE_EQ(static_cast<char>(0x03), clamped[28]);
  REQUIRE_EQ((uint32_t)0xffffffffu, readUInt32(clamped.data() + 29));
}

TEST_CASE("Ed2kHelperTest.testKadSearchEntriesToSearchResults")
{
  std::string fileIdHex("0123456789abcdef0123456789abcdef");
  auto fileId = util::fromHex(fileIdHex.begin(), fileIdHex.end());
  KadSearchEntry entry;
  entry.id = fileId;
  Tag name;
  name.id = 0x01;
  name.valueType = TagValueType::STRING;
  name.stringValue = "video.mkv";
  entry.tags.push_back(name);
  Tag sizeLow;
  sizeLow.id = 0x02;
  sizeLow.valueType = TagValueType::UINT;
  sizeLow.intValue = 5;
  entry.tags.push_back(sizeLow);
  Tag sizeHigh;
  sizeHigh.id = 0x3a;
  sizeHigh.valueType = TagValueType::UINT;
  sizeHigh.intValue = 1;
  entry.tags.push_back(sizeHigh);
  Tag fileType;
  fileType.id = 0x03;
  fileType.valueType = TagValueType::STRING;
  fileType.stringValue = "Video";
  entry.tags.push_back(fileType);
  Tag extension;
  extension.id = 0x04;
  extension.valueType = TagValueType::STRING;
  extension.stringValue = "mkv";
  entry.tags.push_back(extension);
  Tag sources;
  sources.id = 0x15;
  sources.valueType = TagValueType::UINT;
  sources.intValue = 3;
  entry.tags.push_back(sources);
  Tag complete;
  complete.id = 0x30;
  complete.valueType = TagValueType::UINT;
  complete.intValue = 2;
  entry.tags.push_back(complete);

  auto results = kadSearchEntriesToSearchResults(
      std::vector<KadSearchEntry>{entry}, "kad");

  REQUIRE_EQ((size_t)1, results.size());
  REQUIRE_EQ(fileId, results[0].hash);
  REQUIRE_EQ(std::string("video.mkv"), results[0].name);
  REQUIRE_EQ((int64_t)0x100000005LL, results[0].size);
  REQUIRE_EQ(std::string("Video"), results[0].fileType);
  REQUIRE_EQ(std::string("mkv"), results[0].extension);
  REQUIRE_EQ((uint32_t)3, results[0].sourceCount);
  REQUIRE_EQ((uint32_t)2, results[0].completeSourceCount);
  REQUIRE_EQ(std::string("kad"), results[0].sourceNetwork);
  REQUIRE_EQ(std::string("ed2k://|file|video.mkv|4294967301|"
                         "0123456789abcdef0123456789abcdef|/"),
             results[0].ed2kLink);
}

TEST_CASE("Ed2kHelperTest.testKadSourceEndpointPreservesUdpAndCryptMetadata")
{
  KadSearchEntry entry;
  entry.id = std::string(HASH_LENGTH, '\x44');

  Tag sourceType;
  sourceType.id = 0xff;
  sourceType.valueType = TagValueType::UINT;
  sourceType.intValue = 1;
  entry.tags.push_back(sourceType);

  Tag sourceIp;
  sourceIp.id = 0xfe;
  sourceIp.valueType = TagValueType::UINT;
  sourceIp.intValue = 0xdc84b534;
  entry.tags.push_back(sourceIp);

  Tag sourcePort;
  sourcePort.id = 0xfd;
  sourcePort.valueType = TagValueType::UINT;
  sourcePort.intValue = 4662;
  entry.tags.push_back(sourcePort);

  Tag sourceUdpPort;
  sourceUdpPort.id = 0xfc;
  sourceUdpPort.valueType = TagValueType::UINT;
  sourceUdpPort.intValue = 4672;
  entry.tags.push_back(sourceUdpPort);

  Tag encryption;
  encryption.id = 0xf3;
  encryption.valueType = TagValueType::UINT;
  encryption.intValue = 0x03;
  entry.tags.push_back(encryption);

  Endpoint endpoint;
  REQUIRE(extractKadSourceEndpoint(endpoint, entry));
  REQUIRE_EQ(std::string("220.132.181.52"), endpoint.host);
  REQUIRE_EQ((uint16_t)4662, endpoint.port);
  REQUIRE_EQ(std::string(HASH_LENGTH, '\x44'), endpoint.userHash);
  REQUIRE_EQ((uint16_t)0x03, endpoint.cryptOptions);

  entry.id = util::fromHex(std::begin("0c7fab2a8d37bed47b551391d0d8241d"),
                           std::end("0c7fab2a8d37bed47b551391d0d8241d") - 1);
  REQUIRE(extractKadSourceEndpoint(endpoint, entry));
  REQUIRE_EQ(std::string("2aab7f0cd4be378d9113557b1d24d8d0"),
             util::toHex(endpoint.userHash));

  KadSourceEndpoint source;
  REQUIRE(extractKadSourceEndpoint(source, entry));
  REQUIRE_EQ(std::string("220.132.181.52"), source.endpoint.host);
  REQUIRE_EQ((uint16_t)4662, source.endpoint.port);
  REQUIRE_EQ((uint16_t)4672, source.udpPort);
  REQUIRE_EQ((uint8_t)1, source.sourceType);
  REQUIRE_EQ((uint16_t)0x03, source.endpoint.cryptOptions);

  sourceType.intValue = 3;
  entry.tags[0] = sourceType;
  auto buddyHashTagPayload =
      createStringTag(0xf8, "11111111111111111111111111111111");
  size_t buddyHashTagOffset = 0;
  entry.tags.push_back(readTag(buddyHashTagPayload, buddyHashTagOffset));
  REQUIRE(extractKadSourceEndpoint(source, entry));
  REQUIRE_EQ((uint8_t)3, source.sourceType);
  REQUIRE_EQ((uint16_t)4672, source.udpPort);
  REQUIRE_EQ(std::string("\x11\x11\x11\x11\x11\x11\x11\x11"
                         "\x11\x11\x11\x11\x11\x11\x11\x11",
                         HASH_LENGTH),
             source.buddyId);

  sourceType.intValue = 5;
  entry.tags[0] = sourceType;
  REQUIRE(extractKadSourceEndpoint(source, entry));
  REQUIRE_EQ((uint8_t)5, source.sourceType);

  sourceType.intValue = 6;
  entry.tags[0] = sourceType;
  REQUIRE(extractKadSourceEndpoint(source, entry));
  REQUIRE_EQ((uint8_t)6, source.sourceType);

  KadSearchResult result;
  result.entries.push_back(entry);
  auto endpoints = extractKadSourceEndpoints(result);
  REQUIRE_EQ((size_t)0, endpoints.size());

  sourceType.intValue = 4;
  result.entries[0].tags[0] = sourceType;
  endpoints = extractKadSourceEndpoints(result);
  REQUIRE_EQ((size_t)1, endpoints.size());
  REQUIRE_EQ(std::string("220.132.181.52"), endpoints[0].host);
  REQUIRE_EQ((uint16_t)4662, endpoints[0].port);
}

} // namespace aria2::ed2k
