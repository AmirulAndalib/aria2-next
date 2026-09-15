/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
#include <cstdint>
#include "CommandTestSupport.h"
#include "Ed2kCommand.h"

#include <array>
#include "a2doctest.h"
#include <memory>
#include <vector>

#include "ARC4Encryptor.h"
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
#include "ed2k_crypto.h"
#include "ed2k_hash.h"
#include "ed2k_link.h"
#include "ed2k_packet.h"
#include "ed2k_peer.h"
#include "support/Encoding.h"
#include "a2functional.h"

namespace aria2 {

using namespace test::ed2k_command;

TEST_CASE("Ed2kCommandTest.testCryptPeerStartsWithObfuscatedHandshake")
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
  peer.userHash = std::string(ed2k::HASH_LENGTH, '\x22');
  peer.cryptOptions = ed2k::SOURCE_CRYPT_SUPPORT | ed2k::SOURCE_CRYPT_REQUEST |
                      ed2k::SOURCE_CRYPT_HAS_USER_HASH;
  addEd2kPeer(getEd2kAttrs(dctx), peer, ed2k::PEER_SOURCE_INLINE);

  engine.addCommand(make_unique<Ed2kCommand>(engine.newCUID(), group.get(),
                                             &engine, peer, false));

  runEngineTicks(engine, 1);
  auto peerSocket = acceptPeer(listenSocket, engine);
  runEngineTicks(engine, 1);

  std::array<char, 1> prefix;
  readFromSocket(peerSocket, engine, prefix.data(), prefix.size());
  auto marker = static_cast<uint8_t>(prefix[0]);
  REQUIRE(marker != ed2k::PROTO_EDONKEY);
  REQUIRE(marker != ed2k::PROTO_PACKED);
  REQUIRE(marker != ed2k::PROTO_EMULE);

  engine.requestHalt();
}

TEST_CASE("Ed2kCommandTest.testTcpObfuscationKeyMatchesAMuleVector")
{
  const auto key = ed2k::createTcpObfuscationKey(
      std::string(ed2k::HASH_LENGTH, '\x22'), ED2K_OBFUSCATION_MAGIC_REQUESTER,
      0x01020304);
  REQUIRE_EQ(std::string("b13280f234ea469301bfc1c073a4cae7"), util::toHex(key));
}

TEST_CASE("Ed2kCommandTest.testIncomingCryptPeerCompletesEncryptedHello")
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

  const uint32_t randomKeyPart = 0x01020304;
  const auto localHash = getEd2kAttrs(dctx)->clientHash;
  ARC4Encryptor encryptor;
  auto requestKey = createAMuleTcpObfuscationKey(
      localHash, ED2K_OBFUSCATION_MAGIC_REQUESTER, randomKeyPart);
  encryptor.init(reinterpret_cast<const unsigned char*>(requestKey.data()),
                 requestKey.size());
  discardTcpObfuscationPrefix(encryptor);
  ARC4Encryptor decryptor;
  auto responseKey = createAMuleTcpObfuscationKey(
      localHash, ED2K_OBFUSCATION_MAGIC_SERVER, randomKeyPart);
  decryptor.init(reinterpret_cast<const unsigned char*>(responseKey.data()),
                 responseKey.size());
  discardTcpObfuscationPrefix(decryptor);

  std::string negotiation = ed2k::packUInt32(ED2K_OBFUSCATION_SYNC);
  negotiation.append(3, '\0');
  encryptor.encrypt(negotiation.size(),
                    reinterpret_cast<unsigned char*>(&negotiation[0]),
                    reinterpret_cast<const unsigned char*>(negotiation.data()));
  std::string request(1, '\x01');
  request += ed2k::packUInt32(randomKeyPart);
  request += negotiation;
  client.writeData(request);
  runEngineTicks(engine, 2);

  std::array<char, 6> response;
  readFromSocket(clientRef, engine, response.data(), response.size());
  decryptor.encrypt(response.size(),
                    reinterpret_cast<unsigned char*>(response.data()),
                    reinterpret_cast<const unsigned char*>(response.data()));
  REQUIRE_EQ(ED2K_OBFUSCATION_SYNC, ed2k::readUInt32(response.data()));
  REQUIRE_EQ((uint8_t)0, static_cast<uint8_t>(response[4]));
  REQUIRE_EQ((uint8_t)0, static_cast<uint8_t>(response[5]));

  auto remoteInfo = ed2k::createLocalEmulePeerInfo();
  auto hello = ed2k::createPacket(
      ed2k::PROTO_EDONKEY, ed2k::OP_HELLO,
      ed2k::createPeerHelloPayload(std::string(ed2k::HASH_LENGTH, '\x22'),
                                   0x04030201, localAddr.port, ed2k::Endpoint(),
                                   "encrypted-peer", remoteInfo, true));
  encryptor.encrypt(hello.size(), reinterpret_cast<unsigned char*>(&hello[0]),
                    reinterpret_cast<const unsigned char*>(hello.data()));
  client.writeData(hello);
  runEngineTicks(engine, 2);

  std::array<char, 6> header;
  readFromSocket(clientRef, engine, header.data(), header.size());
  decryptor.encrypt(header.size(),
                    reinterpret_cast<unsigned char*>(header.data()),
                    reinterpret_cast<const unsigned char*>(header.data()));
  ed2k::PacketHeader packetHeader;
  REQUIRE(ed2k::readPacketHeader(packetHeader, header.data(), header.size()));
  REQUIRE_EQ(ed2k::OP_HELLOANSWER, packetHeader.opcode);
  std::string body(packetHeader.payloadSize(), '\0');
  readFromSocket(clientRef, engine, &body[0], body.size());
  decryptor.encrypt(body.size(), reinterpret_cast<unsigned char*>(&body[0]),
                    reinterpret_cast<const unsigned char*>(body.data()));
  ed2k::EmulePeerInfo parsed;
  REQUIRE(ed2k::parsePeerHelloPayload(parsed, body, false));
  REQUIRE_EQ(localHash, parsed.userHash);
  engine.requestHalt();
}

