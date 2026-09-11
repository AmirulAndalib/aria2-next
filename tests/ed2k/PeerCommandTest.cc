/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
#include "DiskAdaptor.h"
#include "ContextAttribute.h"
#include <cstddef>
#include <cstdint>
#include <ios>
#include <utility>
#include "CommandTestSupport.h"
#include "Ed2kCommand.h"

#include <fstream>
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
#include "DefaultPieceStorage.h"
#include "SeedCheckCommand.h"
#include "ShareRatioSeedCriteria.h"
#include "File.h"
#include "RequestGroup.h"
#include "RequestGroupMan.h"
#include "SelectEventPoll.h"
#include "SocketCore.h"
#include "ed2k_constants.h"
#include "ed2k_hash.h"
#include "ed2k_kad.h"
#include "ed2k_link.h"
#include "ed2k_packet.h"
#include "ed2k_peer.h"
#include "a2functional.h"

namespace aria2 {

using namespace test::ed2k_command;

TEST_CASE("Ed2kCommandTest.testPeerHandshakeQueuesFileRequestAndQueueRank")
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

  SocketCore listenSocket;
  listenSocket.bind(0);
  listenSocket.beginListen();
  listenSocket.setBlockingMode();
  auto peerAddr = listenSocket.getAddrInfo();

  ed2k::Endpoint peer;
  peer.host = "127.0.0.1";
  peer.port = peerAddr.port;
  auto attrs = getEd2kAttrs(dctx);
  addEd2kPeer(attrs, peer, ed2k::PEER_SOURCE_INLINE);

  engine.addCommand(make_unique<Ed2kCommand>(engine.newCUID(), group.get(),
                                             &engine, peer, false));

  runEngineTicks(engine, 1);
  auto peerSocket = acceptPeer(listenSocket, engine);
  runEngineTicks(engine, 1);

  auto hello = readPacket(peerSocket, engine);
  auto helloHeader = packetHeaderOf(hello);
  REQUIRE_EQ(ed2k::PROTO_EDONKEY, helloHeader.protocol);
  REQUIRE_EQ(ed2k::OP_HELLO, helloHeader.opcode);

  auto remoteInfo = ed2k::createLocalEmulePeerInfo();
  remoteInfo.miscOptions.multiPacket = false;
  remoteInfo.miscOptions2.supportsExtendedMultipacket = false;
  auto helloAnswerPayload = ed2k::createPeerHelloPayload(
      std::string(ed2k::HASH_LENGTH, '\x22'), 0x04030201, peerAddr.port,
      ed2k::Endpoint(), "test-peer", remoteInfo, false);
  peerSocket->writeData(ed2k::createPacket(
      ed2k::PROTO_EDONKEY, ed2k::OP_HELLOANSWER, helloAnswerPayload));
  runEngineTicks(engine, 1);

  auto fileRequest = readPacket(peerSocket, engine);
  auto fileRequestHeader = packetHeaderOf(fileRequest);
  REQUIRE_EQ(ed2k::PROTO_EDONKEY, fileRequestHeader.protocol);
  REQUIRE_EQ(ed2k::OP_REQUESTFILENAME, fileRequestHeader.opcode);
  REQUIRE_EQ(attrs->link.hash,
             packetBodyOf(fileRequest).substr(0, ed2k::HASH_LENGTH));

  auto statusRequest = readPacket(peerSocket, engine);
  auto statusRequestHeader = packetHeaderOf(statusRequest);
  REQUIRE_EQ(ed2k::PROTO_EDONKEY, statusRequestHeader.protocol);
  REQUIRE_EQ(ed2k::OP_SETREQFILEID, statusRequestHeader.opcode);
  REQUIRE_EQ(attrs->link.hash, packetBodyOf(statusRequest));

