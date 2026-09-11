/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
#include "ContextAttribute.h"
#include <cstdint>
#include <ios>
#include <memory>
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

TEST_CASE("DownloadHelperTest.testCreateRequestGroupForUri_ED2K")
{
  auto option = std::make_shared<Option>();
  std::vector<std::string> uris{"ed2k://|file|aria2%2Fnext.bin|9728001|"
                                "0123456789abcdef0123456789abcdef|"
                                "p=11111111111111111111111111111111:"
                                "22222222222222222222222222222222|/"};
  option->put(PREF_ED2K_MAX_CONNECTIONS, "4");
  option->put(PREF_DIR, "/tmp");
  option->put(PREF_ED2K_SERVER, "203.0.113.10:4661,203.0.113.11:4661");

  std::vector<std::shared_ptr<RequestGroup>> result;

  createRequestGroupForUri(result, option, uris);

  REQUIRE_EQ((size_t)1, result.size());
  auto group = result[0];
  auto ctx = group->getDownloadContext();
  REQUIRE(ctx->hasAttribute(CTX_ATTR_ED2K));
  REQUIRE_EQ(ed2k::PIECE_LENGTH, ctx->getPieceLength());
  REQUIRE_EQ((int64_t)9728001, ctx->getTotalLength());
  REQUIRE_EQ(std::string("/tmp/aria2_next.bin"), ctx->getBasePath());
  REQUIRE(ctx->getFirstFileEntry()->isRequested());
  REQUIRE_EQ((size_t)0, ctx->getFirstFileEntry()->getRemainingUris().size());
  REQUIRE(!ctx->isChecksumVerificationAvailable());
  REQUIRE(!ctx->isPieceHashVerificationAvailable());
  REQUIRE_EQ(4, group->getNumConcurrentCommand());

  auto attrs = getEd2kAttrs(ctx);
  REQUIRE_EQ(std::string("aria2_next.bin"), attrs->link.name);
  REQUIRE_EQ(std::string("0123456789abcdef0123456789abcdef"),
             util::toHex(attrs->link.hash));
  REQUIRE_EQ((size_t)2, attrs->link.pieceHashes.size());
  REQUIRE_EQ((size_t)2, attrs->servers.size());
  REQUIRE_EQ(std::string("203.0.113.10"), attrs->servers[0].host);
  REQUIRE_EQ((uint16_t)4661, attrs->servers[0].port);

  option->put(PREF_ED2K_MAX_CONNECTIONS,
              util::itos(ed2k::DEFAULT_PEER_CONNECTIONS));
  result.clear();
  createRequestGroupForUri(result, option, uris);
  REQUIRE_EQ(ed2k::DEFAULT_PEER_CONNECTIONS,
             result[0]->getNumConcurrentCommand());
}

#ifndef __MINGW32__

TEST_CASE(
    "DownloadHelperTest.testCreateRequestGroupForUri_ED2KDefaultKadBootstrap")
{
  auto option = std::make_shared<Option>();
  std::string nodeIdHex("23a8ceff57a7a32d562d649ed7893796");
  auto nodeId = util::fromHex(nodeIdHex.begin(), nodeIdHex.end());
  ed2k::KadContact contact;
  contact.id = nodeId;
  contact.host = "203.0.113.1";
  contact.udpPort = 4672;
  contact.tcpPort = 4662;
  contact.version = 8;

  std::string nodesDat;
  nodesDat += ed2k::packUInt32(0);
  nodesDat += ed2k::packUInt32(3);
  nodesDat += ed2k::packUInt32(1);
  nodesDat += ed2k::packUInt32(1);
  nodesDat += ed2k::createKadResponsePayload(
                  nodeId, std::vector<ed2k::KadContact>{contact})
                  .substr(ed2k::HASH_LENGTH + 1);
  const std::string home = A2_TEST_OUT_DIR "/ed2k-default-home";
  const std::string amuleDir = home + "/.aMule";
  File(amuleDir).mkdirs();
  const std::string path = amuleDir + "/nodes.dat";
  {
    std::ofstream out(path.c_str(), std::ios::binary);
    out.write(nodesDat.data(), nodesDat.size());
  }

  const char* oldHome = getenv("HOME");
  const std::string oldHomeValue = oldHome ? oldHome : "";
  setenv("HOME", home.c_str(), 1);

  std::vector<std::string> uris{"ed2k://|file|aria2%20next.bin|9728001|"
                                "0123456789abcdef0123456789abcdef|/"};
  option->put(PREF_DIR, "/tmp");

  std::vector<std::shared_ptr<RequestGroup>> result;
  try {
    createRequestGroupForUri(result, option, uris);
  }
  catch (...) {
    if (oldHome) {
      setenv("HOME", oldHomeValue.c_str(), 1);
    }
    else {
      unsetenv("HOME");
    }
    throw;
  }
  if (oldHome) {
    setenv("HOME", oldHomeValue.c_str(), 1);
  }
  else {
    unsetenv("HOME");
  }

  REQUIRE_EQ((size_t)1, result.size());
  auto attrs = getEd2kAttrs(result[0]->getDownloadContext());
  REQUIRE(attrs->kadRoutingTable);
  REQUIRE(!attrs->kadRoutingTable->getRouterNodes().empty());
  REQUIRE_EQ(ed2k::ed2kHashToKadId(attrs->clientHash),
             attrs->kadRoutingTable->snapshot().selfId);
}

