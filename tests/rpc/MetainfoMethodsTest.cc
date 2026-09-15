/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
#include "GroupId.h"
#include "ValueBase.h"
#include "error_code.h"
#include <cstddef>
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

namespace {
#ifdef ENABLE_BITTORRENT
RpcRequest createAddTorrentReq()
{
  auto req = createReq(AddTorrentRpcMethod::getMethodName());
  req.params->append(readFile(A2_TEST_DIR "/single.torrent"));
  auto uris = List::g();
  uris->append("http://localhost/aria2-0.8.2.tar.bz2");
  req.params->append(std::move(uris));
  return req;
}
#endif
} // namespace

#ifdef ENABLE_BITTORRENT

TEST_CASE_FIXTURE(RpcMethodTest, "RpcMethodTest.testAddTorrent")
{
  AddTorrentRpcMethod m;
  auto add = [&]() {
    auto response = m.execute(createAddTorrentReq(), e_.get());
    REQUIRE_EQ(0, response.code);
    a2_gid_t gid;
    REQUIRE_EQ(0, GroupId::toNumericId(
                      gid, downcast<String>(response.param)->s().c_str()));
    return gid;
  };

  auto* manager = e_->getRequestGroupMan().get();
  const auto firstGid = add();
  auto first = findReservedGroup(manager, firstGid);
  REQUIRE(first);
  const auto firstState = first->getBtDownload()->stateReference();
  REQUIRE(File(firstState.metadataPath).isFile());
  REQUIRE_EQ(state::btTorrentDirectory(option_.get()),
             File(firstState.metadataPath).getDirname());
  REQUIRE_EQ(e_->getOption()->get(PREF_DIR) + "/aria2-0.8.2.tar.bz2",
             first->getFirstFilePath());
  REQUIRE_EQ((size_t)0, first->getDownloadContext()
                            ->getFirstFileEntry()
                            ->getRemainingUris()
                            .size());
  REQUIRE(!File(util::applyDir(option_->get(PREF_DIR),
                               File(firstState.metadataPath).getBasename()))
               .exists());

  BtStateStore::writeResume(firstState.resumePath, "resume", 6);
  const auto secondGid = add();
  auto second = findReservedGroup(manager, secondGid);
  REQUIRE(second);
  REQUIRE_EQ(firstState.metadataPath,
             second->getBtDownload()->stateReference().metadataPath);
  REQUIRE_EQ(firstState.resumePath,
             second->getBtDownload()->stateReference().resumePath);

  REQUIRE(manager->removeReservedGroup(firstGid));
  REQUIRE(File(firstState.metadataPath).exists());
  REQUIRE(File(firstState.resumePath).exists());

  auto stopped = second->createDownloadResult();
  manager->addDownloadResult(stopped);
  REQUIRE(manager->removeReservedGroup(secondGid));
  REQUIRE(File(firstState.metadataPath).exists());
  REQUIRE(File(firstState.resumePath).exists());
  REQUIRE(manager->removeDownloadResult(secondGid));
  REQUIRE(!File(firstState.metadataPath).exists());
  REQUIRE(!File(firstState.resumePath).exists());
  REQUIRE(File(A2_TEST_DIR "/single.torrent").exists());

  {
    auto req = createAddTorrentReq();
    std::string dir = A2_TEST_OUT_DIR "/aria2_RpcMethodTest_testAddTorrent";
    File(dir).mkdirs();
    auto opt = Dict::g();
    opt->put(PREF_DIR->k, dir);
    req.params->append(std::move(opt));

    auto res = m.execute(std::move(req), e_.get());
    REQUIRE_EQ(0, res.code);
    a2_gid_t gid;
    REQUIRE_EQ(
        0, GroupId::toNumericId(gid, downcast<String>(res.param)->s().c_str()));
    REQUIRE_EQ(dir + "/aria2-0.8.2.tar.bz2",
               findReservedGroup(e_->getRequestGroupMan().get(), gid)
                   ->getFirstFilePath());
    auto group = findReservedGroup(manager, gid);
    const auto state = group->getBtDownload()->stateReference();
    REQUIRE(!File(util::applyDir(dir, File(state.metadataPath).getBasename()))
                 .exists());
    group->getBtDownload()->mutableSnapshot().complete = true;
    group->getBtDownload()->mutableSnapshot().selectedComplete = true;
    auto completed = group->createDownloadResult();
    REQUIRE_EQ(error_code::FINISHED, completed->result);
    manager->addDownloadResult(completed);
    REQUIRE(manager->removeReservedGroup(gid));
    REQUIRE(File(state.metadataPath).exists());
    manager->purgeDownloadResult();
    REQUIRE(!File(state.metadataPath).exists());
  }
}

TEST_CASE_FIXTURE(RpcMethodTest, "RpcMethodTest.testAddTorrent_withoutTorrent")
{
  AddTorrentRpcMethod m;
  auto res =
      m.execute(createReq(AddTorrentRpcMethod::getMethodName()), e_.get());
  REQUIRE_EQ(1, res.code);
}