  auto sourceExchange = readPacket(peerSocket, engine);
  auto sourceExchangeHeader = packetHeaderOf(sourceExchange);
  REQUIRE_EQ(ed2k::PROTO_EMULE, sourceExchangeHeader.protocol);
  REQUIRE_EQ(ed2k::OP_REQUESTSOURCES2, sourceExchangeHeader.opcode);
  REQUIRE_EQ(ed2k::createRequestSources2Payload(attrs->link.hash),
             packetBodyOf(sourceExchange));

  auto aichRequest = readPacket(peerSocket, engine);
  REQUIRE_EQ(ed2k::PROTO_EMULE, packetHeaderOf(aichRequest).protocol);
  REQUIRE_EQ(ed2k::OP_AICHFILEHASHREQ, packetHeaderOf(aichRequest).opcode);
  REQUIRE_EQ(attrs->link.hash, packetBodyOf(aichRequest));

  peerSocket->writeData(
      ed2k::createPacket(ed2k::PROTO_EDONKEY, ed2k::OP_FILESTATUS,
                         ed2k::createFileStatusPayload(
                             attrs->link.hash, std::vector<bool>{true, true})));
  runEngineTicks(engine, 1);

  auto hashSetRequest = readPacket(peerSocket, engine);
  auto hashSetRequestHeader = packetHeaderOf(hashSetRequest);
  REQUIRE_EQ(ed2k::PROTO_EDONKEY, hashSetRequestHeader.protocol);
  REQUIRE_EQ(ed2k::OP_HASHSETREQUEST, hashSetRequestHeader.opcode);
  REQUIRE_EQ(attrs->link.hash, packetBodyOf(hashSetRequest));

  const auto pieceHashes = createPieceHashes();
  peerSocket->writeData(ed2k::createPacket(
      ed2k::PROTO_EDONKEY, ed2k::OP_HASHSETANSWER,
      ed2k::createHashSetAnswerPayload(attrs->link.hash, pieceHashes)));
  runEngineTicks(engine, 1);

  auto startUpload = readPacket(peerSocket, engine);
  auto startUploadHeader = packetHeaderOf(startUpload);
  REQUIRE_EQ(ed2k::PROTO_EDONKEY, startUploadHeader.protocol);
  REQUIRE_EQ(ed2k::OP_STARTUPLOADREQ, startUploadHeader.opcode);
  REQUIRE_EQ(attrs->link.hash, packetBodyOf(startUpload));

  peerSocket->writeData(ed2k::createPacket(ed2k::PROTO_EDONKEY,
                                           ed2k::OP_QUEUERANK,
                                           ed2k::createQueueRankPayload(7)));
  runEngineTicks(engine, MAX_ENGINE_TICKS);

  auto state = getEd2kPeerState(attrs, peer);
  REQUIRE(state);
  REQUIRE(state->queued);
  REQUIRE_EQ((uint16_t)7, state->queueRank);
  REQUIRE(!state->connecting);
  engine.requestHalt();
}