TEST_CASE("DownloadHelperTest.testCreateRequestGroupForUri_"
          "ED2KDefaultMacKadBootstrap")
{
  auto option = std::make_shared<Option>();
  std::string nodeIdHex("23a8ceff57a7a32d562d649ed7893796");
  auto nodeId = util::fromHex(nodeIdHex.begin(), nodeIdHex.end());
  ed2k::KadContact contact;
  contact.id = nodeId;
  contact.host = "203.0.113.2";
  contact.udpPort = 4672;
  contact.tcpPort = 4662;
  contact.version = 8;

  std::string nodesDat;
  nodesDat += ed2k::packUInt32(0);
  nodesDat += ed2k::packUInt32(3);
  nodesDat += ed2k::packUInt32(1);
  nodesDat += ed2k::packUInt32(1);
  nodesDat += ed2k::createKadResponsePayload(
                  nodeId, std::vector<ed2k::KadContact>{contact})
                  .substr(ed2k::HASH_LENGTH + 1);
  const std::string home = A2_TEST_OUT_DIR "/ed2k-default-mac-home";
  const std::string amuleDir = home + "/Library/Application Support/aMule";
  File(amuleDir).mkdirs();
  const std::string path = amuleDir + "/nodes.dat";
  {
    std::ofstream out(path.c_str(), std::ios::binary);
    out.write(nodesDat.data(), nodesDat.size());
  }

  const char* oldHome = getenv("HOME");
  const std::string oldHomeValue = oldHome ? oldHome : "";
  setenv("HOME", home.c_str(), 1);

  std::vector<std::string> uris{"ed2k://|file|aria2%20next.bin|9728001|"
                                "0123456789abcdef0123456789abcdef|/"};
  option->put(PREF_DIR, "/tmp");

  std::vector<std::shared_ptr<RequestGroup>> result;
  try {
    createRequestGroupForUri(result, option, uris);
  }
  catch (...) {
    if (oldHome) {
      setenv("HOME", oldHomeValue.c_str(), 1);
    }
    else {
      unsetenv("HOME");
    }
    throw;
  }
  if (oldHome) {
    setenv("HOME", oldHomeValue.c_str(), 1);
  }
  else {
    unsetenv("HOME");
  }

  REQUIRE_EQ((size_t)1, result.size());
  auto attrs = getEd2kAttrs(result[0]->getDownloadContext());
  REQUIRE(!attrs->servers.empty());
  REQUIRE(attrs->kadRoutingTable);
  REQUIRE_EQ((size_t)1, attrs->kadRoutingTable->getRouterNodes().size());
  REQUIRE_EQ(std::string("203.0.113.2"),
             attrs->kadRoutingTable->getRouterNodes()[0].host);
  REQUIRE_EQ(ed2k::ed2kHashToKadId(attrs->clientHash),
             attrs->kadRoutingTable->snapshot().selfId);
}

#endif

