/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
#include <cstdint>
#include <string>
#include <vector>
#include "ed2k_aich.h"
#include "ed2k_constants.h"
#include "ed2k_hash.h"
#include "ed2k_link.h"
#include "ed2k_packet.h"
#include "ed2k_peer.h"
#include "ed2k_search.h"
#include "ed2k_server.h"
#include "ed2k_endpoint.h"

#include <algorithm>
#include "a2doctest.h"
#include "DlAbortEx.h"
#include <cstring>

#include "support/Encoding.h"

namespace aria2::ed2k {

TEST_CASE("Ed2kHelperTest.testPacketHelpers")
{
  std::string payload;
  payload += "abc";
  auto packet = createPacket(PROTO_EDONKEY, OP_GETSOURCES, payload);

  REQUIRE_EQ((size_t)9, packet.size());
  REQUIRE_EQ((unsigned char)PROTO_EDONKEY, (unsigned char)packet[0]);
  REQUIRE_EQ(std::string("04000000"), util::toHex(packet.substr(1, 4)));
  REQUIRE_EQ((unsigned char)OP_GETSOURCES, (unsigned char)packet[5]);
  REQUIRE_EQ(std::string("abc"), packet.substr(6));

  PacketHeader header;
  REQUIRE(readPacketHeader(header, packet.data(), packet.size()));
  REQUIRE_EQ((uint8_t)PROTO_EDONKEY, header.protocol);
  REQUIRE_EQ((uint32_t)4, header.size);
  REQUIRE_EQ((uint8_t)OP_GETSOURCES, header.opcode);
  REQUIRE_EQ((size_t)3, header.payloadSize());

  REQUIRE_EQ(std::string("78563412"), util::toHex(packUInt32(0x12345678)));
  REQUIRE_EQ((uint32_t)0x12345678,
             readUInt32(std::string("\x78\x56\x34\x12", 4).data()));
}

TEST_CASE("Ed2kHelperTest.testTagParser")
{
  std::string payload;
  payload += packUInt32(5);
  payload.push_back(static_cast<char>(0x02 | 0x80));
  payload.push_back('\x01');
  payload += packUInt16(9);
  payload += "video.mkv";
  payload.push_back(static_cast<char>(0x03 | 0x80));
  payload.push_back('\x15');
  payload += packUInt32(77);
  payload.push_back(static_cast<char>(0x0b | 0x80));
  payload.push_back('\x02');
  payload += packUInt64(0x100000005LL);
  payload.push_back(static_cast<char>(0x13 | 0x80));
  payload.push_back('\x03');
  payload += "Vid";
  payload.push_back(0x03);
  payload += packUInt16(1);
  payload.push_back('\xfe');
  payload += packUInt32(0x6f71c138);

  std::vector<Tag> tags;
  REQUIRE(parseTagList(tags, payload));
  REQUIRE_EQ((size_t)5, tags.size());
  REQUIRE_EQ((uint8_t)0x01, tags[0].id);
  REQUIRE_EQ(std::string("video.mkv"), tags[0].stringValue);
  REQUIRE_EQ((uint8_t)0x15, tags[1].id);
  REQUIRE_EQ((uint64_t)77, tags[1].intValue);
  REQUIRE_EQ((uint8_t)0x02, tags[2].id);
  REQUIRE_EQ((uint64_t)0x100000005LL, tags[2].intValue);
  REQUIRE_EQ(std::string("Vid"), tags[3].stringValue);
  REQUIRE_EQ((uint8_t)0xfe, tags[4].id);
  REQUIRE_EQ((uint64_t)0x6f71c138, tags[4].intValue);
}

TEST_CASE("Ed2kHelperTest.testProtocolPayloads")
{
  std::string fileHashHex("0123456789abcdef0123456789abcdef");
  auto fileHash = util::fromHex(fileHashHex.begin(), fileHashHex.end());
  std::string clientHashHex("11111111111111111111111111111111");
  auto clientHash = util::fromHex(clientHashHex.begin(), clientHashHex.end());

  auto login =
      createLoginRequestPayload(clientHash, 0x04030201, 0, "aria2-next");
  REQUIRE_EQ(clientHash, login.substr(0, HASH_LENGTH));
  REQUIRE_EQ(std::string("010203040000"), util::toHex(login.substr(16, 6)));
  REQUIRE_EQ((uint32_t)4, readUInt32(login.data() + 22));
  std::vector<Tag> loginTags;
  REQUIRE(parseTagList(loginTags, login.substr(22)));
  REQUIRE_EQ((uint64_t)0x071d, loginTags[1].intValue);
  REQUIRE_EQ((uint64_t)0x03060000, loginTags[3].intValue);

  auto source32 = createGetSourcesPayload(fileHash, 9728001);
  REQUIRE_EQ((size_t)20, source32.size());
  REQUIRE_EQ(fileHash, source32.substr(0, HASH_LENGTH));
  REQUIRE_EQ((uint32_t)9728001, readUInt32(source32.data() + 16));

  auto source64 = createGetSourcesPayload(fileHash, 0x100000001LL);
  REQUIRE_EQ((size_t)28, source64.size());
  REQUIRE_EQ((uint32_t)0, readUInt32(source64.data() + 16));
  REQUIRE_EQ((uint32_t)1, readUInt32(source64.data() + 20));
  REQUIRE_EQ((uint32_t)1, readUInt32(source64.data() + 24));

  auto globSources = createGlobGetSourcesPayload(fileHash, 9728001, false);
  REQUIRE_EQ((size_t)16, globSources.size());
  REQUIRE_EQ(fileHash, globSources);
  REQUIRE_EQ(source32, createGlobGetSourcesPayload(fileHash, 9728001, true));

  std::vector<Endpoint> sources;
  Endpoint source;
  source.host = "1.2.3.4";
  source.port = 4662;
  sources.push_back(source);
  auto found = createFoundSourcesPayload(fileHash, sources);
  auto parsedSources = parseFoundSourcesPayload(found);
  REQUIRE_EQ((size_t)1, parsedSources.size());
  REQUIRE_EQ(std::string("1.2.3.4"), parsedSources[0].host);
  REQUIRE_EQ((uint16_t)4662, parsedSources[0].port);
  REQUIRE(parseFoundSourcesPayload(parsedSources, found, fileHash));
  REQUIRE(!parseFoundSourcesPayload(parsedSources, found, clientHash));
  std::vector<FoundSource> foundSources;
  REQUIRE(parseFoundSourcesPayload(foundSources, found, fileHash));
  REQUIRE_EQ((size_t)1, foundSources.size());
  REQUIRE_EQ((uint32_t)0x04030201, foundSources[0].clientId);
  REQUIRE(!foundSources[0].lowId);

  Endpoint lowId;
  lowId.host = "120.0.0.0";
  lowId.port = 4662;
  auto lowIdPayload =
      createFoundSourcesPayload(fileHash, std::vector<Endpoint>{lowId});
  REQUIRE(parseFoundSourcesPayload(foundSources, lowIdPayload, fileHash));
  REQUIRE_EQ((uint32_t)120, foundSources[0].clientId);
  REQUIRE(foundSources[0].lowId);

  auto obfuPayload = found;
  obfuPayload.push_back(static_cast<char>(0x81));
  obfuPayload += clientHash;
  REQUIRE(parseFoundSourcesPayload(foundSources, obfuPayload, fileHash, true));
  REQUIRE_EQ((size_t)1, foundSources.size());
  REQUIRE_EQ((uint16_t)0x81, foundSources[0].endpoint.cryptOptions);
  REQUIRE_EQ(clientHash, foundSources[0].endpoint.userHash);
  REQUIRE(!parseFoundSourcesPayload(foundSources, obfuPayload, fileHash));

  Endpoint source2;
  source2.host = "5.6.7.8";
  source2.port = 4662;
  auto packedFound =
      createFoundSourcesPayload(clientHash, std::vector<Endpoint>{source}) +
      createDatagram(
          PROTO_EDONKEY, OP_GLOBFOUNDSOURCES,
          createFoundSourcesPayload(fileHash, std::vector<Endpoint>{source2}));
  std::vector<Endpoint> packedSources;
  REQUIRE(
      parsePackedFoundSourcesPayloads(packedSources, packedFound, fileHash));
  REQUIRE_EQ((size_t)1, packedSources.size());
  REQUIRE_EQ(std::string("5.6.7.8"), packedSources[0].host);
  REQUIRE_EQ((uint16_t)4662, packedSources[0].port);
  std::vector<FoundSource> packedFoundSources;
  REQUIRE(parsePackedFoundSourcesPayloads(packedFoundSources, packedFound,
                                          fileHash));
  REQUIRE_EQ((size_t)1, packedFoundSources.size());
  REQUIRE_EQ((uint32_t)0x08070605, packedFoundSources[0].clientId);
  REQUIRE(!packedFoundSources[0].lowId);
  auto packedWithBadTail =
      createFoundSourcesPayload(fileHash, std::vector<Endpoint>{source}) +
      createDatagram(PROTO_EDONKEY, OP_GLOBFOUNDSOURCES,
                     createFoundSourcesPayload(
                         clientHash, std::vector<Endpoint>{source2})) +
      std::string("\xe3\x90", 2);
  REQUIRE(parsePackedFoundSourcesPayloads(packedFoundSources, packedWithBadTail,
                                          fileHash));
  REQUIRE_EQ((size_t)1, packedFoundSources.size());
  REQUIRE_EQ((uint32_t)0x04030201, packedFoundSources[0].clientId);

  auto callbackRequest = createCallbackRequestPayload(120);
  REQUIRE_EQ(std::string("78000000"), util::toHex(callbackRequest));
  Endpoint callbackEndpoint;
  REQUIRE(parseCallbackRequestIncomingPayload(
      callbackEndpoint, packUInt32(0x04030201) + packUInt16(4662)));
  REQUIRE_EQ(std::string("1.2.3.4"), callbackEndpoint.host);
  REQUIRE_EQ((uint16_t)4662, callbackEndpoint.port);
  REQUIRE(parseCallbackRequestIncomingPayload(
      callbackEndpoint, packUInt32(0x04030201) + packUInt16(4662) +
                            std::string(1, '\x83') + clientHash));
  REQUIRE_EQ(std::string("1.2.3.4"), callbackEndpoint.host);
  REQUIRE_EQ((uint16_t)4662, callbackEndpoint.port);
  REQUIRE_EQ((uint16_t)0x83, callbackEndpoint.cryptOptions);
  REQUIRE_EQ(clientHash, callbackEndpoint.userHash);
  REQUIRE(parseCallbackRequestIncomingPayload(
      callbackEndpoint, packUInt32(0x04030201) + packUInt16(4662) +
                            std::string(1, '\x83') + clientHash +
                            std::string("ignored")));

  std::vector<bool> bitfield;
  bitfield.push_back(true);
  bitfield.push_back(false);
  bitfield.push_back(true);
  auto status = createFileStatusPayload(fileHash, bitfield);
  REQUIRE_EQ((uint8_t)0x05, static_cast<uint8_t>(status[18]));
  std::vector<bool> parsedBitfield;
  REQUIRE(parseFileStatusPayload(parsedBitfield, status, fileHash));
  REQUIRE_EQ((size_t)3, parsedBitfield.size());
  REQUIRE(parsedBitfield[0]);
  REQUIRE(!parsedBitfield[1]);
  REQUIRE(parsedBitfield[2]);

  std::vector<PartRange> ranges;
  PartRange range;
  range.begin = 0;
  range.end = 10;
  ranges.push_back(range);
  range.begin = 20;
  range.end = 30;
  ranges.push_back(range);
  auto requestParts = createRequestPartsPayload(fileHash, ranges, false);
  REQUIRE_EQ((size_t)40, requestParts.size());
  REQUIRE_EQ((uint32_t)0, readUInt32(requestParts.data() + 16));
  REQUIRE_EQ((uint32_t)20, readUInt32(requestParts.data() + 20));
  REQUIRE_EQ((uint32_t)0, readUInt32(requestParts.data() + 24));
  REQUIRE_EQ((uint32_t)10, readUInt32(requestParts.data() + 28));
  REQUIRE_EQ((uint32_t)30, readUInt32(requestParts.data() + 32));
  REQUIRE_EQ((uint32_t)0, readUInt32(requestParts.data() + 36));

  auto requestParts64 = createRequestPartsPayload(fileHash, ranges, true);
  REQUIRE_EQ((size_t)64, requestParts64.size());
  REQUIRE_EQ((uint64_t)0, readUInt64(requestParts64.data() + 16));
  REQUIRE_EQ((uint64_t)20, readUInt64(requestParts64.data() + 24));
  REQUIRE_EQ((uint64_t)0, readUInt64(requestParts64.data() + 32));
  REQUIRE_EQ((uint64_t)10, readUInt64(requestParts64.data() + 40));
  REQUIRE_EQ((uint64_t)30, readUInt64(requestParts64.data() + 48));
  REQUIRE_EQ((uint64_t)0, readUInt64(requestParts64.data() + 56));

  range.begin = 0x100000000LL;
  range.end = 0x100000100LL;
  REQUIRE_THROWS_AS(createRequestPartsPayload(
                        fileHash, std::vector<PartRange>(1, range), false),
                    DlAbortEx);
  REQUIRE_NOTHROW(createRequestPartsPayload(
      fileHash, std::vector<PartRange>(1, range), true));
}

TEST_CASE("Ed2kHelperTest.testKadKeywordTarget")
{
  REQUIRE_EQ(std::string("oxymoronaccelerator"),
             pickKadKeyword("The oxymoronaccelerator 2"));
  REQUIRE_EQ(std::string("ubuntu"), pickKadKeyword("Ubuntu-22.04 ISO"));
  REQUIRE_EQ(std::string(), pickKadKeyword("a 12 ()"));

  auto target = createKadKeywordTarget("The oxymoronaccelerator 2");
  REQUIRE_EQ(std::string("bfdc1e49ecaa72c4f57ed35998a5a40d"),
             util::toHex(target));
}

TEST_CASE("Ed2kHelperTest.testSourceExchange2Payloads")
{
  std::string fileHashHex("0123456789abcdef0123456789abcdef");
  auto fileHash = util::fromHex(fileHashHex.begin(), fileHashHex.end());
  std::string userHashHex("11111111111111111111111111111111");
  auto userHash = util::fromHex(userHashHex.begin(), userHashHex.end());

  auto request = createRequestSources2Payload(fileHash);
  REQUIRE_EQ((size_t)19, request.size());
  REQUIRE_EQ((uint8_t)4, static_cast<uint8_t>(request[0]));
  REQUIRE_EQ((uint16_t)0, readUInt16(request.data() + 1));
  REQUIRE_EQ(fileHash, request.substr(3, HASH_LENGTH));
  uint8_t requestVersion = 0;
  REQUIRE(parseRequestSources2Payload(requestVersion, fileHash, fileHash));
  REQUIRE_EQ((uint8_t)1, requestVersion);
  REQUIRE(parseRequestSources2Payload(requestVersion, request, fileHash));
  REQUIRE_EQ((uint8_t)4, requestVersion);

  EmulePeerInfo sx2Peer;
  sx2Peer.miscOptions2.supportsSourceExchange2 = true;
  auto selectedRequest = createRequestSourcesPayload(fileHash, sx2Peer);
  REQUIRE_EQ((uint8_t)OP_REQUESTSOURCES2, selectedRequest.opcode);
  REQUIRE_EQ(request, selectedRequest.payload);

  EmulePeerInfo sx1Peer;
  sx1Peer.miscOptions.sourceExchange1Version = 3;
  selectedRequest = createRequestSourcesPayload(fileHash, sx1Peer);
  REQUIRE_EQ((uint8_t)OP_REQUESTSOURCES, selectedRequest.opcode);
  REQUIRE_EQ(fileHash, selectedRequest.payload);

  sx1Peer.miscOptions.sourceExchange1Version = 1;
  selectedRequest = createRequestSourcesPayload(fileHash, sx1Peer);
  REQUIRE_EQ((uint8_t)0, selectedRequest.opcode);
  REQUIRE(selectedRequest.payload.empty());

  selectedRequest = createRequestSourcesPayload(fileHash, EmulePeerInfo());
  REQUIRE_EQ((uint8_t)0, selectedRequest.opcode);
  REQUIRE(selectedRequest.payload.empty());

  SourceExchangeEntry entry;
  entry.endpoint.host = "203.0.113.9";
  entry.endpoint.port = 4662;
  entry.server.host = "198.51.100.2";
  entry.server.port = 4661;
  entry.userHash = userHash;
  entry.cryptOptions = 0x83;
  std::vector<SourceExchangeEntry> entries{entry};

  auto answer = createAnswerSources2Payload(fileHash, entries);
  SourceExchangeAnswer parsed;
  REQUIRE(parseAnswerSources2Payload(parsed, answer, fileHash));
  REQUIRE_EQ((uint8_t)4, parsed.version);
  REQUIRE_EQ((size_t)1, parsed.entries.size());
  REQUIRE_EQ(std::string("203.0.113.9"), parsed.entries[0].endpoint.host);
  REQUIRE_EQ((uint16_t)4662, parsed.entries[0].endpoint.port);
  REQUIRE_EQ(std::string("198.51.100.2"), parsed.entries[0].server.host);
  REQUIRE_EQ((uint16_t)4661, parsed.entries[0].server.port);
  REQUIRE_EQ(userHash, parsed.entries[0].userHash);
  REQUIRE_EQ((uint8_t)0x83, parsed.entries[0].cryptOptions);

  auto sx1 = createAnswerSourcesPayload(fileHash, 1, entries);
  REQUIRE(parseAnswerSourcesPayload(parsed, sx1, fileHash, 1));
  REQUIRE_EQ((uint8_t)1, parsed.version);
  REQUIRE_EQ((size_t)1, parsed.entries.size());
  REQUIRE_EQ(std::string("203.0.113.9"), parsed.entries[0].endpoint.host);
  REQUIRE(parsed.entries[0].userHash.empty());

  auto sx4 = createAnswerSourcesPayload(fileHash, 4, entries);
  REQUIRE(parseAnswerSourcesPayload(parsed, sx4, fileHash, 4));
  REQUIRE_EQ((uint8_t)4, parsed.version);
  REQUIRE_EQ(userHash, parsed.entries[0].userHash);
  REQUIRE_EQ((uint8_t)0x83, parsed.entries[0].cryptOptions);
}

TEST_CASE("Ed2kHelperTest.testMultipacketPayloads")
{
  std::string fileHashHex("0123456789abcdef0123456789abcdef");
  auto fileHash = util::fromHex(fileHashHex.begin(), fileHashHex.end());
  std::string aichRootHex("1111111111111111111111111111111111111111");
  auto aichRoot = util::fromHex(aichRootHex.begin(), aichRootHex.end());
  std::vector<bool> localParts{true, false};
  EmulePeerInfo remoteInfo;
  remoteInfo.miscOptions.aichVersion = 1;
  remoteInfo.miscOptions.sourceExchange1Version = 3;
  remoteInfo.miscOptions.extendedRequestsVersion = 2;
  remoteInfo.miscOptions.multiPacket = true;
  remoteInfo.miscOptions2.supportsSourceExchange2 = true;
  remoteInfo.miscOptions2.supportsExtendedMultipacket = true;

  auto request = createMultipacketFileRequestPayload(
      fileHash, ed2k::PIECE_LENGTH + 1, localParts, remoteInfo, true);

  REQUIRE_EQ(fileHash, request.substr(0, HASH_LENGTH));
  REQUIRE_EQ(static_cast<uint64_t>(ed2k::PIECE_LENGTH + 1),
             readUInt64(request.data() + HASH_LENGTH));
  size_t offset = HASH_LENGTH + 8;
  REQUIRE_EQ((uint8_t)OP_REQUESTFILENAME,
             static_cast<uint8_t>(request[offset++]));
  REQUIRE_EQ((uint16_t)2, readUInt16(request.data() + offset));
  offset += 2;
  REQUIRE_EQ((uint8_t)0x01, static_cast<uint8_t>(request[offset++]));
  REQUIRE_EQ((uint16_t)0, readUInt16(request.data() + offset));
  offset += 2;
  REQUIRE_EQ((uint8_t)OP_SETREQFILEID, static_cast<uint8_t>(request[offset++]));
  REQUIRE_EQ((uint8_t)OP_REQUESTSOURCES2,
             static_cast<uint8_t>(request[offset++]));
  REQUIRE_EQ((uint8_t)SOURCE_EXCHANGE2_VERSION,
             static_cast<uint8_t>(request[offset++]));
  REQUIRE_EQ((uint16_t)0, readUInt16(request.data() + offset));
  offset += 2;
  REQUIRE_EQ((uint8_t)OP_AICHFILEHASHREQ,
             static_cast<uint8_t>(request[offset++]));
  REQUIRE_EQ(request.size(), offset);

  auto answerPayload = fileHash;
  answerPayload.push_back(static_cast<char>(OP_REQFILENAMEANSWER));
  answerPayload += packUInt16(8);
  answerPayload += "test.iso";
  answerPayload.push_back(static_cast<char>(OP_FILESTATUS));
  answerPayload += packUInt16(2);
  answerPayload.push_back(static_cast<char>(0x03));
  answerPayload.push_back(static_cast<char>(OP_AICHFILEHASHANS));
  answerPayload += aichRoot;

  MultipacketAnswer answer;
  REQUIRE(parseMultipacketAnswerPayload(answer, answerPayload, fileHash));
  REQUIRE_EQ(std::string("test.iso"), answer.fileName);
  REQUIRE(answer.hasFileStatus);
  REQUIRE_EQ((size_t)2, answer.partStatus.size());
  REQUIRE(answer.partStatus[0]);
  REQUIRE(answer.partStatus[1]);
  REQUIRE(answer.hasAichRootHash);
  REQUIRE_EQ(aichRoot, answer.aichRootHash);

  std::vector<bool> completeStatus;
  REQUIRE(parseFileStatusPayload(completeStatus, fileHash + packUInt16(0),
                                 fileHash, 2));
  REQUIRE_EQ((size_t)2, completeStatus.size());
  REQUIRE(completeStatus[0]);
  REQUIRE(completeStatus[1]);
}

TEST_CASE("Ed2kHelperTest.testEmuleInfoPayload")
{
  EmulePeerInfo info;
  info.version = 0x3c;
  info.protocolVersion = 0x01;
  info.miscOptions.aichVersion = 2;
  info.miscOptions.unicode = true;
  info.miscOptions.udpVersion = 4;
  info.miscOptions.dataCompressionVersion = 1;
  info.miscOptions.sourceExchange1Version = 3;
  info.miscOptions.extendedRequestsVersion = 2;
  info.miscOptions.multiPacket = true;
  info.miscOptions2.supportsSourceExchange2 = true;
  info.miscOptions2.supportsLargeFiles = true;
  info.udpPort = 4672;

  auto payload = createEmuleInfoPayload(info);
  std::vector<Tag> muleTags;
  REQUIRE(parseTagList(muleTags, payload.substr(2)));
  auto hasUintTag = [&](uint8_t id) {
    return std::find_if(muleTags.begin(), muleTags.end(), [&](const Tag& tag) {
             return tag.id == id && tag.valueType == TagValueType::UINT;
           }) != muleTags.end();
  };
  auto hasStringTag = [&](uint8_t id) {
    return std::find_if(muleTags.begin(), muleTags.end(), [&](const Tag& tag) {
             return tag.id == id && tag.valueType == TagValueType::STRING;
           }) != muleTags.end();
  };
  REQUIRE(hasUintTag(0x20));
  REQUIRE(hasUintTag(0x21));
  REQUIRE(hasUintTag(0x22));
  REQUIRE(hasUintTag(0x23));
  REQUIRE(hasUintTag(0x24));
  REQUIRE(hasUintTag(0x25));
  REQUIRE(hasUintTag(0x26));
  REQUIRE(hasUintTag(0x27));
  REQUIRE(hasStringTag(0x55));
  REQUIRE(!hasUintTag(0xfb));
  REQUIRE(!hasUintTag(0xfa));
  REQUIRE(!hasUintTag(0xfe));

  EmulePeerInfo parsed;
  REQUIRE(parseEmuleInfoPayload(parsed, payload));
  REQUIRE_EQ((uint8_t)0x3c, parsed.version);
  REQUIRE_EQ((uint8_t)0x01, parsed.protocolVersion);
  REQUIRE_EQ((uint16_t)4672, parsed.udpPort);
  REQUIRE_EQ((uint8_t)0, parsed.miscOptions.aichVersion);
  REQUIRE(!parsed.miscOptions.unicode);
  REQUIRE_EQ((uint8_t)1, parsed.miscOptions.dataCompressionVersion);
  REQUIRE_EQ((uint8_t)3, parsed.miscOptions.sourceExchange1Version);
  REQUIRE_EQ((uint8_t)2, parsed.miscOptions.extendedRequestsVersion);
  REQUIRE(!parsed.miscOptions.multiPacket);
  REQUIRE(!parsed.miscOptions2.supportsSourceExchange2);
  REQUIRE(!parsed.miscOptions2.supportsLargeFiles);

  std::string remotePayload;
  remotePayload.push_back(static_cast<char>(0x3c));
  remotePayload.push_back(static_cast<char>(0x01));
  remotePayload += packUInt32(2);
  remotePayload += createUInt32Tag(0x21, 4672);
  remotePayload += createUInt32Tag(0x22, 4);
  REQUIRE(parseEmuleInfoPayload(parsed, remotePayload));
  REQUIRE_EQ((uint16_t)4672, parsed.udpPort);
  REQUIRE_EQ((uint8_t)4, parsed.miscOptions.udpVersion);
}

TEST_CASE("Ed2kHelperTest.testLocalEmulePeerInfoCapabilities")
{
  auto info = createLocalEmulePeerInfo();

  REQUIRE_EQ((uint8_t)1, info.miscOptions.aichVersion);
  REQUIRE(info.miscOptions.unicode);
  REQUIRE_EQ((uint8_t)1, info.miscOptions.dataCompressionVersion);
  REQUIRE_EQ((uint8_t)3, info.miscOptions.sourceExchange1Version);
  REQUIRE_EQ((uint8_t)2, info.miscOptions.extendedRequestsVersion);
  REQUIRE(info.miscOptions2.supportsLargeFiles);
  REQUIRE(info.miscOptions2.supportsSourceExchange2);
  REQUIRE_EQ((uint8_t)0, info.miscOptions.secureIdentVersion);
  REQUIRE(info.miscOptions.multiPacket);
  REQUIRE(info.miscOptions2.supportsExtendedMultipacket);
}

TEST_CASE("Ed2kHelperTest.testPeerHelloPayload")
{
  std::string clientHashHex("0123456789abcdef0123456789abcdef");
  auto clientHash = util::fromHex(clientHashHex.begin(), clientHashHex.end());
  EmulePeerInfo info;
  info.version = 0x47;
  info.miscOptions.aichVersion = 1;
  info.miscOptions.unicode = true;
  info.miscOptions.udpVersion = 4;
  info.miscOptions.dataCompressionVersion = 1;
  info.miscOptions.sourceExchange1Version = 3;
  info.miscOptions.extendedRequestsVersion = 2;
  info.miscOptions.multiPacket = true;
  info.miscOptions2.supportsLargeFiles = true;
  info.miscOptions2.supportsExtendedMultipacket = true;
  info.miscOptions2.supportsSourceExchange2 = true;
  info.udpPort = 4672;

  Endpoint server;
  server.host = "1.2.3.4";
  server.port = 4661;
  auto payload = createPeerHelloPayload(clientHash, 0x0a000001, 4662, server,
                                        "aria2-next", info, true);
  REQUIRE_EQ((uint8_t)HASH_LENGTH, static_cast<uint8_t>(payload[0]));
  REQUIRE_EQ(clientHash, payload.substr(1, HASH_LENGTH));
  REQUIRE_EQ((uint32_t)0x0a000001,
             readUInt32(payload.data() + 1 + HASH_LENGTH));
  REQUIRE_EQ((uint16_t)4662, readUInt16(payload.data() + 1 + HASH_LENGTH + 4));

  const auto tagOffset = 1 + HASH_LENGTH + 4 + 2;
  std::vector<Tag> tags;
  REQUIRE(parseTagList(
      tags, payload.substr(tagOffset, payload.size() - tagOffset - 6)));
  auto hasUintTag = [&](uint8_t id) {
    return std::find_if(tags.begin(), tags.end(), [&](const Tag& tag) {
             return tag.id == id && tag.valueType == TagValueType::UINT;
           }) != tags.end();
  };
  REQUIRE(hasUintTag(0x11));
  REQUIRE(hasUintTag(0xef));
  REQUIRE(hasUintTag(0xf9));
  REQUIRE(hasUintTag(0xfa));
  REQUIRE(hasUintTag(0xfb));
  REQUIRE(hasUintTag(0xfe));
  REQUIRE_EQ(std::string("010203043512"),
             util::toHex(payload.substr(payload.size() - 6)));

  EmulePeerInfo parsed;
  REQUIRE(parsePeerHelloPayload(parsed, payload, true));
  REQUIRE_EQ(clientHash, parsed.userHash);
  REQUIRE_EQ((uint16_t)4672, parsed.udpPort);
  REQUIRE_EQ((uint8_t)4, parsed.miscOptions.udpVersion);
  REQUIRE_EQ((uint8_t)1, parsed.miscOptions.aichVersion);
  REQUIRE(parsed.miscOptions.unicode);
  REQUIRE_EQ((uint8_t)1, parsed.miscOptions.dataCompressionVersion);
  REQUIRE_EQ((uint8_t)3, parsed.miscOptions.sourceExchange1Version);
  REQUIRE_EQ((uint8_t)2, parsed.miscOptions.extendedRequestsVersion);
  REQUIRE(parsed.miscOptions.multiPacket);
  REQUIRE(parsed.miscOptions2.supportsLargeFiles);
  REQUIRE(parsed.miscOptions2.supportsExtendedMultipacket);
  REQUIRE(parsed.miscOptions2.supportsSourceExchange2);
}

TEST_CASE("Ed2kHelperTest.testUdpReaskPayloads")
{
  std::string fileHashHex("0123456789abcdef0123456789abcdef");
  auto fileHash = util::fromHex(fileHashHex.begin(), fileHashHex.end());
  auto ping = createUdpReaskFilePingPayload(fileHash, 7);
  UdpReask reask;
  REQUIRE(parseUdpReaskFilePingPayload(reask, ping));
  REQUIRE_EQ(fileHash, reask.fileHash);
  REQUIRE(reask.hasCompleteSources);
  REQUIRE_EQ((uint16_t)7, reask.completeSources);
  REQUIRE(parseUdpReaskFilePingPayload(reask, fileHash));
  REQUIRE(!reask.hasCompleteSources);

  auto ackPayload =
      createUdpReaskAckPayload(std::vector<bool>{true, false, true}, 42);
  UdpReaskAck ack;
  REQUIRE(parseUdpReaskAckPayload(ack, ackPayload));
  REQUIRE_EQ((size_t)3, ack.bitfield.size());
  REQUIRE(ack.bitfield[0]);
  REQUIRE(!ack.bitfield[1]);
  REQUIRE(ack.bitfield[2]);
  REQUIRE_EQ((uint16_t)42, ack.rank);
  REQUIRE(parseUdpReaskAckPayload(ack, createUdpReaskAckPayload(3)));
  REQUIRE(ack.bitfield.empty());
  REQUIRE_EQ((uint16_t)3, ack.rank);
  REQUIRE(!parseUdpReaskAckPayload(ack, std::string(3, '\0')));
}

} // namespace aria2::ed2k