TEST_CASE("Ed2kCommandTest.testPeerHandshakeQueuesMultipacketRequest")
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

  SocketCore listenSocket;
  listenSocket.bind(0);
  listenSocket.beginListen();
  listenSocket.setBlockingMode();
  auto peerAddr = listenSocket.getAddrInfo();

  ed2k::Endpoint peer;
  peer.host = "127.0.0.1";
  peer.port = peerAddr.port;
  auto attrs = getEd2kAttrs(dctx);
  addEd2kPeer(attrs, peer, ed2k::PEER_SOURCE_INLINE);

  engine.addCommand(make_unique<Ed2kCommand>(engine.newCUID(), group.get(),
                                             &engine, peer, false));

  runEngineTicks(engine, 1);
  auto peerSocket = acceptPeer(listenSocket, engine);
  runEngineTicks(engine, 1);
  readPacket(peerSocket, engine);

  auto remoteInfo = ed2k::createLocalEmulePeerInfo();
  remoteInfo.miscOptions.multiPacket = true;
  remoteInfo.miscOptions2.supportsExtendedMultipacket = true;
  auto helloAnswerPayload = ed2k::createPeerHelloPayload(
      std::string(ed2k::HASH_LENGTH, '\x22'), 0x04030201, peerAddr.port,
      ed2k::Endpoint(), "test-peer", remoteInfo, false);
  peerSocket->writeData(ed2k::createPacket(
      ed2k::PROTO_EDONKEY, ed2k::OP_HELLOANSWER, helloAnswerPayload));
  runEngineTicks(engine, 1);

  auto multipacket = readPacket(peerSocket, engine);
  auto multipacketHeader = packetHeaderOf(multipacket);
  REQUIRE_EQ(ed2k::PROTO_EMULE, multipacketHeader.protocol);
  REQUIRE_EQ(ed2k::OP_MULTIPACKET_EXT, multipacketHeader.opcode);

  auto body = packetBodyOf(multipacket);
  REQUIRE_EQ(attrs->link.hash, body.substr(0, ed2k::HASH_LENGTH));
  REQUIRE_EQ(static_cast<uint64_t>(attrs->link.size),
             ed2k::readUInt64(body.data() + ed2k::HASH_LENGTH));
  REQUIRE(body.find(static_cast<char>(ed2k::OP_REQUESTFILENAME)) !=
          std::string::npos);
  REQUIRE(body.find(static_cast<char>(ed2k::OP_SETREQFILEID)) !=
          std::string::npos);
  REQUIRE(body.find(static_cast<char>(ed2k::OP_REQUESTSOURCES2)) !=
          std::string::npos);
  REQUIRE(body.find(static_cast<char>(ed2k::OP_AICHFILEHASHREQ)) !=
          std::string::npos);

  auto answer = attrs->link.hash;
  answer.push_back(static_cast<char>(ed2k::OP_FILESTATUS));
  answer += ed2k::packUInt16(2);
  answer.push_back(static_cast<char>(0x03));
  peerSocket->writeData(ed2k::createPacket(ed2k::PROTO_EMULE,
                                           ed2k::OP_MULTIPACKETANSWER, answer));
  runEngineTicks(engine, 1);

  auto hashSetRequest = readPacket(peerSocket, engine);
  REQUIRE_EQ(ed2k::OP_HASHSETREQUEST, packetHeaderOf(hashSetRequest).opcode);
  REQUIRE_EQ(attrs->link.hash, packetBodyOf(hashSetRequest));
  engine.requestHalt();
}

TEST_CASE(
    "Ed2kCommandTest.testPeerHandshakeFallbackQueuesFileStatusImmediately")
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

  SocketCore listenSocket;
  listenSocket.bind(0);
  listenSocket.beginListen();
  listenSocket.setBlockingMode();
  auto peerAddr = listenSocket.getAddrInfo();

  ed2k::Endpoint peer;
  peer.host = "127.0.0.1";
  peer.port = peerAddr.port;
  auto attrs = getEd2kAttrs(dctx);
  addEd2kPeer(attrs, peer, ed2k::PEER_SOURCE_INLINE);

  engine.addCommand(make_unique<Ed2kCommand>(engine.newCUID(), group.get(),
                                             &engine, peer, false));

  runEngineTicks(engine, 1);
  auto peerSocket = acceptPeer(listenSocket, engine);
  runEngineTicks(engine, 1);
  readPacket(peerSocket, engine);

  auto remoteInfo = ed2k::createLocalEmulePeerInfo();
  remoteInfo.miscOptions.multiPacket = false;
  remoteInfo.miscOptions2.supportsExtendedMultipacket = false;
  auto helloAnswerPayload = ed2k::createPeerHelloPayload(
      std::string(ed2k::HASH_LENGTH, '\x22'), 0x04030201, peerAddr.port,
      ed2k::Endpoint(), "test-peer", remoteInfo, false);
  peerSocket->writeData(ed2k::createPacket(
      ed2k::PROTO_EDONKEY, ed2k::OP_HELLOANSWER, helloAnswerPayload));
  runEngineTicks(engine, 1);

  auto fileRequest = readPacket(peerSocket, engine);
  REQUIRE_EQ(ed2k::OP_REQUESTFILENAME, packetHeaderOf(fileRequest).opcode);
  auto statusRequest = readPacket(peerSocket, engine);
  REQUIRE_EQ(ed2k::OP_SETREQFILEID, packetHeaderOf(statusRequest).opcode);
  REQUIRE_EQ(attrs->link.hash, packetBodyOf(statusRequest));
  engine.requestHalt();
}