TEST_CASE_FIXTURE(RpcMethodTest,
                  "RpcMethodTest.testAddTorrent_notBase64Torrent")
{
  AddTorrentRpcMethod m;
  auto req = createReq(AddTorrentRpcMethod::getMethodName());
  req.params->append("not torrent");
  auto res = m.execute(std::move(req), e_.get());
  REQUIRE_EQ(1, res.code);
}

TEST_CASE_FIXTURE(RpcMethodTest, "RpcMethodTest.testAddTorrent_withPosition")
{
  AddTorrentRpcMethod m;
  auto req1 = createReq(AddTorrentRpcMethod::getMethodName());
  req1.params->append(readFile(A2_TEST_DIR "/test.torrent"));
  req1.params->append(List::g());
  req1.params->append(Dict::g());
  auto res1 = m.execute(std::move(req1), e_.get());
  REQUIRE_EQ(0, res1.code);

  auto req2 = createReq(AddTorrentRpcMethod::getMethodName());
  req2.params->append(readFile(A2_TEST_DIR "/single.torrent"));
  req2.params->append(List::g());
  req2.params->append(Dict::g());
  req2.params->append(Integer::g(0));
  m.execute(std::move(req2), e_.get());

  REQUIRE_EQ((size_t)1, getReservedGroup(e_->getRequestGroupMan().get(), 0)
                            ->getDownloadContext()
                            ->getFileEntries()
                            .size());
}

TEST_CASE_FIXTURE(RpcMethodTest, "RpcMethodTest.testInspectTorrent")
{
  const std::string output =
      A2_TEST_OUT_DIR "/aria2_RpcMethodTest_inspectTorrent";
  const std::string state = output + "/state";
  option_->put(PREF_DIR, output);
  option_->put(PREF_STATE_DIR, state);
  REQUIRE(!File(output).exists());
  REQUIRE(!File(state).exists());
  REQUIRE(!e_->getBtSession());

  InspectTorrentRpcMethod method;
  auto request = createReq(InspectTorrentRpcMethod::getMethodName());
  const auto torrent = readFile(A2_TEST_DIR "/single.torrent");
  request.params->append(base64::encode(torrent.begin(), torrent.end()));
  request.jsonRpc = true;
  const auto response = method.execute(std::move(request), e_.get());

  REQUIRE_EQ(0, response.code);
  const auto result = downcast<Dict>(response.param);
  REQUIRE_EQ(std::string("aria2-0.8.2.tar.bz2"), getString(result, "name"));
  REQUIRE_EQ(std::string("single"), getString(result, "mode"));
  REQUIRE_EQ((size_t)40, getString(result, "infoHashV1").size());
  REQUIRE(getString(result, "infoHashV2").empty());
  REQUIRE_EQ(std::string("384"), getString(result, "totalLength"));
  const auto files = downcast<List>(result->get("files"));
  REQUIRE_EQ((size_t)1, files->size());
  const auto file = downcast<Dict>(files->get(0));
  REQUIRE_EQ(std::string("1"), getString(file, "index"));
  REQUIRE_EQ(std::string("aria2-0.8.2.tar.bz2"), getString(file, "path"));
  REQUIRE_EQ(std::string("384"), getString(file, "length"));

  const auto manager = e_->getRequestGroupMan().get();
  REQUIRE_EQ((size_t)0, manager->getRequestGroups().size());
  REQUIRE_EQ((size_t)0, manager->getReservedGroups().size());
  REQUIRE_EQ((size_t)0, manager->getDownloadResults().size());
  REQUIRE(!e_->getBtSession());
  REQUIRE(!File(output).exists());
  REQUIRE(!File(state).exists());
}

TEST_CASE_FIXTURE(RpcMethodTest, "RpcMethodTest.testInspectTorrentErrors")
{
  InspectTorrentRpcMethod method;
  const auto inspect = [&method, this](std::string payload) {
    auto request = createReq(InspectTorrentRpcMethod::getMethodName());
    request.params->append(std::move(payload));
    request.jsonRpc = true;
    return method.execute(std::move(request), e_.get());
  };

  auto response = inspect("%%%");
  REQUIRE_EQ(1, response.code);
  auto error = downcast<Dict>(response.param);
  auto data = downcast<Dict>(error->get("data"));
  REQUIRE_EQ(std::string("invalidBase64"), getString(data, "kind"));
  REQUIRE_EQ(std::string("rpc"), getString(data, "category"));

  const std::string corrupt = "not torrent metadata";
  response = inspect(base64::encode(corrupt.begin(), corrupt.end()));
  REQUIRE_EQ(1, response.code);
  error = downcast<Dict>(response.param);
  data = downcast<Dict>(error->get("data"));
  REQUIRE_EQ(std::string("invalidTorrent"), getString(data, "kind"));
  REQUIRE(!getString(data, "category").empty());
  REQUIRE(downcast<Integer>(data->get("code"))->i() != 0);
}

#endif

} // namespace aria2::rpc
