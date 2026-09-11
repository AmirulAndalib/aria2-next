/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
#include "download_helper.h"

#include <string>
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <vector>

#include "a2doctest.h"

#include "RequestGroup.h"
#include "DownloadEngine.h"
#include "DownloadContext.h"
#include "DefaultPieceStorage.h"
#include "DiskAdaptor.h"
#include "DlRetryEx.h"
#include "Ed2kAttribute.h"
#include "Ed2kKadCommand.h"
#include "Ed2kPeerTransfer.h"
#include "Ed2kUploadQueue.h"
#include "Option.h"
#include "Piece.h"
#include "RequestGroupMan.h"
#include "SelectEventPoll.h"
#include "Segment.h"
#include "SegmentMan.h"
#include "array_fun.h"
#include "base32.h"
#include "base64.h"
#include "ed2k_constants.h"
#include "ed2k_aich.h"
#include "ed2k_endpoint.h"
#include "ed2k_hash.h"
#include "ed2k_kad.h"
#include "ed2k_link.h"
#include "ed2k_packet.h"
#include "ed2k_peer.h"
#include "ed2k_policy.h"
#include "ed2k_search.h"
#include "ed2k_server.h"
#include "prefs.h"
#include "Exception.h"
#include "TestUtil.h"
#include "support/Numbers.h"
#include "support/Encoding.h"
#include "support/FilePath.h"
#include "a2functional.h"
#include "FileEntry.h"
#ifdef ENABLE_BITTORRENT
#  include "BtDownload.h"
#endif // ENABLE_BITTORRENT

