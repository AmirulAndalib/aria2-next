/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
#include <cstdint>
#include <iterator>
#include <string>
#include <vector>
#include "ed2k_aich.h"
#include "ed2k_constants.h"
#include "ed2k_hash.h"
#include "ed2k_kad.h"
#include "ed2k_kad_search.h"
#include "ed2k_link.h"
#include "ed2k_packet.h"
#include "ed2k_endpoint.h"
#include "ed2k_crypto.h"

#include <algorithm>
#include "a2doctest.h"
#include <cstring>

#include "support/Encoding.h"

namespace aria2::ed2k {

TEST_CASE("Ed2kHelperTest.testKadPacketPayloads")
{
  std::string nodeIdHex("0123456789abcdef0123456789abcdef");
  auto nodeId = util::fromHex(nodeIdHex.begin(), nodeIdHex.end());
  KadContact contact;
  contact.id = nodeId;
  contact.host = "203.0.113.9";
  contact.udpPort = 4672;
  contact.tcpPort = 4662;
  contact.version = 5;

  auto hello = createKadHelloPayload(nodeId, 4662, 5);
  KadHello parsedHello;
  REQUIRE(parseKadHelloPayload(parsedHello, hello));
  REQUIRE_EQ(nodeId, parsedHello.id);
  REQUIRE_EQ((uint16_t)4662, parsedHello.tcpPort);
  REQUIRE_EQ((uint8_t)5, parsedHello.version);
  auto verifiedHello = createKadHelloPayload(nodeId, 4662, 8, true, true, true);
  REQUIRE(parseKadHelloPayload(parsedHello, verifiedHello));
  REQUIRE(parsedHello.requestsAck);
  REQUIRE(parsedHello.tcpFirewalled);
  REQUIRE(parsedHello.udpFirewalled);
  std::string ackId;
  REQUIRE(parseKadHelloAckPayload(ackId, createKadHelloAckPayload(nodeId)));
  REQUIRE_EQ(nodeId, ackId);

  auto bootstrap = createKadBootstrapResponsePayload(
      nodeId, 4662, 5, std::vector<KadContact>{contact});
  KadBootstrapResponse parsedBootstrap;
  REQUIRE(parseKadBootstrapResponsePayload(parsedBootstrap, bootstrap));
  REQUIRE_EQ(nodeId, parsedBootstrap.id);
  REQUIRE_EQ((uint16_t)4662, parsedBootstrap.tcpPort);
  REQUIRE_EQ((uint8_t)5, parsedBootstrap.version);
  REQUIRE_EQ((size_t)1, parsedBootstrap.contacts.size());
  REQUIRE_EQ(std::string("0123456789abcdef0123456789abcdef097100cb"
                         "4012361205"),
             util::toHex(bootstrap.substr(HASH_LENGTH + 5)));
  REQUIRE_EQ(std::string("203.0.113.9"), parsedBootstrap.contacts[0].host);
  REQUIRE_EQ((uint16_t)4672, parsedBootstrap.contacts[0].udpPort);
  REQUIRE_EQ((uint16_t)4662, parsedBootstrap.contacts[0].tcpPort);

  auto req = createKadRequestPayload(KAD_FIND_NODE, nodeId, nodeId);
  KadRequest parsedReq;
  REQUIRE(parseKadRequestPayload(parsedReq, req));
  REQUIRE_EQ((uint8_t)KAD_FIND_NODE, parsedReq.searchType);
  REQUIRE_EQ(nodeId, parsedReq.targetId);
  REQUIRE_EQ(nodeId, parsedReq.receiverId);

  auto res = createKadResponsePayload(nodeId, std::vector<KadContact>{contact});
  KadResponse parsedRes;
  REQUIRE(parseKadResponsePayload(parsedRes, res));
  REQUIRE_EQ(nodeId, parsedRes.targetId);
  REQUIRE_EQ((size_t)1, parsedRes.contacts.size());
  REQUIRE_EQ(std::string("0123456789abcdef0123456789abcdef01"
                         "0123456789abcdef0123456789abcdef097100cb"
                         "4012361205"),
             util::toHex(res));
  REQUIRE_EQ(std::string("203.0.113.9"), parsedRes.contacts[0].host);
}

TEST_CASE("Ed2kHelperTest.testKadUInt128ConversionMatchesAMule")
{
  const auto fileHash =
      util::fromHex(std::begin("2aab7f0cd4be378d9113557b1d24d8d0"),
                    std::end("2aab7f0cd4be378d9113557b1d24d8d0") - 1);
  const auto kadId = ed2kHashToKadId(fileHash);

  REQUIRE_EQ(std::string("0c7fab2a8d37bed47b551391d0d8241d"),
             util::toHex(kadId));
  REQUIRE_EQ(fileHash, kadIdToEd2kHash(kadId));
}

TEST_CASE("Ed2kHelperTest.testKadDirectCallbackPayload")
{
  const auto userHash = std::string(16, '\x42');

  auto payload = createDirectCallbackRequestPayload(4662, userHash, 0x03);

  REQUIRE_EQ((size_t)19, payload.size());
  REQUIRE_EQ((uint16_t)4662, readUInt16(payload.data()));
  REQUIRE_EQ(userHash, payload.substr(2, HASH_LENGTH));
  REQUIRE_EQ(static_cast<char>(0x03), payload[18]);

  DirectCallbackRequest request;
  REQUIRE(parseDirectCallbackRequestPayload(request, payload));
  REQUIRE_EQ((uint16_t)4662, request.tcpPort);
  REQUIRE_EQ(userHash, request.userHash);
  REQUIRE_EQ((uint8_t)0x03, request.connectOptions);

  REQUIRE(!parseDirectCallbackRequestPayload(request, payload.substr(0, 18)));
}

TEST_CASE("Ed2kHelperTest.testKadBuddyCallbackPayload")
{
  const auto buddyId = std::string(16, '\x31');
  const auto fileId = std::string(16, '\x42');
  Endpoint source;
  source.host = "203.0.113.44";
  source.port = 4662;

  auto payload = createBuddyCallbackPayload(buddyId, fileId, source);

  REQUIRE_EQ((size_t)38, payload.size());
  REQUIRE_EQ(buddyId, payload.substr(0, HASH_LENGTH));
  REQUIRE_EQ(fileId, payload.substr(HASH_LENGTH, HASH_LENGTH));
  REQUIRE_EQ(std::string("cb00712c3612"),
             util::toHex(payload.substr(HASH_LENGTH * 2)));

  BuddyCallback callback;
  REQUIRE(parseBuddyCallbackPayload(callback, payload));
  REQUIRE_EQ(buddyId, callback.buddyId);
  REQUIRE_EQ(fileId, callback.fileId);
  REQUIRE_EQ(source.host, callback.endpoint.host);
  REQUIRE_EQ(source.port, callback.endpoint.port);
  REQUIRE(!parseBuddyCallbackPayload(callback, payload.substr(0, 37)));
}

TEST_CASE("Ed2kHelperTest.testKadObfuscatedPacketRoundTrip")
{
  std::string nodeIdHex("0123456789abcdef0123456789abcdef");
  auto nodeId = util::fromHex(nodeIdHex.begin(), nodeIdHex.end());
  auto datagram =
      createDatagram(KAD_PROTOCOL, KAD_BOOTSTRAP_REQ, std::string());

  auto obfuscated = createKadObfuscatedDatagram(datagram, nodeId, 0x1234);

  REQUIRE_EQ((size_t)18, obfuscated.size());
  REQUIRE(static_cast<uint8_t>(obfuscated[0]) != KAD_PROTOCOL);
  REQUIRE_EQ(std::string("3412"), util::toHex(obfuscated.substr(1, 2)));
  REQUIRE_EQ(std::string("0834123bfb9093bc2416f3250b68702029b8"),
             util::toHex(obfuscated));

  KadObfuscatedDatagram parsed;
  REQUIRE(parseKadObfuscatedDatagram(parsed, obfuscated, nodeId));
  REQUIRE_EQ((uint32_t)0, parsed.receiverVerifyKey);
  REQUIRE_EQ((uint32_t)0, parsed.senderVerifyKey);
  REQUIRE_EQ(datagram, parsed.datagram);

  const auto amuleNodeKeyPacket =
      util::fromHex(std::begin("00fce06eda509dbfe1b8784806755e29e169"),
                    std::end("00fce06eda509dbfe1b8784806755e29e169") - 1);
  const auto amuleNodeId =
      util::fromHex(std::begin("4115b891e3b4fafa7e5116332d508752"),
                    std::end("4115b891e3b4fafa7e5116332d508752") - 1);
  REQUIRE(parseKadObfuscatedDatagram(parsed, amuleNodeKeyPacket, amuleNodeId));
  REQUIRE_EQ((uint32_t)0, parsed.receiverVerifyKey);
  REQUIRE_EQ((uint32_t)0x0c2bca82, parsed.senderVerifyKey);
  REQUIRE_EQ(createDatagram(KAD_PROTOCOL, 0x60, std::string()),
             parsed.datagram);

  auto verifyKeyPacket =
      createKadObfuscatedDatagram(datagram, 0x11223344, 0x55667788, 0x1234);
  REQUIRE_EQ(std::string("0a34124907d4afead3f790b245955cf96823"),
             util::toHex(verifyKeyPacket));
  REQUIRE(parseKadObfuscatedDatagram(parsed, verifyKeyPacket,
                                     (uint32_t)0x11223344));
  REQUIRE_EQ((uint32_t)0x11223344, parsed.receiverVerifyKey);
  REQUIRE_EQ((uint32_t)0x55667788, parsed.senderVerifyKey);
  REQUIRE_EQ(datagram, parsed.datagram);
  REQUIRE_EQ((uint32_t)0xc43989ee,
             createKadUdpVerifyKey(0x61726961, "203.0.113.9"));
}

TEST_CASE("Ed2kHelperTest.testServerUdpObfuscationRoundTrip")
{
  const auto datagram = createDatagram(PROTO_EDONKEY, OP_GLOBGETSOURCES,
                                       std::string(HASH_LENGTH, '\x42'));
  const auto encrypted = encryptServerUdpDatagram(datagram, 0x11223344, 0x5566);

  REQUIRE_EQ(datagram.size() + 8, encrypted.size());
  REQUIRE_EQ(std::string("6655"), util::toHex(encrypted.substr(1, 2)));
  REQUIRE(static_cast<uint8_t>(encrypted[0]) != PROTO_EDONKEY);

  REQUIRE_EQ(
      std::string("5566552d6483f8428ade01f79455b00981b4786e987a6d5b8c1c"),
      util::toHex(encrypted));

  const auto serverPacket = util::fromHex(
      std::begin("5566551b41314224bc0defec3483de4035cbe3dca1a17586f719"),
      std::end("5566551b41314224bc0defec3483de4035cbe3dca1a17586f719") - 1);
  std::string decrypted;
  REQUIRE(decryptServerUdpDatagram(decrypted, serverPacket, 0x11223344));
  REQUIRE_EQ(datagram, decrypted);
  REQUIRE(!decryptServerUdpDatagram(decrypted, serverPacket, 0x11223345));
}

TEST_CASE("Ed2kHelperTest.testPeerUdpObfuscationRoundTrip")
{
  const auto datagram = createDatagram(PROTO_EMULE, OP_REASKFILEPING,
                                       std::string(HASH_LENGTH, '\x42'));
  const auto userHash = std::string(HASH_LENGTH, '\x42');
  const auto encrypted =
      encryptPeerUdpDatagram(datagram, userHash, 0x097100cb, 0x5566);

  REQUIRE_EQ(
      std::string("55665523045d002279fbce1cd9463eea0fa05b20856711e25773"),
      util::toHex(encrypted));
  std::string decrypted;
  REQUIRE(decryptPeerUdpDatagram(decrypted, encrypted, userHash, 0x097100cb));
  REQUIRE_EQ(datagram, decrypted);
  REQUIRE(!decryptPeerUdpDatagram(decrypted, encrypted, userHash, 0x097100ca));
}

TEST_CASE("Ed2kHelperTest.testKadSearchPublishAndFirewallPayloads")
{
  std::string fileIdHex("0123456789abcdef0123456789abcdef");
  auto fileId = util::fromHex(fileIdHex.begin(), fileIdHex.end());
  std::string sourceIdHex("11111111111111111111111111111111");
  auto sourceId = util::fromHex(sourceIdHex.begin(), sourceIdHex.end());

  auto sourceReq = createKadSearchSourcesRequestPayload(fileId, 3, 123456789);
  KadSearchSourcesRequest parsedSourceReq;
  REQUIRE(parseKadSearchSourcesRequestPayload(parsedSourceReq, sourceReq));
  REQUIRE_EQ(fileId, parsedSourceReq.targetId);
  REQUIRE_EQ((uint16_t)3, parsedSourceReq.startPosition);
  REQUIRE_EQ((uint64_t)123456789, parsedSourceReq.size);

  auto keyReq = createKadSearchKeysRequestPayload(fileId, 7);
  REQUIRE_EQ((size_t)18, keyReq.size());
  REQUIRE_EQ((uint16_t)7, readUInt16(keyReq.data() + HASH_LENGTH));

  Endpoint source;
  source.host = "203.0.113.9";
  source.port = 4662;
  auto publish = createKadPublishSourceRequestPayload(fileId, source, sourceId);
  KadPublishSourceRequest parsedPublish;
  REQUIRE(parseKadPublishSourceRequestPayload(parsedPublish, publish));
  REQUIRE_EQ(fileId, parsedPublish.fileId);
  auto largePublish = createKadPublishSourceRequestPayload(
      fileId, source, sourceId, 0x100000001ULL, 4672,
      SOURCE_CRYPT_SUPPORT | SOURCE_CRYPT_REQUEST);
  REQUIRE(parseKadPublishSourceRequestPayload(parsedPublish, largePublish));
  auto sizeTag = std::find_if(parsedPublish.source.tags.begin(),
                              parsedPublish.source.tags.end(),
                              [](const Tag& tag) { return tag.id == 0xd3; });
  REQUIRE(sizeTag != parsedPublish.source.tags.end());
  REQUIRE_EQ((uint64_t)0x100000001ULL, sizeTag->intValue);
  auto sourceTypeTag = std::find_if(
      parsedPublish.source.tags.begin(), parsedPublish.source.tags.end(),
      [](const Tag& tag) { return tag.id == 0xff; });
  REQUIRE(sourceTypeTag != parsedPublish.source.tags.end());
  REQUIRE_EQ((uint64_t)4, sourceTypeTag->intValue);

  auto searchRes = createKadSearchResultPayload(
      sourceId, fileId, std::vector<KadSearchEntry>{parsedPublish.source});
  KadSearchResult result;
  REQUIRE(parseKadSearchResultPayload(result, searchRes));
  REQUIRE_EQ(sourceId, result.sourceId);
  REQUIRE_EQ(fileId, result.targetId);
  auto endpoints = extractKadSourceEndpoints(result);
  REQUIRE_EQ((size_t)1, endpoints.size());
  REQUIRE_EQ(std::string("203.0.113.9"), endpoints[0].host);
  REQUIRE_EQ((uint16_t)4662, endpoints[0].port);
  auto detailed = extractKadSourceEndpointDetails(result);
  REQUIRE_EQ((size_t)1, detailed.size());
  REQUIRE_EQ((uint16_t)4672, detailed[0].udpPort);
  REQUIRE_EQ((uint16_t)(SOURCE_CRYPT_SUPPORT | SOURCE_CRYPT_REQUEST),
             detailed[0].endpoint.cryptOptions);

  KadPublishResult publishResult;
  REQUIRE(parseKadPublishResultPayload(
      publishResult, createKadPublishResultPayload(fileId, 1)));
  REQUIRE_EQ(fileId, publishResult.fileId);
  REQUIRE_EQ((uint8_t)1, publishResult.count);

  KadFirewalledRequest fwReq;
  REQUIRE(parseKadFirewalledRequestPayload(
      fwReq, createKadFirewalledRequestPayload(4662, sourceId, 2)));
  REQUIRE_EQ((uint16_t)4662, fwReq.tcpPort);
  REQUIRE_EQ(sourceId, fwReq.id);
  REQUIRE_EQ((uint8_t)2, fwReq.options);

  KadFirewalledResponse fwRes;
  REQUIRE(parseKadFirewalledResponsePayload(
      fwRes, createKadFirewalledResponsePayload("203.0.113.9")));
  REQUIRE_EQ(std::string("203.0.113.9"), fwRes.ipAddress);

  KadCallbackRequest callbackReq;
  REQUIRE(parseKadCallbackRequestPayload(
      callbackReq, createKadCallbackRequestPayload(sourceId, fileId, 4662)));
  REQUIRE_EQ(sourceId, callbackReq.buddyId);
  REQUIRE_EQ(fileId, callbackReq.fileId);
  REQUIRE_EQ((uint16_t)4662, callbackReq.tcpPort);

  KadFirewalledUdp fwUdp;
  REQUIRE(parseKadFirewalledUdpPayload(fwUdp,
                                       createKadFirewalledUdpPayload(1, 4662)));
  REQUIRE_EQ((uint8_t)1, fwUdp.errorCode);
  REQUIRE_EQ((uint16_t)4662, fwUdp.tcpPort);
}

} // namespace aria2::ed2k
