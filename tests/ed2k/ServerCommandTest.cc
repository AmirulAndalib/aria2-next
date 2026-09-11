/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
#include "DHKeyExchange.h"
#include "MSEDHKeyExchange.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
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
#include "ed2k_server.h"
#include "a2functional.h"

namespace aria2 {

using namespace test::ed2k_command;

TEST_CASE("Ed2kCommandTest.testServerSourceDiscoveryFlow")
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
  auto serverAddr = listenSocket.getAddrInfo();

  ed2k::Endpoint server;
  server.host = "127.0.0.1";
  server.port = serverAddr.port;
  auto attrs = getEd2kAttrs(dctx);
  attrs->servers.push_back(server);

  std::vector<std::unique_ptr<Command>> commands;
  schedulePendingEd2kServers(commands, group.get(), &engine);
  REQUIRE_EQ((size_t)1, commands.size());
  engine.addCommand(std::move(commands));

  runEngineTicks(engine, 1);
  auto serverSocket = acceptPeer(listenSocket, engine);
  runEngineTicks(engine, 1);

  auto login = readPacket(serverSocket, engine);
  auto loginHeader = packetHeaderOf(login);
  REQUIRE_EQ(ed2k::PROTO_EDONKEY, loginHeader.protocol);
  REQUIRE_EQ(ed2k::OP_LOGINREQUEST, loginHeader.opcode);

  serverSocket->writeData(ed2k::createPacket(
      ed2k::PROTO_EDONKEY, ed2k::OP_IDCHANGE,
      ed2k::packUInt32(0x04030201) +
          ed2k::packUInt32(ed2k::SRV_TCPFLG_LARGEFILES) + ed2k::packUInt32(0)));
  runEngineTicks(engine, 1);

  auto getSources = readPacket(serverSocket, engine);
  auto getSourcesHeader = packetHeaderOf(getSources);
  REQUIRE_EQ(ed2k::PROTO_EDONKEY, getSourcesHeader.protocol);
  REQUIRE_EQ(ed2k::OP_GETSOURCES, getSourcesHeader.opcode);
  auto getSourcesBody = packetBodyOf(getSources);
  REQUIRE(getSourcesBody.size() >= ed2k::HASH_LENGTH + 4);
  REQUIRE_EQ(attrs->link.hash, getSourcesBody.substr(0, ed2k::HASH_LENGTH));
  REQUIRE_EQ(static_cast<uint32_t>(attrs->link.size),
             ed2k::readUInt32(getSourcesBody.data() + ed2k::HASH_LENGTH));

  std::vector<ed2k::Endpoint> sources;
  ed2k::Endpoint source;
  source.host = "203.0.113.20";
  source.port = 4662;
  sources.push_back(source);
  serverSocket->writeData(ed2k::createPacket(
      ed2k::PROTO_EDONKEY, ed2k::OP_FOUNDSOURCES,
      ed2k::createFoundSourcesPayload(attrs->link.hash, sources)));
  runEngineTicks(engine, 1);

  REQUIRE_EQ((size_t)1, attrs->peers.size());
  REQUIRE_EQ(std::string("203.0.113.20"), attrs->peers[0].host);
  REQUIRE_EQ((uint16_t)4662, attrs->peers[0].port);
  auto state = getEd2kServerState(attrs, server);
  REQUIRE(state);
  REQUIRE(state->handshakeCompleted);
  REQUIRE(!state->connecting);
  REQUIRE(state->connected);
  engine.requestHalt();
}