TEST_CASE("DownloadHelperTest.testCreateRequestGroupForUri_ED2KNodesDat")
{
  auto option = std::make_shared<Option>();
  std::string nodeIdHex("23a8ceff57a7a32d562d649ed7893796");
  auto nodeId = util::fromHex(nodeIdHex.begin(), nodeIdHex.end());
  ed2k::KadContact contact;
  contact.id = nodeId;
  contact.host = "203.0.113.1";
  contact.udpPort = 4672;
  contact.tcpPort = 4662;
  contact.version = 8;

  std::string nodesDat;
  nodesDat += ed2k::packUInt32(0);
  nodesDat += ed2k::packUInt32(3);
  nodesDat += ed2k::packUInt32(1);
  nodesDat += ed2k::packUInt32(1);
  nodesDat += ed2k::createKadResponsePayload(
                  nodeId, std::vector<ed2k::KadContact>{contact})
                  .substr(ed2k::HASH_LENGTH + 1);
  const std::string path = A2_TEST_OUT_DIR "/ed2k-nodes.dat";
  createFile(path, nodesDat.size());
  {
    std::ofstream out(path.c_str(), std::ios::binary);
    out.write(nodesDat.data(), nodesDat.size());
  }

  std::vector<std::string> uris{"ed2k://|file|aria2%20next.bin|9728001|"
                                "0123456789abcdef0123456789abcdef|/"};
  option->put(PREF_DIR, "/tmp");
  option->put(PREF_ED2K_NODE_LIST, path);

  std::vector<std::shared_ptr<RequestGroup>> result;

  createRequestGroupForUri(result, option, uris);

  auto attrs = getEd2kAttrs(result[0]->getDownloadContext());
  REQUIRE(!attrs->servers.empty());
  REQUIRE(attrs->kadRoutingTable);
  REQUIRE_EQ((size_t)1, attrs->kadRoutingTable->getRouterNodes().size());
  REQUIRE_EQ(ed2k::ed2kHashToKadId(attrs->clientHash),
             attrs->kadRoutingTable->snapshot().selfId);
  REQUIRE_EQ(std::string("203.0.113.1"),
             attrs->kadRoutingTable->getRouterNodes()[0].host);
  REQUIRE_EQ((uint16_t)4672, attrs->kadRoutingTable->getRouterNodes()[0].port);
}

TEST_CASE(
    "DownloadHelperTest.testCreateRequestGroupForUri_ED2KServerMetMetadata")
{
  auto option = std::make_shared<Option>();
  std::string serverMet;
  serverMet.push_back('\x0e');
  serverMet += ed2k::packUInt32(1);
  serverMet += ed2k::packUInt32(0x04030201);
  serverMet += ed2k::packUInt16(4661);
  serverMet += ed2k::packUInt32(5);
  serverMet += ed2k::createStringTag(0x01, "Peer Server");
  serverMet += ed2k::createStringTag(0x0b, "Primary ED2K server");
  serverMet += ed2k::createUInt32Tag(0x87, 9000);
  serverMet += ed2k::createUInt32Tag(0x92, 0x01020304);
  serverMet += ed2k::createUInt32Tag(0x97, 4666);
  const std::string path = A2_TEST_OUT_DIR "/ed2k-server.met";
  createFile(path, serverMet.size());
  {
    std::ofstream out(path.c_str(), std::ios::binary);
    out.write(serverMet.data(), serverMet.size());
  }

  std::vector<std::string> uris{"ed2k://|file|aria2%20next.bin|9728001|"
                                "0123456789abcdef0123456789abcdef|/"};
  option->put(PREF_DIR, "/tmp");
  option->put(PREF_ED2K_SERVER_LIST, path);

  std::vector<std::shared_ptr<RequestGroup>> result;

  createRequestGroupForUri(result, option, uris);

  auto attrs = getEd2kAttrs(result[0]->getDownloadContext());
  REQUIRE_EQ((size_t)1, attrs->servers.size());
  REQUIRE_EQ((size_t)1, attrs->serverStates.size());
  REQUIRE_EQ(std::string("1.2.3.4"), attrs->servers[0].host);
  REQUIRE_EQ((uint16_t)4661, attrs->servers[0].port);
  REQUIRE_EQ(std::string("Peer Server"), attrs->serverStates[0].name);
  REQUIRE_EQ(std::string("Primary ED2K server"),
             attrs->serverStates[0].description);
  REQUIRE_EQ((uint32_t)9000, attrs->serverStates[0].maxUsers);
  REQUIRE_EQ((uint32_t)0x01020304, attrs->serverStates[0].udpFlags);
  REQUIRE_EQ((uint16_t)4666, attrs->serverStates[0].tcpObfuscationPort);
}

TEST_CASE("DownloadHelperTest.testCreateRequestGroupForUri_ED2KDefaultServers")
{
  auto option = std::make_shared<Option>();
  std::vector<std::string> uris{"ed2k://|file|aria2%20next.bin|9728001|"
                                "0123456789abcdef0123456789abcdef|/"};
  option->put(PREF_DIR, "/tmp");
  option->remove(PREF_ED2K_SERVER);

  std::vector<std::shared_ptr<RequestGroup>> result;
  createRequestGroupForUri(result, option, uris);

  auto attrs = getEd2kAttrs(result[0]->getDownloadContext());
  REQUIRE_EQ((size_t)7, attrs->servers.size());
  REQUIRE_EQ(std::string("45.82.80.155"), attrs->servers[0].host);
  REQUIRE_EQ((uint16_t)5687, attrs->servers[0].port);
}

} // namespace aria2