TEST_CASE("Ed2kCommandTest.testPeerHandlesBuddyCallback")
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

  SocketCore listenSocket;
  listenSocket.bind(0);
  listenSocket.beginListen();
  listenSocket.setBlockingMode();
  auto peerAddr = listenSocket.getAddrInfo();

  ed2k::Endpoint buddy;
  buddy.host = "127.0.0.1";
  buddy.port = peerAddr.port;
  buddy.userHash = std::string(ed2k::HASH_LENGTH, '\x22');
  auto attrs = getEd2kAttrs(dctx);
  addEd2kPeer(attrs, buddy, ed2k::PEER_SOURCE_KAD);

  engine.addCommand(make_unique<Ed2kCommand>(engine.newCUID(), group.get(),
                                             &engine, buddy, false));

  runEngineTicks(engine, 1);
  auto buddySocket = acceptPeer(listenSocket, engine);
  runEngineTicks(engine, 1);
  readPacket(buddySocket, engine);

  auto remoteInfo = ed2k::createLocalEmulePeerInfo();
  auto helloAnswerPayload = ed2k::createPeerHelloPayload(
      std::string(ed2k::HASH_LENGTH, '\x22'), 0x04030201, peerAddr.port,
      ed2k::Endpoint(), "buddy-peer", remoteInfo, false);
  buddySocket->writeData(ed2k::createPacket(
      ed2k::PROTO_EDONKEY, ed2k::OP_HELLOANSWER, helloAnswerPayload));
  runEngineTicks(engine, 1);
  readPacket(buddySocket, engine);

  ed2k::Endpoint callbackSource;
  callbackSource.host = "203.0.113.44";
  callbackSource.port = 4662;
  buddySocket->writeData(ed2k::createPacket(
      ed2k::PROTO_EMULE, ed2k::OP_CALLBACK,
      ed2k::createBuddyCallbackPayload(ed2k::ed2kHashToKadId(attrs->clientHash),
                                       ed2k::ed2kHashToKadId(attrs->link.hash),
                                       callbackSource)));
  runEngineTicks(engine, 1);

  auto state = getEd2kPeerState(attrs, callbackSource);
  REQUIRE(state);
  REQUIRE((state->sourceFlags & ed2k::PEER_SOURCE_KAD) != 0);
  engine.requestHalt();
}

