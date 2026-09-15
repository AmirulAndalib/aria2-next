/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
#include "a2netcompat.h"
#include <cstddef>
#include <cstdint>
#include <utility>
#include "CommandTestSupport.h"
#include "Ed2kCommand.h"

#include "a2doctest.h"
#include <memory>
#include <vector>

#include "DownloadContext.h"
#include "DownloadEngine.h"
#include "Ed2kAttribute.h"
#include "Ed2kKadCommand.h"
#include "Ed2kListenCommand.h"
#include "Ed2kSession.h"
#include "Ed2kSharingTimeSeedCriteria.h"
#include "SeedCheckCommand.h"
#include "RequestGroup.h"
#include "RequestGroupMan.h"
#include "SelectEventPoll.h"
#include "SocketCore.h"
#include "ed2k_constants.h"
#include "ed2k_kad.h"
#include "ed2k_kad_search.h"
#include "ed2k_link.h"
#include "ed2k_packet.h"
#include "a2functional.h"

namespace aria2 {

using namespace test::ed2k_command;

TEST_CASE("Ed2kCommandTest.testForceHaltDrainsIdleKadGroup")
{
  auto option = createOption();
  DownloadEngine engine(make_unique<SelectEventPoll>());
  engine.setOption(option.get());
  auto dctx = createEd2kContext();
  auto group = createRequestGroup(option, dctx);
  engine.setRequestGroupMan(make_unique<RequestGroupMan>(
      std::vector<std::shared_ptr<RequestGroup>>{group}, 5, option.get()));
  engine.getRequestGroupMan()->addRequestGroup(group);
  group->setRequestGroupMan(engine.getRequestGroupMan().get());

  engine.addCommand(
      make_unique<Ed2kKadCommand>(engine.newCUID(), group.get(), &engine));
  runEngineTicks(engine, 1);
  REQUIRE_EQ((int32_t)1, group->getNumCommand());

  engine.getRequestGroupMan()->clearQueueCheck();
  group->setForceHaltRequested(true, RequestGroup::USER_REQUEST);
  REQUIRE(!engine.getRequestGroupMan()->queueCheckRequested());
  runEngineTicks(engine, 1);
  REQUIRE_EQ((int32_t)0, group->getNumCommand());
  engine.getRequestGroupMan()->removeStoppedGroup(&engine);
  REQUIRE_EQ((size_t)0, engine.getRequestGroupMan()->countRequestGroup());
}

TEST_CASE("Ed2kCommandTest.testKadBootstrapSourceSearchAddsPeer")
{
  auto option = createOption();
  DownloadEngine engine(make_unique<SelectEventPoll>());
  engine.setOption(option.get());
  auto dctx = createEd2kContext();
  auto group = createRequestGroup(option, dctx);
  engine.setRequestGroupMan(make_unique<RequestGroupMan>(
      std::vector<std::shared_ptr<RequestGroup>>{group}, 5, option.get()));
  engine.getRequestGroupMan()->setKeepRunning(true);
  engine.getRequestGroupMan()->addRequestGroup(group);
  group->setRequestGroupMan(engine.getRequestGroupMan().get());

  SocketCore routerSocket(SOCK_DGRAM);
  routerSocket.bind("127.0.0.1", 0, AF_INET);
  routerSocket.setBlockingMode();
  auto routerAddr = routerSocket.getAddrInfo();

  auto attrs = getEd2kAttrs(dctx);
  const auto kadFileId = ed2k::ed2kHashToKadId(attrs->link.hash);
  attrs->kadRoutingTable =
      std::make_shared<ed2k::KadRoutingTable>(std::string(16, '\x55'));
  ed2k::Endpoint routerEndpoint;
  routerEndpoint.host = "127.0.0.1";
  routerEndpoint.port = routerAddr.port;
  attrs->kadRoutingTable->addRouterNode(routerEndpoint);

  auto command =
      make_unique<Ed2kKadCommand>(engine.newCUID(), group.get(), &engine);
  auto commandPtr = command.get();
  engine.addCommand(std::move(command));

  auto bootstrapReq = readDatagram(routerSocket, engine);
  const auto kadUdpPort = commandPtr->getLocalUdpPort();
  auto bootstrapReqHeader = datagramHeaderOf(bootstrapReq);
  REQUIRE_EQ(ed2k::KAD_PROTOCOL, bootstrapReqHeader.protocol);
  REQUIRE_EQ(ed2k::KAD_BOOTSTRAP_REQ, bootstrapReqHeader.opcode);

  const auto remoteId = std::string(16, '\x33');
  writeDatagram(
      routerSocket,
      ed2k::createDatagram(ed2k::KAD_PROTOCOL, ed2k::KAD_BOOTSTRAP_RES,
                           ed2k::createKadBootstrapResponsePayload(
                               remoteId, 4662, 8,
                               std::vector<ed2k::KadContact>{createKadContact(
                                   remoteId, routerAddr.port)})),
      kadUdpPort);
  runEngineTicks(engine, 1);

  auto kadReq = decodeKadDatagram(readDatagram(routerSocket, engine), remoteId);
  auto kadReqHeader = datagramHeaderOf(kadReq);
  REQUIRE_EQ(ed2k::KAD_PROTOCOL, kadReqHeader.protocol);
  REQUIRE_EQ(ed2k::KAD_REQ, kadReqHeader.opcode);
  ed2k::KadRequest parsedKadReq;
  REQUIRE(ed2k::parseKadRequestPayload(parsedKadReq, datagramBodyOf(kadReq)));
  REQUIRE_EQ((uint8_t)ed2k::KAD_FIND_VALUE, parsedKadReq.searchType);
  REQUIRE_EQ(kadFileId, parsedKadReq.targetId);

  writeDatagram(
      routerSocket,
      ed2k::createDatagram(ed2k::KAD_PROTOCOL, ed2k::KAD_RES,
                           ed2k::createKadResponsePayload(
                               kadFileId, std::vector<ed2k::KadContact>())),
      kadUdpPort);
  runEngineTicks(engine, 1);

  auto sourceSearchDatagram = readKadDatagramWithOpcode(
      routerSocket, engine, ed2k::KAD_SEARCH_SOURCES_REQ, remoteId);
  REQUIRE_EQ(kadUdpPort, sourceSearchDatagram.sender.port);
  auto sourceSearchHeader = datagramHeaderOf(sourceSearchDatagram.data);
  REQUIRE_EQ(ed2k::KAD_PROTOCOL, sourceSearchHeader.protocol);
  REQUIRE_EQ(ed2k::KAD_SEARCH_SOURCES_REQ, sourceSearchHeader.opcode);
  ed2k::KadSearchSourcesRequest searchRequest;
  REQUIRE(ed2k::parseKadSearchSourcesRequestPayload(
      searchRequest, datagramBodyOf(sourceSearchDatagram.data)));
  REQUIRE_EQ(kadFileId, searchRequest.targetId);

  ed2k::Endpoint source;
  source.host = "203.0.113.44";
  source.port = 4662;
  const auto sourceId = std::string(16, '\x44');
  ed2k::KadPublishSourceRequest publishRequest;
  REQUIRE(ed2k::parseKadPublishSourceRequestPayload(
      publishRequest,
      ed2k::createKadPublishSourceRequestPayload(attrs->link.hash, source,
                                                 sourceId, attrs->link.size)));
  ed2k::KadSearchResult localResult;
  REQUIRE(ed2k::parseKadSearchResultPayload(
      localResult,
      ed2k::createKadSearchResultPayload(
          remoteId, kadFileId,
          std::vector<ed2k::KadSearchEntry>{publishRequest.source})));
  auto localPeers = ed2k::extractKadSourceEndpoints(localResult);
  REQUIRE_EQ((size_t)1, localPeers.size());
  writeDatagram(routerSocket,
                ed2k::createDatagram(ed2k::KAD_PROTOCOL, ed2k::KAD_SEARCH_RES,
                                     ed2k::createKadSearchResultPayload(
                                         remoteId, kadFileId,
                                         std::vector<ed2k::KadSearchEntry>{
                                             publishRequest.source})),
                sourceSearchDatagram.sender.port);
  for (int i = 0; i < MAX_ENGINE_TICKS && attrs->peers.empty(); ++i) {
    engine.run(true);
  }

  REQUIRE_EQ((size_t)1, attrs->peers.size());
  REQUIRE_EQ(std::string("203.0.113.44"), attrs->peers[0].host);
  REQUIRE_EQ((uint16_t)4662, attrs->peers[0].port);
  engine.requestHalt();
}

TEST_CASE("Ed2kCommandTest.testKadDecodesReceiverKeyObfuscatedResponse")
{
  auto option = createOption();
  DownloadEngine engine(make_unique<SelectEventPoll>());
  engine.setOption(option.get());
  auto dctx = createEd2kContext();
  auto group = createRequestGroup(option, dctx);
  engine.setRequestGroupMan(make_unique<RequestGroupMan>(
      std::vector<std::shared_ptr<RequestGroup>>{group}, 5, option.get()));
  engine.getRequestGroupMan()->setKeepRunning(true);
  engine.getRequestGroupMan()->addRequestGroup(group);
  group->setRequestGroupMan(engine.getRequestGroupMan().get());

  SocketCore routerSocket(SOCK_DGRAM);
  routerSocket.bind("127.0.0.1", 0, AF_INET);
  routerSocket.setBlockingMode();
  auto routerAddr = routerSocket.getAddrInfo();

  const auto remoteId = std::string(16, '\x33');
  auto attrs = getEd2kAttrs(dctx);
  attrs->kadRoutingTable =
      std::make_shared<ed2k::KadRoutingTable>(std::string(16, '\x55'));
  attrs->kadRoutingTable->addRouterNode(
      createKadContact(remoteId, routerAddr.port));

  auto command =
      make_unique<Ed2kKadCommand>(engine.newCUID(), group.get(), &engine);
  auto commandPtr = command.get();
  engine.addCommand(std::move(command));

  auto bootstrapReq = readDatagramFrom(routerSocket, engine);
  ed2k::KadObfuscatedDatagram parsedReq;
  REQUIRE(
      ed2k::parseKadObfuscatedDatagram(parsedReq, bootstrapReq.data, remoteId));
  REQUIRE(parsedReq.senderVerifyKey != 0);

  writeDatagram(routerSocket,
                ed2k::createKadObfuscatedDatagram(
                    ed2k::createDatagram(
                        ed2k::KAD_PROTOCOL, ed2k::KAD_BOOTSTRAP_RES,
                        ed2k::createKadBootstrapResponsePayload(
                            remoteId, 4662, 8,
                            std::vector<ed2k::KadContact>{
                                createKadContact(remoteId, routerAddr.port)})),
                    parsedReq.senderVerifyKey, 0x55667788, 0x1234),
                commandPtr->getLocalUdpPort());
  runEngineTicks(engine, 1);

  auto kadReq = decodeKadDatagram(readDatagram(routerSocket, engine), remoteId);
  auto kadReqHeader = datagramHeaderOf(kadReq);
  REQUIRE_EQ(ed2k::KAD_PROTOCOL, kadReqHeader.protocol);
  REQUIRE_EQ(ed2k::KAD_REQ, kadReqHeader.opcode);
  engine.requestHalt();
}

TEST_CASE("Ed2kCommandTest.testKadDecodesSelfIdObfuscatedResponse")
{
  auto option = createOption();
  DownloadEngine engine(make_unique<SelectEventPoll>());
  engine.setOption(option.get());
  auto dctx = createEd2kContext();
  auto group = createRequestGroup(option, dctx);
  engine.setRequestGroupMan(make_unique<RequestGroupMan>(
      std::vector<std::shared_ptr<RequestGroup>>{group}, 5, option.get()));
  engine.getRequestGroupMan()->setKeepRunning(true);
  engine.getRequestGroupMan()->addRequestGroup(group);
  group->setRequestGroupMan(engine.getRequestGroupMan().get());

  SocketCore routerSocket(SOCK_DGRAM);
  routerSocket.bind("127.0.0.1", 0, AF_INET);
  routerSocket.setBlockingMode();
  auto routerAddr = routerSocket.getAddrInfo();

  const auto remoteId = std::string(16, '\x33');
  auto attrs = getEd2kAttrs(dctx);
  const auto localKadId = ed2k::ed2kHashToKadId(attrs->clientHash);
  attrs->kadRoutingTable =
      std::make_shared<ed2k::KadRoutingTable>(std::string(16, '\x55'));
  attrs->kadRoutingTable->addRouterNode(
      createKadContact(remoteId, routerAddr.port));

  auto command =
      make_unique<Ed2kKadCommand>(engine.newCUID(), group.get(), &engine);
  auto commandPtr = command.get();
  engine.addCommand(std::move(command));

  readDatagramFrom(routerSocket, engine);
  writeDatagram(routerSocket,
                ed2k::createKadObfuscatedDatagram(
                    ed2k::createDatagram(
                        ed2k::KAD_PROTOCOL, ed2k::KAD_BOOTSTRAP_RES,
                        ed2k::createKadBootstrapResponsePayload(
                            remoteId, 4662, 8,
                            std::vector<ed2k::KadContact>{
                                createKadContact(remoteId, routerAddr.port)})),
                    localKadId, 0x1234),
                commandPtr->getLocalUdpPort());
  runEngineTicks(engine, 1);

  auto kadReq = decodeKadDatagram(readDatagram(routerSocket, engine), remoteId);
  auto kadReqHeader = datagramHeaderOf(kadReq);
  REQUIRE_EQ(ed2k::KAD_PROTOCOL, kadReqHeader.protocol);
  REQUIRE_EQ(ed2k::KAD_REQ, kadReqHeader.opcode);
  engine.requestHalt();
}

} // namespace aria2
