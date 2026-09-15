/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
#include "ed2k_kad_search.h"
#include <cstdint>
#include <memory>
#include "download_helper.h"

#include <string>
#include <cstdlib>
#include <vector>

#include "a2doctest.h"

#include "RequestGroup.h"
#include "DownloadEngine.h"
#include "DownloadContext.h"
#include "DefaultPieceStorage.h"
#include "Ed2kAttribute.h"
#include "Ed2kKadCommand.h"
#include "Ed2kUploadQueue.h"
#include "Option.h"
#include "RequestGroupMan.h"
#include "SelectEventPoll.h"
#include "SegmentMan.h"
#include "ed2k_constants.h"
#include "ed2k_endpoint.h"
#include "ed2k_hash.h"
#include "ed2k_kad.h"
#include "ed2k_link.h"
#include "ed2k_packet.h"
#include "ed2k_peer.h"
#include "prefs.h"
#include "TestUtil.h"
#include "a2functional.h"
#ifdef ENABLE_BITTORRENT
#  include "BtDownload.h"
#endif // ENABLE_BITTORRENT

namespace aria2 {

TEST_CASE("DownloadHelperTest.testEd2kKadCommandQueuesDuePeerReask")
{
  auto option = std::make_shared<Option>();
  std::vector<std::string> uris{"ed2k://|file|aria2%20next.bin|9728001|"
                                "0123456789abcdef0123456789abcdef|/"};
  option->put(PREF_DIR, "/tmp");
  option->put(PREF_ED2K_SERVER, "203.0.113.10:4661");
  option->put(PREF_MAX_DOWNLOAD_LIMIT, "0");
  option->put(PREF_MAX_UPLOAD_LIMIT, "0");
  option->put(PREF_FILE_ALLOCATION, V_NONE);
  option->put(PREF_DRY_RUN, A2_V_TRUE);

  std::vector<std::shared_ptr<RequestGroup>> result;
  createRequestGroupForUri(result, option, uris);
  auto group = result[0];
  auto attrs = getEd2kAttrs(group->getDownloadContext());
  ed2k::Endpoint peer;
  peer.host = "203.0.113.20";
  peer.port = 4662;
  addEd2kPeer(attrs, peer, ed2k::PEER_SOURCE_SERVER);
  markEd2kPeerQueued(attrs, peer, 4, std::vector<bool>{true});
  auto state = getEd2kPeerState(attrs, peer);
  state->udpPort = 4672;
  state->udpVersion = 4;
  state->nextUdpReaskTime = 100;

  DownloadEngine engine(make_unique<SelectEventPoll>());
  engine.setOption(option.get());
  engine.setRequestGroupMan(make_unique<RequestGroupMan>(
      std::vector<std::shared_ptr<RequestGroup>>{group}, 1, option.get()));
  Ed2kKadCommand command(1, group.get(), &engine);

  REQUIRE_EQ((size_t)1, command.testQueueDuePeerReasks(200));
  REQUIRE_EQ((size_t)1, command.testQueuedPacketCount());
  const auto& item = command.testQueuedPacketAt(0);
  REQUIRE_EQ(std::string("203.0.113.20"), item.first.host);
  REQUIRE_EQ((uint16_t)4672, item.first.port);
  ed2k::PacketHeader header;
  REQUIRE(
      ed2k::readDatagramHeader(header, item.second.data(), item.second.size()));
  REQUIRE_EQ(ed2k::PROTO_EMULE, header.protocol);
  REQUIRE_EQ(ed2k::OP_REASKFILEPING, header.opcode);
  ed2k::UdpReask reask;
  REQUIRE(ed2k::parseUdpReaskFilePingPayload(reask, item.second.substr(2)));
  REQUIRE_EQ(attrs->link.hash, reask.fileHash);
  REQUIRE(state->udpReaskPending);
  REQUIRE_EQ((int64_t)200, state->lastUdpReaskTime);
  REQUIRE_EQ((int64_t)1500, state->nextUdpReaskTime);
}

TEST_CASE("DownloadHelperTest.testEd2kKadCommandQueuesKadCallback")
{
  auto option = std::make_shared<Option>();
  std::vector<std::string> uris{"ed2k://|file|aria2%20next.bin|9728001|"
                                "0123456789abcdef0123456789abcdef|/"};
  option->put(PREF_DIR, "/tmp");
  option->put(PREF_ED2K_SERVER, "203.0.113.10:4661");
  option->put(PREF_ED2K_LISTEN_PORT, "4662");
  option->put(PREF_MAX_DOWNLOAD_LIMIT, "0");
  option->put(PREF_MAX_UPLOAD_LIMIT, "0");
  option->put(PREF_FILE_ALLOCATION, V_NONE);
  option->put(PREF_DRY_RUN, A2_V_TRUE);

  std::vector<std::shared_ptr<RequestGroup>> result;
  createRequestGroupForUri(result, option, uris);
  auto group = result[0];
  auto attrs = getEd2kAttrs(group->getDownloadContext());
  ed2k::KadSourceEndpoint source;
  source.endpoint.host = "203.0.113.44";
  source.endpoint.port = 4662;
  source.endpoint.userHash = std::string(ed2k::HASH_LENGTH, '\x44');
  source.udpPort = 4672;
  source.sourceType = 3;
  source.buddyIp = ed2k::ipv4ToEndpointValue("203.0.113.99");
  source.buddyPort = 4672;
  source.buddyHash = std::string(ed2k::HASH_LENGTH, '\x55');
  addEd2kKadSourcePeer(attrs, source, ed2k::PEER_SOURCE_KAD);
  auto state = getEd2kPeerState(attrs, source.endpoint);

  DownloadEngine engine(make_unique<SelectEventPoll>());
  engine.setOption(option.get());
  engine.setRequestGroupMan(make_unique<RequestGroupMan>(
      std::vector<std::shared_ptr<RequestGroup>>{group}, 1, option.get()));
  Ed2kKadCommand command(1, group.get(), &engine);

  REQUIRE_EQ((size_t)1, command.testQueueDueKadCallbacks(200));
  REQUIRE_EQ((size_t)1, command.testQueuedPacketCount());
  const auto& item = command.testQueuedPacketAt(0);
  REQUIRE_EQ(std::string("203.0.113.99"), item.first.host);
  REQUIRE_EQ((uint16_t)4672, item.first.port);
  ed2k::PacketHeader header;
  REQUIRE(
      ed2k::readDatagramHeader(header, item.second.data(), item.second.size()));
  REQUIRE_EQ(ed2k::KAD_PROTOCOL, header.protocol);
  REQUIRE_EQ(ed2k::KAD_CALLBACK_REQ, header.opcode);
  ed2k::KadCallbackRequest request;
  REQUIRE(ed2k::parseKadCallbackRequestPayload(request, item.second.substr(2)));
  REQUIRE_EQ(ed2k::ed2kHashToKadId(source.buddyHash), request.buddyId);
  REQUIRE_EQ(ed2k::ed2kHashToKadId(attrs->link.hash), request.fileId);
  REQUIRE_EQ((uint16_t)4662, request.tcpPort);
  REQUIRE_EQ((int64_t)200, state->lastCallbackTime);
  REQUIRE_EQ((int64_t)245, state->callbackDeadline);
  REQUIRE_EQ((size_t)0, command.testQueueDueKadCallbacks(210));
}

TEST_CASE("DownloadHelperTest.testEd2kKadCommandQueuesDirectCallback")
{
  auto option = std::make_shared<Option>();
  std::vector<std::string> uris{"ed2k://|file|aria2%20next.bin|9728001|"
                                "0123456789abcdef0123456789abcdef|/"};
  option->put(PREF_DIR, "/tmp");
  option->put(PREF_ED2K_SERVER, "203.0.113.10:4661");
  option->put(PREF_ED2K_LISTEN_PORT, "4662");
  option->put(PREF_MAX_DOWNLOAD_LIMIT, "0");
  option->put(PREF_MAX_UPLOAD_LIMIT, "0");
  option->put(PREF_FILE_ALLOCATION, V_NONE);
  option->put(PREF_DRY_RUN, A2_V_TRUE);

  std::vector<std::shared_ptr<RequestGroup>> result;
  createRequestGroupForUri(result, option, uris);
  auto group = result[0];
  auto attrs = getEd2kAttrs(group->getDownloadContext());
  attrs->kadFirewalled = false;
  ed2k::KadSourceEndpoint source;
  source.endpoint.host = "203.0.113.44";
  source.endpoint.port = 4662;
  source.endpoint.userHash = std::string(ed2k::HASH_LENGTH, '\x44');
  source.endpoint.cryptOptions =
      ed2k::SOURCE_CRYPT_SUPPORT | ed2k::SOURCE_CRYPT_DIRECT_CALLBACK;
  source.udpPort = 4672;
  source.sourceType = 6;
  addEd2kKadSourcePeer(attrs, source, ed2k::PEER_SOURCE_KAD);
  auto state = getEd2kPeerState(attrs, source.endpoint);

  DownloadEngine engine(make_unique<SelectEventPoll>());
  engine.setOption(option.get());
  engine.setRequestGroupMan(make_unique<RequestGroupMan>(
      std::vector<std::shared_ptr<RequestGroup>>{group}, 1, option.get()));
  Ed2kKadCommand command(1, group.get(), &engine);

  REQUIRE_EQ((size_t)1, command.testQueueDueKadCallbacks(200));
  REQUIRE_EQ((size_t)1, command.testQueuedPacketCount());
  const auto& item = command.testQueuedPacketAt(0);
  REQUIRE_EQ(std::string("203.0.113.44"), item.first.host);
  REQUIRE_EQ((uint16_t)4672, item.first.port);
  ed2k::PacketHeader header;
  REQUIRE(
      ed2k::readDatagramHeader(header, item.second.data(), item.second.size()));
  REQUIRE_EQ(ed2k::PROTO_EMULE, header.protocol);
  REQUIRE_EQ(ed2k::OP_DIRECTCALLBACKREQ, header.opcode);
  ed2k::DirectCallbackRequest request;
  REQUIRE(
      ed2k::parseDirectCallbackRequestPayload(request, item.second.substr(2)));
  REQUIRE_EQ((uint16_t)4662, request.tcpPort);
  REQUIRE_EQ(attrs->clientHash, request.userHash);
  REQUIRE_EQ((uint8_t)(ed2k::SOURCE_CRYPT_SUPPORT | ed2k::SOURCE_CRYPT_REQUEST),
             request.connectOptions);
  REQUIRE_EQ((int64_t)200, state->lastCallbackTime);
  REQUIRE_EQ((int64_t)245, state->callbackDeadline);
  REQUIRE_EQ((size_t)0, command.testQueueDueKadCallbacks(210));
}

TEST_CASE(
    "DownloadHelperTest.testEd2kKadCommandUsesRuntimeTcpPortForDirectCallback")
{
  auto option = std::make_shared<Option>();
  std::vector<std::string> uris{"ed2k://|file|aria2%20next.bin|9728001|"
                                "0123456789abcdef0123456789abcdef|/"};
  option->put(PREF_DIR, "/tmp");
  option->put(PREF_ED2K_SERVER, "203.0.113.10:4661");
  option->put(PREF_ED2K_LISTEN_PORT, "0");
  option->put(PREF_MAX_DOWNLOAD_LIMIT, "0");
  option->put(PREF_MAX_UPLOAD_LIMIT, "0");
  option->put(PREF_FILE_ALLOCATION, V_NONE);
  option->put(PREF_DRY_RUN, A2_V_TRUE);

  std::vector<std::shared_ptr<RequestGroup>> result;
  createRequestGroupForUri(result, option, uris);
  auto group = result[0];
  auto attrs = getEd2kAttrs(group->getDownloadContext());
  attrs->kadFirewalled = false;
  ed2k::KadSourceEndpoint source;
  source.endpoint.host = "203.0.113.44";
  source.endpoint.port = 4662;
  source.endpoint.userHash = std::string(ed2k::HASH_LENGTH, '\x44');
  source.endpoint.cryptOptions =
      ed2k::SOURCE_CRYPT_SUPPORT | ed2k::SOURCE_CRYPT_DIRECT_CALLBACK;
  source.udpPort = 4672;
  source.sourceType = 6;
  addEd2kKadSourcePeer(attrs, source, ed2k::PEER_SOURCE_KAD);

  DownloadEngine engine(make_unique<SelectEventPoll>());
  engine.setOption(option.get());
  engine.setEd2kTcpPort(50265);
  engine.setRequestGroupMan(make_unique<RequestGroupMan>(
      std::vector<std::shared_ptr<RequestGroup>>{group}, 1, option.get()));
  Ed2kKadCommand command(1, group.get(), &engine);

  REQUIRE_EQ((size_t)1, command.testQueueDueKadCallbacks(200));
  const auto& item = command.testQueuedPacketAt(0);
  ed2k::DirectCallbackRequest request;
  REQUIRE(
      ed2k::parseDirectCallbackRequestPayload(request, item.second.substr(2)));
  REQUIRE_EQ((uint16_t)50265, request.tcpPort);
}

TEST_CASE("DownloadHelperTest.testEd2kKadCommandQueueFullForUnknownUploadReask")
{
  auto option = std::make_shared<Option>();
  std::vector<std::string> uris{"ed2k://|file|aria2%20next.bin|9728001|"
                                "0123456789abcdef0123456789abcdef|/"};
  option->put(PREF_DIR, "/tmp");
  option->put(PREF_ED2K_SERVER, "203.0.113.10:4661");
  option->put(PREF_MAX_DOWNLOAD_LIMIT, "0");
  option->put(PREF_MAX_UPLOAD_LIMIT, "0");
  option->put(PREF_FILE_ALLOCATION, V_NONE);
  option->put(PREF_DRY_RUN, A2_V_TRUE);

  std::vector<std::shared_ptr<RequestGroup>> result;
  createRequestGroupForUri(result, option, uris);
  auto group = result[0];
  auto attrs = getEd2kAttrs(group->getDownloadContext());

  DownloadEngine engine(make_unique<SelectEventPoll>());
  engine.setOption(option.get());
  engine.setRequestGroupMan(make_unique<RequestGroupMan>(
      std::vector<std::shared_ptr<RequestGroup>>{group}, 1, option.get()));
  Ed2kKadCommand command(1, group.get(), &engine);

  ed2k::Endpoint remote;
  remote.host = "203.0.113.30";
  remote.port = 4672;
  command.testHandleEd2kUdpPacket(
      remote, ed2k::OP_REASKFILEPING,
      ed2k::createUdpReaskFilePingPayload(attrs->link.hash));

  REQUIRE_EQ((size_t)1, command.testQueuedPacketCount());
  const auto& item = command.testQueuedPacketAt(0);
  REQUIRE_EQ(remote.host, item.first.host);
  REQUIRE_EQ(remote.port, item.first.port);
  ed2k::PacketHeader header;
  REQUIRE(
      ed2k::readDatagramHeader(header, item.second.data(), item.second.size()));
  REQUIRE_EQ(ed2k::PROTO_EMULE, header.protocol);
  REQUIRE_EQ(ed2k::OP_QUEUEFULL, header.opcode);
  REQUIRE_EQ((size_t)0, header.payloadSize());
}

TEST_CASE("DownloadHelperTest.testEd2kKadCommandAckForUploadingPeerReask")
{
  auto option = std::make_shared<Option>();
  std::vector<std::string> uris{"ed2k://|file|aria2%20next.bin|9728001|"
                                "0123456789abcdef0123456789abcdef|/"};
  option->put(PREF_DIR, "/tmp");
  option->put(PREF_ED2K_SERVER, "203.0.113.10:4661");
  option->put(PREF_MAX_DOWNLOAD_LIMIT, "0");
  option->put(PREF_MAX_UPLOAD_LIMIT, "0");
  option->put(PREF_FILE_ALLOCATION, V_NONE);
  option->put(PREF_DRY_RUN, A2_V_TRUE);

  std::vector<std::shared_ptr<RequestGroup>> result;
  createRequestGroupForUri(result, option, uris);
  auto group = result[0];
  auto attrs = getEd2kAttrs(group->getDownloadContext());

  DownloadEngine engine(make_unique<SelectEventPoll>());
  engine.setOption(option.get());
  engine.setRequestGroupMan(make_unique<RequestGroupMan>(
      std::vector<std::shared_ptr<RequestGroup>>{group}, 1, option.get()));
  auto uploadQueue = engine.getRequestGroupMan()->getEd2kUploadQueue();

  ed2k::Endpoint remote;
  remote.host = "203.0.113.31";
  remote.port = 4672;
  REQUIRE(uploadQueue->requestUpload(remote,
                                     std::string(ed2k::HASH_LENGTH, '\x44'),
                                     attrs->link.hash, 1000, nullptr));
  Ed2kKadCommand command(1, group.get(), &engine);
  command.testHandleEd2kUdpPacket(
      remote, ed2k::OP_REASKFILEPING,
      ed2k::createUdpReaskFilePingPayload(attrs->link.hash));

  REQUIRE_EQ((size_t)1, command.testQueuedPacketCount());
  const auto& item = command.testQueuedPacketAt(0);
  ed2k::PacketHeader header;
  REQUIRE(
      ed2k::readDatagramHeader(header, item.second.data(), item.second.size()));
  REQUIRE_EQ(ed2k::PROTO_EMULE, header.protocol);
  REQUIRE_EQ(ed2k::OP_REASKACK, header.opcode);
  ed2k::UdpReaskAck ack;
  REQUIRE(ed2k::parseUdpReaskAckPayload(ack, item.second.substr(2)));
  REQUIRE_EQ((uint16_t)0, ack.rank);
}

} // namespace aria2