TEST_CASE("Ed2kCommandTest.testIncomingPeerMultipacketRequestGetsAnswer")
{
  auto option = createOption();
  const std::string data = "incoming multipacket seed data";
  const std::string path = A2_TEST_OUT_DIR "/ed2k-incoming-multipacket.bin";
  {
    std::ofstream out(path.c_str(), std::ios::binary);
    out << data;
  }

  auto dctx = std::make_shared<DownloadContext>(
      ed2k::PIECE_LENGTH, static_cast<int64_t>(data.size()), path);
  auto attrs = make_unique<Ed2kAttribute>();
  attrs->link.type = ed2k::LinkType::FILE;
  attrs->link.name = "ed2k-incoming-multipacket.bin";
  attrs->link.size = data.size();
  attrs->link.hash = ed2k::md4Digest(data);
  attrs->clientHash =
      normalizeEd2kClientHash(std::string(ed2k::HASH_LENGTH, '\x42'));
  dctx->setAttribute(CTX_ATTR_ED2K, std::move(attrs));

  auto group = createRequestGroup(option, dctx);
  group->initPieceStorage();
  group->getPieceStorage()->markAllPiecesDone();
  DownloadEngine engine(make_unique<SelectEventPoll>());
  engine.setOption(option.get());
  engine.setRequestGroupMan(make_unique<RequestGroupMan>(
      std::vector<std::shared_ptr<RequestGroup>>{group}, 5, option.get()));
  engine.getRequestGroupMan()->addRequestGroup(group);
  group->setRequestGroupMan(engine.getRequestGroupMan().get());
  group->enableSeedOnly();

  SocketCore listenSocket;
  listenSocket.bind(0);
  listenSocket.beginListen();
  listenSocket.setBlockingMode();
  auto localAddr = listenSocket.getAddrInfo();

  SocketCore client;
  client.establishConnection("127.0.0.1", localAddr.port);
  client.setNonBlockingMode();
  for (int i = 0; i < MAX_ENGINE_TICKS && !client.isWritable(0); ++i) {
    engine.run(true);
  }
  auto serverSocket = acceptPeer(listenSocket, engine);
  ed2k::Endpoint endpoint;
  endpoint.host = "127.0.0.1";
  endpoint.port = localAddr.port;
  engine.addCommand(make_unique<Ed2kCommand>(engine.newCUID(), group.get(),
                                             &engine, endpoint, serverSocket));
  auto clientRef = std::shared_ptr<SocketCore>(&client, [](SocketCore*) {});

  auto remoteInfo = ed2k::createLocalEmulePeerInfo();
  client.writeData(ed2k::createPacket(
      ed2k::PROTO_EDONKEY, ed2k::OP_HELLO,
      ed2k::createPeerHelloPayload(std::string(ed2k::HASH_LENGTH, '\x22'),
                                   0x04030201, localAddr.port, ed2k::Endpoint(),
                                   "test-peer", remoteInfo, true)));
  runEngineTicks(engine, 1);

  readPacket(clientRef, engine);

  auto request = ed2k::createMultipacketFileRequestPayload(
      getEd2kAttrs(dctx)->link.hash, data.size(), std::vector<bool>{false},
      remoteInfo, true);
  client.writeData(
      ed2k::createPacket(ed2k::PROTO_EMULE, ed2k::OP_MULTIPACKET_EXT, request));
  runEngineTicks(engine, 1);

  auto answerPacket = readPacket(clientRef, engine);
  if (packetHeaderOf(answerPacket).opcode == ed2k::OP_ANSWERSOURCES2) {
    answerPacket = readPacket(clientRef, engine);
  }
  REQUIRE_EQ(ed2k::OP_MULTIPACKETANSWER, packetHeaderOf(answerPacket).opcode);
  ed2k::MultipacketAnswer answer;
  REQUIRE(ed2k::parseMultipacketAnswerPayload(
      answer, packetBodyOf(answerPacket), getEd2kAttrs(dctx)->link.hash));
  REQUIRE(answer.hasFileName);
  REQUIRE_EQ(std::string("ed2k-incoming-multipacket.bin"), answer.fileName);
  engine.requestHalt();
}

