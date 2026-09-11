/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
#include "ContextAttribute.h"
#include "Ed2kKadState.h"
#include "ValueBase.h"
#include "ed2k_kad.h"
#include "ed2k_peer.h"
#include "ed2k_server.h"
#include <cstddef>
#include <memory>
#include <utility>
#include "RpcTestSupport.h"
#include "RpcMethod.h"

#include "a2doctest.h"

#include "ApplicationStatePath.h"
#include "DownloadEngine.h"
#include "Option.h"
#include "RequestGroupMan.h"
#include "RequestGroup.h"
#include "rpc/RpcMethods.h"
#include "rpc/RpcStatus.h"
#include "OptionParser.h"
#include "OptionHandler.h"
#include "RpcRequest.h"
#include "RpcResponse.h"
#include "TestUtil.h"
#include "DownloadContext.h"
#include "download_helper.h"
#include "DefaultPieceStorage.h"
#include "Ed2kAttribute.h"
#include "ed2k_hash.h"
#include "ed2k_link.h"
#include "ed2k_search.h"
#ifdef ENABLE_BITTORRENT
#  include "BtDownload.h"
#  include "BtSession.h"
#  include "BtStateStore.h"
#endif // ENABLE_BITTORRENT

namespace aria2::rpc {

TEST_CASE_FIXTURE(RpcMethodTest, "RpcMethodTest.testEd2kSearchResults")
{
  auto dctx = std::make_shared<DownloadContext>(
      ed2k::PIECE_LENGTH, 0, A2_TEST_OUT_DIR "/ed2k-rpc-search");
  auto attrs = std::make_shared<Ed2kAttribute>();
  attrs->searchActive = true;
  attrs->searchMoreResults = true;
  attrs->searchQuery.keyword = "movie";
  ed2k::SearchResultEntry entry;
  entry.hash = std::string(ed2k::HASH_LENGTH, '\x42');
  entry.name = "movie.mkv";
  entry.size = 123456789;
  entry.sourceCount = 8;
  entry.completeSourceCount = 5;
  entry.fileType = "Video";
  entry.extension = "mkv";
  entry.mediaTitle = "Movie";
  entry.mediaBitrate = 320;
  entry.sourceNetwork = "server|kad";
  ed2k::Link link;
  link.type = ed2k::LinkType::FILE;
  link.name = entry.name;
  link.size = entry.size;
  link.hash = entry.hash;
  entry.ed2kLink = ed2k::toFileLink(link);
  attrs->searchResults.push_back(entry);
  dctx->setAttribute(CTX_ATTR_ED2K, attrs);

  auto group = std::make_shared<RequestGroup>(GroupId::create(), option_);
  group->setDownloadContext(dctx);
  const auto gid = group->getGID();
  e_->getRequestGroupMan()->addReservedGroup(group);

  GetEd2kSearchResultsRpcMethod m;
  auto req = createReq(GetEd2kSearchResultsRpcMethod::getMethodName());
  req.params->append(GroupId::toHex(gid));
  auto res = m.execute(std::move(req), e_.get());

  REQUIRE_EQ(0, res.code);
  const auto body = downcast<Dict>(res.param);
  REQUIRE(body);
  REQUIRE_EQ(GroupId::toHex(gid), getString(body, "gid"));
  const auto moreResults = downcast<Bool>(body->get("moreResults"));
  REQUIRE(moreResults);
  REQUIRE(moreResults->val());
  const auto results = downcast<List>(body->get("results"));
  REQUIRE(results);
  REQUIRE_EQ((size_t)1, results->size());
  const auto result = downcast<Dict>(results->get(0));
  REQUIRE(result);
  REQUIRE_EQ(std::string("42424242424242424242424242424242"),
             getString(result, "hash"));
  REQUIRE_EQ(std::string("movie.mkv"), getString(result, "name"));
  REQUIRE_EQ(std::string("123456789"), getString(result, "length"));
  REQUIRE_EQ(std::string("8"), getString(result, "sourceCount"));
  REQUIRE_EQ(std::string("5"), getString(result, "completeSourceCount"));
  REQUIRE_EQ(std::string("server|kad"), getString(result, "sourceNetwork"));
  REQUIRE_EQ(entry.ed2kLink, getString(result, "ed2kLink"));
}

TEST_CASE_FIXTURE(RpcMethodTest,
                  "RpcMethodTest.testEd2kSearchResultLinkCreatesDownload")
{
  ed2k::Link link;
  link.type = ed2k::LinkType::FILE;
  link.name = "movie.mkv";
  link.size = 123456789;
  link.hash = std::string(ed2k::HASH_LENGTH, '\x42');

  AddUriRpcMethod m;
  auto req = createReq(AddUriRpcMethod::getMethodName());
  auto urisParam = List::g();
  urisParam->append(ed2k::toFileLink(link));
  req.params->append(std::move(urisParam));
  auto res = m.execute(std::move(req), e_.get());

  REQUIRE_EQ(0, res.code);
  const auto& groups = e_->getRequestGroupMan()->getReservedGroups();
  REQUIRE_EQ((size_t)1, groups.size());
  auto attrs = getEd2kAttrs((*groups.begin())->getDownloadContext());
  REQUIRE(attrs);
  REQUIRE_EQ(link.hash, attrs->link.hash);
  REQUIRE_EQ(link.name, attrs->link.name);
  REQUIRE_EQ(link.size, attrs->link.size);
}

TEST_CASE_FIXTURE(RpcMethodTest, "RpcMethodTest.testGatherProgressEd2kStatus")
{
  auto dctx = std::make_shared<DownloadContext>(
      ed2k::PIECE_LENGTH, 1024, A2_TEST_OUT_DIR "/ed2k-status.bin");
  auto attrs = std::make_shared<Ed2kAttribute>();
  attrs->link.type = ed2k::LinkType::FILE;
  attrs->link.name = "ed2k-status.bin";
  attrs->link.size = 1024;
  attrs->link.hash = std::string(ed2k::HASH_LENGTH, '\x42');
  attrs->searchActive = true;
  attrs->searchMoreResults = true;
  attrs->searchResults.resize(2);
  attrs->sharingTime.restore(23);
  attrs->pieceHashes.push_back(std::string(ed2k::HASH_LENGTH, '\x11'));
  attrs->aichRootHash = std::string(ed2k::AICH_HASH_LENGTH, '\x22');

  ed2k::ServerState server;
  server.endpoint.host = "203.0.113.10";
  server.endpoint.port = 4661;
  server.name = "server";
  server.connected = true;
  server.handshakeCompleted = true;
  server.highId = true;
  server.users = 10;
  server.files = 20;
  attrs->serverStates.push_back(server);

  ed2k::PeerState peer;
  peer.endpoint.host = "203.0.113.20";
  peer.endpoint.port = 4662;
  peer.sourceFlags = ed2k::PEER_SOURCE_SERVER;
  peer.queued = true;
  peer.queueRank = 7;
  attrs->peerStates.push_back(peer);

  ed2k::PeerState lowIdPeer;
  lowIdPeer.endpoint.host = "120.0.0.42";
  lowIdPeer.endpoint.port = 4662;
  lowIdPeer.lowId = true;
  lowIdPeer.callbackRequested = true;
  lowIdPeer.lowIdCallbackState = ed2k::LowIdCallbackState::REQUESTED;
  attrs->peerStates.push_back(lowIdPeer);

  attrs->kadRoutingTable = std::make_shared<ed2k::KadRoutingTable>(
      std::string(ed2k::HASH_LENGTH, '\x33'));
  ed2k::KadContact kadNode;
  kadNode.id = std::string(ed2k::HASH_LENGTH, '\x34');
  kadNode.host = "203.0.113.31";
  kadNode.udpPort = 4672;
  kadNode.tcpPort = 4662;
  kadNode.version = 8;
  attrs->kadRoutingTable->nodeSeen(kadNode, 100);
  ed2k::Endpoint router;
  router.host = "203.0.113.30";
  router.port = 4672;
  attrs->kadRoutingTable->addRouterNode(router);
  attrs->kadObservedAddresses.push_back("198.51.100.1");
  attrs->kadFirewalled = false;

  dctx->setAttribute(CTX_ATTR_ED2K, attrs);
  auto group = std::make_shared<RequestGroup>(
      GroupId::create(), std::make_shared<Option>(*option_));
  group->setDownloadContext(dctx);
  group->setRequestGroupMan(e_->getRequestGroupMan().get());

  auto seederEntry = Dict::g();
  gatherProgressCommon(seederEntry.get(), group, {"seeder"});
  REQUIRE_EQ((size_t)1, seederEntry->size());
  REQUIRE(seederEntry->containsKey("seeder"));
  REQUIRE_EQ(std::string("false"), getString(seederEntry.get(), "seeder"));

  group->initPieceStorage();
  group->getPieceStorage()->markAllPiecesDone();
  seederEntry = Dict::g();
  gatherProgressCommon(seederEntry.get(), group, {"seeder"});
  REQUIRE_EQ((size_t)1, seederEntry->size());
  REQUIRE(seederEntry->containsKey("seeder"));
  REQUIRE_EQ(std::string("true"), getString(seederEntry.get(), "seeder"));

  auto entry = Dict::g();
  gatherProgressCommon(entry.get(), group, {"ed2k"});

  REQUIRE_EQ((size_t)1, entry->size());
  auto ed2kStatus = downcast<Dict>(entry->get("ed2k"));
  REQUIRE(ed2kStatus);
  REQUIRE_EQ(std::string("42424242424242424242424242424242"),
             getString(ed2kStatus, "hash"));
  REQUIRE_EQ(std::string("ed2k-status.bin"), getString(ed2kStatus, "name"));
  REQUIRE_EQ(std::string("1024"), getString(ed2kStatus, "length"));
  REQUIRE_EQ(std::string("ed2k://|file|ed2k-status.bin|1024|"
                         "42424242424242424242424242424242|/"),
             getString(ed2kStatus, "ed2kLink"));
  REQUIRE_EQ(std::string("1"), getString(ed2kStatus, "partHashCount"));
  REQUIRE_EQ(std::string("2222222222222222222222222222222222222222"),
             getString(ed2kStatus, "aichRoot"));
  REQUIRE_EQ(std::string("1"), getString(ed2kStatus, "serverCount"));
  REQUIRE_EQ(std::string("1"), getString(ed2kStatus, "connectedServerCount"));
  REQUIRE_EQ(std::string("2"), getString(ed2kStatus, "peerCount"));
  REQUIRE_EQ(std::string("1"), getString(ed2kStatus, "queuedPeerCount"));
  REQUIRE_EQ(std::string("1"), getString(ed2kStatus, "lowIdPeerCount"));
  REQUIRE_EQ(std::string("1"),
             getString(ed2kStatus, "callbackWaitingPeerCount"));
  REQUIRE_EQ(std::string("1"), getString(ed2kStatus, "kadNodeCount"));
  REQUIRE_EQ(std::string("1"), getString(ed2kStatus, "kadRouterCount"));
  REQUIRE(!downcast<Bool>(ed2kStatus->get("kadFirewalled"))->val());
  REQUIRE_EQ(std::string("2"), getString(ed2kStatus, "searchResultCount"));
  REQUIRE_EQ(std::string("23"), getString(ed2kStatus, "sharingTime"));
  REQUIRE(downcast<Bool>(ed2kStatus->get("searchActive"))->val());
  REQUIRE(downcast<Bool>(ed2kStatus->get("searchMoreResults"))->val());
  REQUIRE_EQ(std::string("0"), getString(ed2kStatus, "uploadingPeerCount"));
  REQUIRE_EQ(std::string("0"), getString(ed2kStatus, "waitingUploadPeerCount"));
  REQUIRE_EQ(std::string("0"), getString(ed2kStatus, "peerCreditCount"));
}

} // namespace aria2::rpc
