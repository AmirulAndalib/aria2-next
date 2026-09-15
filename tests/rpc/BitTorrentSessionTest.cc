/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
#include "BtSnapshot.h"
#include "ValueBase.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include "RpcTestSupport.h"
#include "RpcMethod.h"

#include "a2doctest.h"

#include "ApplicationStatePath.h"
#include "DownloadEngine.h"
#include "SelectEventPoll.h"
#include "Option.h"
#include "RequestGroupMan.h"
#include "RequestGroup.h"
#include "rpc/RpcMethods.h"
#include "rpc/RpcStatus.h"
#include "OptionParser.h"
#include "OptionHandler.h"
#include "RpcRequest.h"
#include "RpcResponse.h"
#include "prefs.h"
#include "TestUtil.h"
#include "MessageDigest.h"
#include "DownloadContext.h"
#include "FeatureConfig.h"
#include "support/Text.h"
#include "support/Numbers.h"
#include "support/Encoding.h"
#include "support/FilePath.h"
#include "a2functional.h"
#include "array_fun.h"
#include "base64.h"
#include "download_helper.h"
#include "FileEntry.h"
#include "DefaultPieceStorage.h"
#include "Piece.h"
#include "RpcMethodFactory.h"
#include "Ed2kAttribute.h"
#include "ed2k_hash.h"
#include "ed2k_link.h"
#include "ed2k_search.h"
#ifdef ENABLE_BITTORRENT
#  include "BtDownload.h"
#  include "BtSession.h"
#  include "BtStateStore.h"
#  include "BtMetadata.h"
#endif // ENABLE_BITTORRENT