TEST_CASE("Ed2kCommandTest.testCryptPeerHandshakeMatchesAMuleKeys")
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

  const auto userHash = std::string(ed2k::HASH_LENGTH, '\x22');
  ed2k::Endpoint peer;
  peer.host = "127.0.0.1";
  peer.port = peerAddr.port;
  peer.userHash = userHash;
  peer.cryptOptions = ed2k::SOURCE_CRYPT_SUPPORT | ed2k::SOURCE_CRYPT_REQUEST |
                      ed2k::SOURCE_CRYPT_HAS_USER_HASH;
  addEd2kPeer(getEd2kAttrs(dctx), peer, ed2k::PEER_SOURCE_INLINE);

  engine.addCommand(make_unique<Ed2kCommand>(engine.newCUID(), group.get(),
                                             &engine, peer, false));

  runEngineTicks(engine, 1);
  auto peerSocket = acceptPeer(listenSocket, engine);
  runEngineTicks(engine, 1);

  std::array<char, 11> request;
  readFromSocket(peerSocket, engine, request.data(), request.size());
  const auto randomKeyPart = ed2k::readUInt32(request.data() + 1);
  ARC4Encryptor decryptor;
  auto key = createAMuleTcpObfuscationKey(
      userHash, ED2K_OBFUSCATION_MAGIC_REQUESTER, randomKeyPart);
  decryptor.init(reinterpret_cast<const unsigned char*>(key.data()),
                 key.size());
  discardTcpObfuscationPrefix(decryptor);
  decryptor.encrypt(request.size() - 5,
                    reinterpret_cast<unsigned char*>(request.data() + 5),
                    reinterpret_cast<const unsigned char*>(request.data() + 5));

  REQUIRE_EQ(ED2K_OBFUSCATION_SYNC, ed2k::readUInt32(request.data() + 5));
  REQUIRE_EQ((uint8_t)0, static_cast<uint8_t>(request[9]));
  REQUIRE_EQ((uint8_t)0, static_cast<uint8_t>(request[10]));

  engine.requestHalt();
}

TEST_CASE("Ed2kCommandTest.testCryptSupportOnlyPeerUsesPlainHandshake")
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
  peer.userHash = std::string(ed2k::HASH_LENGTH, '\x22');
  peer.cryptOptions =
      ed2k::SOURCE_CRYPT_SUPPORT | ed2k::SOURCE_CRYPT_HAS_USER_HASH;
  addEd2kPeer(getEd2kAttrs(dctx), peer, ed2k::PEER_SOURCE_INLINE);

  engine.addCommand(make_unique<Ed2kCommand>(engine.newCUID(), group.get(),
                                             &engine, peer, false));

  runEngineTicks(engine, 1);
  auto peerSocket = acceptPeer(listenSocket, engine);
  runEngineTicks(engine, 1);

  auto hello = readPacket(peerSocket, engine);
  REQUIRE_EQ(ed2k::PROTO_EDONKEY, packetHeaderOf(hello).protocol);
  REQUIRE_EQ(ed2k::OP_HELLO, packetHeaderOf(hello).opcode);

  engine.requestHalt();
}

} // namespace aria2