namespace aria2 {

TEST_CASE("DownloadHelperTest.testEd2kServerSourceCadencePolicy")
{
  Ed2kAttribute attrs;
  attrs.link.size = 100;

  ed2k::ServerState fresh;
  fresh.endpoint.host = "203.0.113.10";
  fresh.endpoint.port = 4661;
  fresh.connected = true;
  fresh.handshakeCompleted = true;
  fresh.nextSourceRequestTime = 1000;
  fresh.lastSourceResponseTime = 930;
  fresh.lastSourceCount = 2;
  attrs.serverStates.push_back(fresh);

  ed2k::ServerState unknownLargeServer;
  unknownLargeServer.endpoint.host = "203.0.113.9";
  unknownLargeServer.endpoint.port = 4661;
  REQUIRE(ed2k::serverConnectionDue(unknownLargeServer, 950));
  REQUIRE(!ed2k::serverTcpSourceRequestDue(
      unknownLargeServer,
      static_cast<int64_t>(std::numeric_limits<uint32_t>::max()) + 1, 950));

  REQUIRE(!ed2k::serverTcpSourceRequestDue(attrs.serverStates[0],
                                           attrs.link.size, 950));
  REQUIRE(!ed2k::serverTcpSourceRequestDue(attrs.serverStates[0],
                                           attrs.link.size, 1000));

  attrs.serverStates[0].lastSourceCount = 0;
  attrs.serverStates[0].lastSourceResponseTime = 600;
  REQUIRE(ed2k::serverTcpSourceRequestDue(attrs.serverStates[0],
                                          attrs.link.size, 1000));

  ed2k::ServerState udp = attrs.serverStates[0];
  udp.endpoint.port = 4665;
  udp.connected = false;
  udp.handshakeCompleted = false;
  udp.udpFlags = ed2k::SRV_UDPFLG_EXT_GETSOURCES;
  REQUIRE(ed2k::serverUdpSourceRequestDue(udp, attrs.link.size, 1000));

  udp.lastUdpSourceRequestTime = 990;
  REQUIRE(!ed2k::serverUdpSourceRequestDue(udp, attrs.link.size, 1000));

  udp.lastUdpSourceRequestTime = 0;
  udp.udpFlags = 0;
  REQUIRE(ed2k::serverUdpSourceRequestDue(udp, attrs.link.size, 1000));

  udp.udpFlags = ed2k::SRV_UDPFLG_EXT_GETSOURCES2;
  attrs.link.size =
      static_cast<int64_t>(std::numeric_limits<uint32_t>::max()) + 1;
  REQUIRE(!ed2k::serverUdpSourceRequestDue(udp, attrs.link.size, 1000));

  udp.udpFlags |= ed2k::SRV_UDPFLG_LARGEFILES;
  REQUIRE(ed2k::serverUdpSourceRequestDue(udp, attrs.link.size, 1000));

  ed2k::ServerState failed = udp;
  failed.nextRetryTime = 1200;
  REQUIRE(!ed2k::serverUdpSourceRequestDue(failed, attrs.link.size, 1000));
}

TEST_CASE("DownloadHelperTest.testEd2kServerSearchCadencePolicy")
{
  auto option = std::make_shared<Option>();
  auto attrs = std::make_shared<Ed2kAttribute>();
  attrs->searchActive = true;
  ed2k::Endpoint server;
  server.host = "203.0.113.10";
  server.port = 4661;
  attrs->servers.push_back(server);
  attrs->serverStates.push_back(ed2k::ServerState());
  attrs->serverStates[0].endpoint = server;
  attrs->serverStates[0].handshakeCompleted = true;
  attrs->serverStates[0].nextSourceRequestTime = 0;
  attrs->serverStates[0].lastSourceResponseTime = 1000;
  attrs->serverStates[0].lastSourceCount = 2;

  auto dctx = std::make_shared<DownloadContext>(
      ed2k::PIECE_LENGTH, 0, "/tmp/aria2-next-ed2k-search-test");
  dctx->setAttribute(CTX_ATTR_ED2K, attrs);
  auto group = std::make_shared<RequestGroup>(GroupId::create(), option);
  group->setDownloadContext(dctx);
  DownloadEngine engine(make_unique<SelectEventPoll>());
  engine.setOption(option.get());

  std::vector<std::unique_ptr<Command>> commands;
  schedulePendingEd2kServers(commands, group.get(), &engine);

  REQUIRE_EQ((size_t)1, commands.size());
}

TEST_CASE("DownloadHelperTest.testEd2kServerSchedulerFillsVacantConnectionSlot")
{
  auto option = std::make_shared<Option>();
  auto attrs = std::make_shared<Ed2kAttribute>();
  for (uint16_t port = 4661; port <= 4663; ++port) {
    ed2k::Endpoint server;
    server.host = "203.0.113." + util::uitos(port - 4650);
    server.port = port;
    attrs->servers.push_back(server);
    attrs->serverStates.push_back(ed2k::ServerState());
    attrs->serverStates.back().endpoint = server;
  }
  attrs->serverStates.front().connecting = true;

  auto dctx = std::make_shared<DownloadContext>(
      ed2k::PIECE_LENGTH, 0, "/tmp/aria2-next-ed2k-server-slots-test");
  dctx->setAttribute(CTX_ATTR_ED2K, attrs);
  auto group = std::make_shared<RequestGroup>(GroupId::create(), option);
  group->setDownloadContext(dctx);
  DownloadEngine engine(make_unique<SelectEventPoll>());
  engine.setOption(option.get());
  engine.addEd2kServerConnection();

  std::vector<std::unique_ptr<Command>> commands;
  schedulePendingEd2kServers(commands, group.get(), &engine);

  REQUIRE_EQ((size_t)1, commands.size());
  REQUIRE_EQ((size_t)2, engine.getEd2kServerConnectionCount());
  engine.removeEd2kServerConnection();
}

TEST_CASE("DownloadHelperTest.testEd2kServerStateUpdate")
{
  Ed2kAttribute attrs;
  ed2k::Endpoint server;
  server.host = "203.0.113.10";
  server.port = 4661;

  auto state = updateEd2kServerConnected(&attrs, server);
  REQUIRE(state);
  REQUIRE(state->connected);
  REQUIRE_EQ(std::string("203.0.113.10"), state->endpoint.host);
  REQUIRE_EQ((uint16_t)4661, state->endpoint.port);

  ed2k::ServerIdChange idChange;
  idChange.clientId = 0x0a000001;
  idChange.highId = true;
  idChange.ipAddress = "1.0.0.10";
  idChange.tcpFlags = 0x55aa;
  idChange.tcpObfuscationPort = 4666;
  updateEd2kServerIdChange(&attrs, server, idChange);
  REQUIRE_EQ((uint32_t)0x0a000001, state->clientId);
  REQUIRE(state->highId);
  REQUIRE(state->handshakeCompleted);
  REQUIRE_EQ(std::string("1.0.0.10"), state->ipAddress);
  REQUIRE_EQ((uint32_t)0x55aa, state->tcpFlags);
  REQUIRE_EQ((uint16_t)4666, state->tcpObfuscationPort);
  REQUIRE_EQ((uint32_t)0, state->failCount);

  ed2k::ServerStatus status;
  status.users = 1234;
  status.files = 5678;
  updateEd2kServerStatus(&attrs, server, status);
  REQUIRE_EQ((uint32_t)1234, state->users);
  REQUIRE_EQ((uint32_t)5678, state->files);
  REQUIRE_EQ((uint16_t)4666, state->tcpObfuscationPort);

  status.challenge = 0x55aa0011;
  status.users = 2234;
  status.files = 6678;
  status.maxUsers = 9000;
  status.softFiles = 100;
  status.hardFiles = 200;
  status.udpFlags = 0x01020304;
  status.lowIdUsers = 77;
  status.udpObfuscationPort = 4665;
  status.tcpObfuscationPort = 4666;
  status.udpKey = 0x11223344;
  updateEd2kServerUdpStatus(&attrs, server, status, 120);
  REQUIRE_EQ((uint32_t)2234, state->users);
  REQUIRE_EQ((uint32_t)6678, state->files);
  REQUIRE_EQ((uint32_t)9000, state->maxUsers);
  REQUIRE_EQ((uint32_t)100, state->softFiles);
  REQUIRE_EQ((uint32_t)200, state->hardFiles);
  REQUIRE_EQ((uint32_t)0x01020304, state->udpFlags);
  REQUIRE_EQ((uint32_t)77, state->lowIdUsers);
  REQUIRE_EQ((uint16_t)4665, state->udpObfuscationPort);
  REQUIRE_EQ((uint16_t)4666, state->tcpObfuscationPort);
  REQUIRE_EQ((uint32_t)0x11223344, state->udpKey);
  REQUIRE_EQ((uint32_t)0, state->udpStatusChallenge);
  REQUIRE_EQ((int64_t)120, state->lastUdpStatusTime);

  updateEd2kServerMessage(&attrs, server, "hello");
  REQUIRE_EQ(std::string("hello"), state->lastMessage);

  ed2k::ServerIdent ident;
  ident.name = "server name";
  ident.description = "server description";
  updateEd2kServerIdent(&attrs, server, ident);
  REQUIRE_EQ(std::string("server name"), state->name);
  REQUIRE_EQ(std::string("server description"), state->description);

  updateEd2kServerSourceRequestTime(&attrs, server, 90);
  REQUIRE(state->connected);
  REQUIRE_EQ((int64_t)90, state->nextSourceRequestTime);
  markEd2kServerSourceRequestFinished(&attrs, server);
  REQUIRE(state->connected);
  REQUIRE(!state->connecting);

  updateEd2kServerFailure(&attrs, server, 100, 30);
  REQUIRE(!state->connected);
  REQUIRE(!state->handshakeCompleted);
  REQUIRE_EQ((uint32_t)1, state->failCount);
  REQUIRE_EQ((int64_t)100, state->lastFailureTime);
  REQUIRE_EQ((int64_t)130, state->nextRetryTime);
}

TEST_CASE("DownloadHelperTest.testEd2kSearchResultDeduplication")
{
  Ed2kAttribute attrs;
  ed2k::SearchResultEntry entry;
  const std::string hashHex = "0123456789abcdef0123456789abcdef";
  entry.hash = util::fromHex(hashHex.begin(), hashHex.end());
  entry.name = "video.mkv";
  entry.size = 12345;
  entry.sourceNetwork = "server";

  REQUIRE_EQ((size_t)1, addEd2kSearchResults(&attrs, {entry}, true));
  REQUIRE_EQ((size_t)0, addEd2kSearchResults(&attrs, {entry}, false));
  REQUIRE_EQ((size_t)1, attrs.searchResults.size());
  REQUIRE(!attrs.searchMoreResults);

  entry.size = 12346;
  REQUIRE_EQ((size_t)1, addEd2kSearchResults(&attrs, {entry}, true));
  REQUIRE_EQ((size_t)2, attrs.searchResults.size());
  REQUIRE(attrs.searchMoreResults);
}

TEST_CASE("DownloadHelperTest.testEd2kSearchResultMergesNetworks")
{
  Ed2kAttribute attrs;
  ed2k::SearchResultEntry server;
  server.hash = std::string(ed2k::HASH_LENGTH, '\x31');
  server.name = "movie.mkv";
  server.size = 42;
  server.sourceCount = 3;
  server.completeSourceCount = 1;
  server.sourceNetwork = "server";
  server.ed2kLink =
      "ed2k://|file|movie.mkv|42|31313131313131313131313131313131|/";

  ed2k::SearchResultEntry kad = server;
  kad.sourceCount = 7;
  kad.completeSourceCount = 4;
  kad.sourceNetwork = "kad";

  REQUIRE_EQ((size_t)1, addEd2kSearchResults(&attrs, {server}, false));
  REQUIRE_EQ((size_t)0, addEd2kSearchResults(&attrs, {kad}, false));
  REQUIRE_EQ((size_t)1, attrs.searchResults.size());
  REQUIRE_EQ((uint32_t)7, attrs.searchResults[0].sourceCount);
  REQUIRE_EQ((uint32_t)4, attrs.searchResults[0].completeSourceCount);
  REQUIRE_EQ(std::string("server|kad"), attrs.searchResults[0].sourceNetwork);
}

TEST_CASE("DownloadHelperTest.testEd2kSearchResultAppliesLocalFilters")
{
  Ed2kAttribute attrs;
  attrs.searchQuery.minSize = 100;
  attrs.searchQuery.maxSize = 200;
  attrs.searchQuery.minSourceCount = 3;
  attrs.searchQuery.minCompleteSourceCount = 2;

  ed2k::SearchResultEntry rejected;
  rejected.hash = std::string(ed2k::HASH_LENGTH, '\x31');
  rejected.name = "small.iso";
  rejected.size = 50;
  rejected.sourceCount = 10;
  rejected.completeSourceCount = 5;
  rejected.sourceNetwork = "kad";
  rejected.ed2kLink =
      "ed2k://|file|small.iso|50|31313131313131313131313131313131|/";

  ed2k::SearchResultEntry accepted = rejected;
  accepted.hash = std::string(ed2k::HASH_LENGTH, '\x32');
  accepted.name = "movie.iso";
  accepted.size = 150;
  accepted.sourceCount = 3;
  accepted.completeSourceCount = 2;
  accepted.ed2kLink =
      "ed2k://|file|movie.iso|150|32323232323232323232323232323232|/";

  REQUIRE_EQ((size_t)1,
             addEd2kSearchResults(&attrs, {rejected, accepted}, false));
  REQUIRE_EQ((size_t)1, attrs.searchResults.size());
  REQUIRE_EQ(accepted.hash, attrs.searchResults[0].hash);
}

} // namespace aria2