namespace aria2::rpc {

#ifdef ENABLE_BITTORRENT

TEST_CASE_FIXTURE(RpcMethodTest, "RpcMethodTest.testGetPeers")
{
  auto download = BtDownload::fromFile(A2_TEST_DIR "/test.torrent", {});
  auto dctx = std::make_shared<DownloadContext>();
  download->populateDownloadContext(dctx, option_.get());
  auto group = std::make_shared<RequestGroup>(GroupId::create(), option_);
  group->setDownloadContext(dctx);
  group->setBtDownload(download);
  e_->getRequestGroupMan()->addReservedGroup(group);

  BtPeerSnapshot peer;
  peer.peerId = "-qB5000-1234567890ab";
  peer.clientName = "qBittorrent/5.0.0";
  peer.ip = "203.0.113.1";
  peer.port = 49152;
  peer.bitfield = "80";
  peer.flags = "D U O I";
  peer.state = "connected";
  peer.downloaded = 1024;
  peer.uploaded = 512;
  peer.completedLength = 1024;
  peer.downloadSpeed = 256;
  peer.uploadSpeed = 128;
  peer.progressPpm = 500000;
  peer.incoming = true;
  peer.amInterested = true;
  peer.peerInterested = true;
  peer.optimisticUnchoke = true;
  download->mutableSnapshot().peers.push_back(std::move(peer));
  REQUIRE_EQ((size_t)1, group->getBtDownload()->snapshot().peers.size());
  REQUIRE(e_->getRequestGroupMan()->findGroup(group->getGID()) == group);

  GetPeersRpcMethod method;
  auto req = createReq(GetPeersRpcMethod::getMethodName());
  req.params->append(GroupId::toHex(group->getGID()));
  auto response = method.execute(std::move(req), e_.get());
  REQUIRE_EQ(0, response.code);
  auto result = downcast<List>(response.param);
  REQUIRE_EQ((size_t)1, result->size());
  auto entry = downcast<Dict>(result->get(0));
  REQUIRE_EQ(std::string("qBittorrent/5.0.0"),
             getString(entry, "peerClientName"));
  REQUIRE_EQ(std::string("1024"), getString(entry, "downloaded"));
  REQUIRE_EQ(std::string("512"), getString(entry, "uploaded"));
  REQUIRE_EQ(std::string("1024"), getString(entry, "completedLength"));
  REQUIRE_EQ(std::string("0.500000"), getString(entry, "progress"));
  REQUIRE_EQ(std::string("D U O I"), getString(entry, "flags"));
  REQUIRE_EQ(std::string("true"), getString(entry, "incoming"));
  REQUIRE_EQ(std::string("49152"), getString(entry, "port"));
  REQUIRE_EQ(std::string("connected"), getString(entry, "state"));
}

TEST_CASE_FIXTURE(RpcMethodTest, "RpcMethodTest.testGetBtTrackers")
{
  auto download = BtDownload::fromFile(A2_TEST_DIR "/test.torrent", {});
  auto context = std::make_shared<DownloadContext>();
  download->populateDownloadContext(context, option_.get());
  auto group = std::make_shared<RequestGroup>(GroupId::create(), option_);
  group->setDownloadContext(context);
  group->setBtDownload(download);
  e_->getRequestGroupMan()->addReservedGroup(group);

  BtTrackerSnapshot tracker;
  tracker.url = "udp://tracker.example:6969/announce";
  tracker.source = "metainfo";
  tracker.tier = 2;
  tracker.status = "working";
  tracker.seeders = 12;
  tracker.leechers = 4;
  tracker.verified = true;
  BtTrackerEndpointSnapshot trackerEndpoint;
  trackerEndpoint.localEndpoint = "192.0.2.1:6881";
  trackerEndpoint.protocol = "v1";
  trackerEndpoint.status = "working";
  trackerEndpoint.verified = true;
  tracker.endpoints.push_back(std::move(trackerEndpoint));
  download->mutableSnapshot().trackers.push_back(std::move(tracker));

  GetBtTrackersRpcMethod method;
  auto request = createReq(GetBtTrackersRpcMethod::getMethodName());
  request.params->append(GroupId::toHex(group->getGID()));
  const auto response = method.execute(std::move(request), e_.get());
  REQUIRE_EQ(0, response.code);
  const auto result = downcast<List>(response.param);
  REQUIRE_EQ((size_t)1, result->size());
  const auto entry = downcast<Dict>(result->get(0));
  REQUIRE_EQ(std::string("working"), getString(entry, "status"));
  REQUIRE_EQ(std::string("metainfo"), getString(entry, "source"));
  REQUIRE_EQ(std::string("12"), getString(entry, "seeders"));
  const auto endpoints = downcast<List>(entry->get("endpoints"));
  REQUIRE_EQ((size_t)1, endpoints->size());
  REQUIRE_EQ(std::string("v1"),
             getString(downcast<Dict>(endpoints->get(0)), "protocol"));
}

TEST_CASE_FIXTURE(RpcMethodTest, "RpcMethodTest.testReplaceBtTrackers")
{
  auto download = BtDownload::fromFile(A2_TEST_DIR "/test.torrent", {});
  auto context = std::make_shared<DownloadContext>();
  download->populateDownloadContext(context, option_.get());
  auto group = std::make_shared<RequestGroup>(GroupId::create(), option_);
  group->setDownloadContext(context);
  group->setBtDownload(download);
  download->initialize(group.get());
  e_->getRequestGroupMan()->addReservedGroup(group);
  e_->setBtSession(make_unique<BtSession>(option_.get()));

  auto trackers = List::g();
  auto first = Dict::g();
  first->put("url", "https://one.example/announce");
  first->put("tier", Integer::g(0));
  trackers->append(std::move(first));
  auto second = Dict::g();
  second->put("url", "udp://two.example:6969/announce");
  second->put("tier", Integer::g(1));
  trackers->append(std::move(second));

  ReplaceBtTrackersRpcMethod method;
  auto request = createReq(ReplaceBtTrackersRpcMethod::getMethodName());
  request.params->append(GroupId::toHex(group->getGID()));
  request.params->append(std::move(trackers));
  const auto response = method.execute(std::move(request), e_.get());
  REQUIRE_EQ(0, response.code);
  const auto& announceList = download->snapshot().announceList;
  REQUIRE_EQ((size_t)2, announceList.size());
  REQUIRE_EQ(std::string("https://one.example/announce"), announceList[0][0]);
  REQUIRE_EQ(std::string("udp://two.example:6969/announce"),
             announceList[1][0]);
}

TEST_CASE_FIXTURE(RpcMethodTest, "RpcMethodTest.testGetBtSessionStatus")
{
  e_->setBtSession(make_unique<BtSession>(option_.get()));
  ChangeGlobalOptionRpcMethod changeMethod;
  auto changeRequest = createReq(ChangeGlobalOptionRpcMethod::getMethodName());
  auto options = Dict::g();
  options->put(PREF_BT_EXTERNAL_IP->k, "203.0.113.7");
  options->put(PREF_BT_EXTERNAL_PORT->k, "62000");
  changeRequest.params->append(std::move(options));
  auto response = changeMethod.execute(std::move(changeRequest), e_.get());
  REQUIRE_EQ(0, response.code);

  GetBtSessionStatusRpcMethod getMethod;
  response = getMethod.execute(
      createReq(GetBtSessionStatusRpcMethod::getMethodName()), e_.get());
  REQUIRE_EQ(0, response.code);
  auto endpoint = downcast<Dict>(response.param);
  REQUIRE_EQ(std::string("0"), getString(endpoint, "listenPort"));
  REQUIRE_EQ(std::string("62000"), getString(endpoint, "announcePort"));
  REQUIRE_EQ(std::string("203.0.113.7"), getString(endpoint, "externalIp"));
  REQUIRE_EQ(std::string("0"), getString(endpoint, "dhtNodes"));
  REQUIRE_EQ(std::string("0"), getString(endpoint, "establishedPeers"));
  REQUIRE(endpoint->containsKey("dhtStateHealthy"));
  REQUIRE(endpoint->containsKey("listenEndpoints"));

  changeRequest = createReq(ChangeGlobalOptionRpcMethod::getMethodName());
  options = Dict::g();
  options->put(PREF_BT_EXTERNAL_IP->k, "invalid");
  changeRequest.params->append(std::move(options));
  response = changeMethod.execute(std::move(changeRequest), e_.get());
  REQUIRE_EQ(1, response.code);
  REQUIRE_EQ(std::string("203.0.113.7"), e_->getBtSession()->externalAddress());
}

TEST_CASE_FIXTURE(RpcMethodTest, "RpcMethodTest.testSetBtPeerBlocklist")
{
  e_->setBtSession(make_unique<BtSession>(option_.get()));
  SetBtPeerBlocklistRpcMethod method;
  auto req = createReq(SetBtPeerBlocklistRpcMethod::getMethodName());
  auto rules = List::g();
  rules->append("203.0.113.0/24");
  rules->append("2001:db8::1");
  req.params->append(std::move(rules));
  auto response = method.execute(std::move(req), e_.get());
  REQUIRE_EQ(0, response.code);
  auto result = downcast<Dict>(response.param);
  REQUIRE_EQ((int64_t)2, downcast<Integer>(result->get("ruleCount"))->i());
  const auto revision = downcast<Integer>(result->get("revision"))->i();

  req = createReq(SetBtPeerBlocklistRpcMethod::getMethodName());
  rules = List::g();
  rules->append("2001:db8::1");
  rules->append("203.0.113.0/24");
  req.params->append(std::move(rules));
  response = method.execute(std::move(req), e_.get());
  REQUIRE_EQ(0, response.code);
  result = downcast<Dict>(response.param);
  REQUIRE_EQ(revision, downcast<Integer>(result->get("revision"))->i());
}

TEST_CASE_FIXTURE(RpcMethodTest, "RpcMethodTest.testBtGlobalStat")
{
  auto group = std::make_shared<RequestGroup>(GroupId::create(), option_);
  auto download = BtDownload::fromFile(A2_TEST_DIR "/test.torrent", {});
  auto context = std::make_shared<DownloadContext>();
  download->populateDownloadContext(context, option_.get());
  group->setDownloadContext(context);
  group->setBtDownload(download);
  group->setPauseRequested(true);
  e_->getRequestGroupMan()->addReservedGroup(group);

  TellStatusRpcMethod tellStatus;
  auto request = createReq(TellStatusRpcMethod::getMethodName());
  request.params->append(GroupId::toHex(group->getGID()));
  auto keys = List::g();
  keys->append("downloadSpeed");
  keys->append("uploadSpeed");
  request.params->append(std::move(keys));
  auto response = tellStatus.execute(std::move(request), e_.get());
  REQUIRE_EQ(0, response.code);
  const auto task = downcast<Dict>(response.param);
  const auto taskDownloadSpeed = getString(task, "downloadSpeed");
  const auto taskUploadSpeed = getString(task, "uploadSpeed");
  REQUIRE_EQ(std::string("0"), taskDownloadSpeed);
  REQUIRE_EQ(std::string("0"), taskUploadSpeed);

  GetGlobalStatRpcMethod globalStat;
  response = globalStat.execute(
      createReq(GetGlobalStatRpcMethod::getMethodName()), e_.get());
  REQUIRE_EQ(0, response.code);
  const auto global = downcast<Dict>(response.param);
  REQUIRE_EQ(taskDownloadSpeed, getString(global, "downloadSpeed"));
  REQUIRE_EQ(taskUploadSpeed, getString(global, "uploadSpeed"));
}

#endif

} // namespace aria2::rpc