TEST_CASE("Ed2kCommandTest.testLargePeerPartRequestUsesEmuleProtocol")
{
  auto option = createOption();
  auto dctx = std::make_shared<DownloadContext>(
      ed2k::PIECE_LENGTH, 0x100000001LL,
      A2_TEST_OUT_DIR "/ed2k-large-command-test.bin");
  auto attrs = make_unique<Ed2kAttribute>();
  attrs->link.type = ed2k::LinkType::FILE;
  attrs->link.name = "ed2k-large-command-test.bin";
  attrs->link.size = dctx->getTotalLength();
  attrs->link.hash = std::string(ed2k::HASH_LENGTH, '\x33');
  attrs->clientHash =
      normalizeEd2kClientHash(std::string(ed2k::HASH_LENGTH, '\x42'));
  attrs->pieceHashes.push_back(attrs->link.hash);
  dctx->setPieceLength(ed2k::PIECE_LENGTH);
  dctx->setAttribute(CTX_ATTR_ED2K, std::move(attrs));

  DownloadEngine engine(make_unique<SelectEventPoll>());
  engine.setOption(option.get());
  auto group = createRequestGroup(option, dctx);
  engine.setRequestGroupMan(make_unique<RequestGroupMan>(
      std::vector<std::shared_ptr<RequestGroup>>{group}, 5, option.get()));
  engine.getRequestGroupMan()->addRequestGroup(group);
  group->setRequestGroupMan(engine.getRequestGroupMan().get());
  group->initPieceStorage();

  SocketCore listenSocket;
  listenSocket.bind(0);
  listenSocket.beginListen();
  listenSocket.setBlockingMode();
  auto peerAddr = listenSocket.getAddrInfo();

  ed2k::Endpoint peer;
  peer.host = "127.0.0.1";
  peer.port = peerAddr.port;
  addEd2kPeer(getEd2kAttrs(dctx), peer, ed2k::PEER_SOURCE_INLINE);
  engine.addCommand(make_unique<Ed2kCommand>(engine.newCUID(), group.get(),
                                             &engine, peer, false));

  runEngineTicks(engine, 1);
  auto peerSocket = acceptPeer(listenSocket, engine);
  runEngineTicks(engine, 1);

  readPacket(peerSocket, engine);
  auto remoteInfo = ed2k::createLocalEmulePeerInfo();
  peerSocket->writeData(ed2k::createPacket(
      ed2k::PROTO_EDONKEY, ed2k::OP_HELLOANSWER,
      ed2k::createPeerHelloPayload(std::string(ed2k::HASH_LENGTH, '\x22'),
                                   0x04030201, peerAddr.port, ed2k::Endpoint(),
                                   "test-peer", remoteInfo, false)));
  runEngineTicks(engine, 1);

  readPacket(peerSocket, engine);
  auto answer = getEd2kAttrs(dctx)->link.hash;
  answer.push_back(static_cast<char>(ed2k::OP_FILESTATUS));
  answer += ed2k::packUInt16(0);
  peerSocket->writeData(ed2k::createPacket(ed2k::PROTO_EMULE,
                                           ed2k::OP_MULTIPACKETANSWER, answer));
  runEngineTicks(engine, 1);

  auto startUpload = readPacket(peerSocket, engine);
  REQUIRE_EQ(ed2k::OP_STARTUPLOADREQ, packetHeaderOf(startUpload).opcode);
  peerSocket->writeData(ed2k::createPacket(
      ed2k::PROTO_EDONKEY, ed2k::OP_ACCEPTUPLOADREQ, std::string()));
  runEngineTicks(engine, 1);

  auto partRequest = readPacket(peerSocket, engine);
  REQUIRE_EQ(ed2k::PROTO_EMULE, packetHeaderOf(partRequest).protocol);
  REQUIRE_EQ(ed2k::OP_REQUESTPARTS_I64, packetHeaderOf(partRequest).opcode);

  engine.requestHalt();
}