TEST_CASE("Ed2kCommandTest.testServerObfuscationCompletesEncryptedLogin")
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
  const auto listenAddr = listenSocket.getAddrInfo();

  ed2k::Endpoint endpoint;
  endpoint.host = "127.0.0.1";
  endpoint.port = 4661;
  auto attrs = getEd2kAttrs(dctx);
  attrs->servers.push_back(endpoint);
  ed2k::ServerState state;
  state.endpoint = endpoint;
  state.tcpObfuscationPort = listenAddr.port;
  state.tcpFlags = ed2k::SRV_TCPFLG_TCPOBFUSCATION;
  attrs->serverStates.push_back(state);

  engine.addCommand(make_unique<Ed2kCommand>(engine.newCUID(), group.get(),
                                             &engine, endpoint, true, false));
  runEngineTicks(engine, 1);
  auto serverSocket = acceptPeer(listenSocket, engine);
  runEngineTicks(engine, 1);

  std::array<char, 98> request;
  readFromSocket(serverSocket, engine, request.data(), request.size());
  const auto marker = static_cast<uint8_t>(request[0]);
  REQUIRE(marker != ed2k::PROTO_EDONKEY);
  REQUIRE(marker != ed2k::PROTO_PACKED);
  REQUIRE(marker != ed2k::PROTO_EMULE);
  MSEDHPublicKey clientPublic{};
  std::copy_n(reinterpret_cast<const unsigned char*>(request.data() + 1),
              clientPublic.size(), clientPublic.begin());
  const auto paddingLength = static_cast<uint8_t>(request[97]);
  if (paddingLength != 0) {
    std::string padding(paddingLength, '\0');
    readFromSocket(serverSocket, engine, &padding[0], padding.size());
  }

  MSEDHPrivateKey serverPrivate{};
  std::fill(serverPrivate.end() - 16, serverPrivate.end(), '\x33');
  DHKeyExchange serverDh(serverPrivate, ed2k::SERVER_DH_PRIME_HEX);
  const auto secret = serverDh.computeSecret(clientPublic);
  const std::string sharedSecret(reinterpret_cast<const char*>(secret.data()),
                                 secret.size());
  ARC4Encryptor encryptor;
  const auto responseKey = ed2k::createServerTcpObfuscationKey(
      sharedSecret, ED2K_OBFUSCATION_MAGIC_SERVER);
  encryptor.init(reinterpret_cast<const unsigned char*>(responseKey.data()),
                 responseKey.size());
  discardTcpObfuscationPrefix(encryptor);
  ARC4Encryptor decryptor;
  const auto requestKey = ed2k::createServerTcpObfuscationKey(
      sharedSecret, ED2K_OBFUSCATION_MAGIC_REQUESTER);
  decryptor.init(reinterpret_cast<const unsigned char*>(requestKey.data()),
                 requestKey.size());
  discardTcpObfuscationPrefix(decryptor);

  std::string negotiation = ed2k::packUInt32(ED2K_OBFUSCATION_SYNC);
  negotiation.append(3, '\0');
  encryptor.encrypt(negotiation.size(),
                    reinterpret_cast<unsigned char*>(&negotiation[0]),
                    reinterpret_cast<const unsigned char*>(negotiation.data()));
  const auto& serverPublic = serverDh.getPublicKey();
  std::string response(reinterpret_cast<const char*>(serverPublic.data()),
                       serverPublic.size());
  response += negotiation;
  serverSocket->writeData(response);
  runEngineTicks(engine, 3);

  std::array<char, 6> clientResponse;
  readFromSocket(serverSocket, engine, clientResponse.data(),
                 clientResponse.size());
  decryptor.encrypt(
      clientResponse.size(),
      reinterpret_cast<unsigned char*>(clientResponse.data()),
      reinterpret_cast<const unsigned char*>(clientResponse.data()));
  REQUIRE_EQ(ED2K_OBFUSCATION_SYNC, ed2k::readUInt32(clientResponse.data()));
  REQUIRE_EQ((uint8_t)0, static_cast<uint8_t>(clientResponse[4]));
  REQUIRE_EQ((uint8_t)0, static_cast<uint8_t>(clientResponse[5]));

  std::array<char, 6> header;
  readFromSocket(serverSocket, engine, header.data(), header.size());
  decryptor.encrypt(header.size(),
                    reinterpret_cast<unsigned char*>(header.data()),
                    reinterpret_cast<const unsigned char*>(header.data()));
  ed2k::PacketHeader packetHeader;
  REQUIRE(ed2k::readPacketHeader(packetHeader, header.data(), header.size()));
  REQUIRE_EQ(ed2k::OP_LOGINREQUEST, packetHeader.opcode);
  std::string body(packetHeader.payloadSize(), '\0');
  readFromSocket(serverSocket, engine, &body[0], body.size());
  decryptor.encrypt(body.size(), reinterpret_cast<unsigned char*>(&body[0]),
                    reinterpret_cast<const unsigned char*>(body.data()));
  REQUIRE(body.size() >= ed2k::HASH_LENGTH);
  REQUIRE_EQ(attrs->clientHash, body.substr(0, ed2k::HASH_LENGTH));
  engine.requestHalt();
}

TEST_CASE("Ed2kCommandTest.testServerObfuscationFailureFallsBackToPlainPort")
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

  SocketCore plainListener;
  plainListener.bind(0);
  plainListener.beginListen();
  plainListener.setBlockingMode();
  SocketCore obfuscatedListener;
  obfuscatedListener.bind(0);
  obfuscatedListener.beginListen();
  obfuscatedListener.setBlockingMode();

  ed2k::Endpoint endpoint;
  endpoint.host = "127.0.0.1";
  endpoint.port = plainListener.getAddrInfo().port;
  auto attrs = getEd2kAttrs(dctx);
  attrs->servers.push_back(endpoint);
  ed2k::ServerState serverState;
  serverState.endpoint = endpoint;
  serverState.tcpObfuscationPort = obfuscatedListener.getAddrInfo().port;
  serverState.tcpFlags = ed2k::SRV_TCPFLG_TCPOBFUSCATION;
  attrs->serverStates.push_back(serverState);

  engine.addCommand(make_unique<Ed2kCommand>(engine.newCUID(), group.get(),
                                             &engine, endpoint, true, false));
  runEngineTicks(engine, 1);
  auto failedSocket = acceptPeer(obfuscatedListener, engine);
  failedSocket->closeConnection();
  runEngineTicks(engine, 2);
  auto state = getEd2kServerState(attrs, endpoint);
  REQUIRE(state);
  REQUIRE(state->tcpObfuscationFailed);

  engine.addCommand(make_unique<Ed2kCommand>(engine.newCUID(), group.get(),
                                             &engine, endpoint, true, false));
  runEngineTicks(engine, 1);
  auto plainSocket = acceptPeer(plainListener, engine);
  runEngineTicks(engine, 1);
  const auto login = readPacket(plainSocket, engine);
  REQUIRE_EQ(ed2k::OP_LOGINREQUEST, packetHeaderOf(login).opcode);
  engine.requestHalt();
}

} // namespace aria2