TEST_CASE("Ed2kCommandTest.testPeerCommandFinishesGroupAfterLastPart")
{
  auto option = createOption();
  const std::string data = "finished ed2k command data";
  const std::string outfile = A2_TEST_OUT_DIR "/ed2k-command-finished.bin";
  File(outfile).remove();

  auto dctx = std::make_shared<DownloadContext>(
      ed2k::PIECE_LENGTH, static_cast<int64_t>(data.size()), outfile);
  auto attrs = make_unique<Ed2kAttribute>();
  attrs->link.type = ed2k::LinkType::FILE;
  attrs->link.name = "ed2k-command-finished.bin";
  attrs->link.size = data.size();
  attrs->link.hash = ed2k::md4Digest(data);
  attrs->clientHash =
      normalizeEd2kClientHash(std::string(ed2k::HASH_LENGTH, '\x42'));
  attrs->pieceHashes.push_back(attrs->link.hash);
  dctx->setAttribute(CTX_ATTR_ED2K, std::move(attrs));

  auto group = createRequestGroup(option, dctx);
  group->initPieceStorage();
  group->getPieceStorage()->getDiskAdaptor()->openFile();
  DownloadEngine engine(make_unique<SelectEventPoll>());
  engine.setOption(option.get());
  engine.setRequestGroupMan(make_unique<RequestGroupMan>(
      std::vector<std::shared_ptr<RequestGroup>>{group}, 5, option.get()));
  engine.getRequestGroupMan()->addRequestGroup(group);
  group->setRequestGroupMan(engine.getRequestGroupMan().get());

  SocketCore listenSocket;
  listenSocket.bind(0);
  listenSocket.beginListen();
  listenSocket.setBlockingMode();
  auto peerAddr = listenSocket.getAddrInfo();

  ed2k::Endpoint peer;
  peer.host = "127.0.0.1";
  peer.port = peerAddr.port;
  addEd2kPeer(getEd2kAttrs(dctx), peer, ed2k::PEER_SOURCE_INLINE);
  engine.addCommand(make_unique<Ed2kCommand>(engine.newCUID(), group.get(),
                                             &engine, peer, false));

  runEngineTicks(engine, 1);
  auto peerSocket = acceptPeer(listenSocket, engine);
  runEngineTicks(engine, 1);

  readPacket(peerSocket, engine);
  auto remoteInfo = ed2k::createLocalEmulePeerInfo();
  peerSocket->writeData(ed2k::createPacket(
      ed2k::PROTO_EDONKEY, ed2k::OP_HELLOANSWER,
      ed2k::createPeerHelloPayload(std::string(ed2k::HASH_LENGTH, '\x22'),
                                   0x04030201, peerAddr.port, ed2k::Endpoint(),
                                   "test-peer", remoteInfo, false)));
  runEngineTicks(engine, 1);

  auto multipacket = readPacket(peerSocket, engine);
  REQUIRE_EQ(ed2k::OP_MULTIPACKET_EXT, packetHeaderOf(multipacket).opcode);
  auto answer = getEd2kAttrs(dctx)->link.hash;
  answer.push_back(static_cast<char>(ed2k::OP_FILESTATUS));
  answer += ed2k::packUInt16(0);
  peerSocket->writeData(ed2k::createPacket(ed2k::PROTO_EMULE,
                                           ed2k::OP_MULTIPACKETANSWER, answer));
  runEngineTicks(engine, 1);

  auto startUpload = readPacket(peerSocket, engine);
  REQUIRE_EQ(ed2k::OP_STARTUPLOADREQ, packetHeaderOf(startUpload).opcode);
  peerSocket->writeData(ed2k::createPacket(
      ed2k::PROTO_EDONKEY, ed2k::OP_ACCEPTUPLOADREQ, std::string()));
  runEngineTicks(engine, 1);

  auto partRequest = readPacket(peerSocket, engine);
  REQUIRE_EQ(ed2k::OP_REQUESTPARTS, packetHeaderOf(partRequest).opcode);
  const size_t split = 8;
  peerSocket->writeData(
      ed2k::createPacket(ed2k::PROTO_EDONKEY, ed2k::OP_SENDINGPART,
                         getEd2kAttrs(dctx)->link.hash + ed2k::packUInt32(0) +
                             ed2k::packUInt32(split) + data.substr(0, split)));
  runEngineTicks(engine, 1);

  peerSocket->writeData(ed2k::createPacket(
      ed2k::PROTO_EDONKEY, ed2k::OP_SENDINGPART,
      getEd2kAttrs(dctx)->link.hash + ed2k::packUInt32(split) +
          ed2k::packUInt32(data.size()) + data.substr(split)));
  runEngineTicks(engine, 2);

  REQUIRE(group->downloadFinished());
  REQUIRE_EQ((int32_t)0, group->getNumCommand());
  REQUIRE(group->isSeedOnlyEnabled());
  REQUIRE_EQ((size_t)0,
             engine.getRequestGroupMan()->getDownloadResults().size());
  engine.requestHalt();
  runEngineTicks(engine, 1);
}

} // namespace aria2
