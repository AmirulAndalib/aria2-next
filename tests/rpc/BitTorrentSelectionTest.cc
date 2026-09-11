/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
#include "BtSnapshot.h"
#include "ValueBase.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>
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

TEST_CASE_FIXTURE(RpcMethodTest, "RpcMethodTest.testBtFileSelectionGate")
{
  auto taskOption = std::make_shared<Option>();
  OptionParser::getInstance()->parseDefaultValues(*taskOption);
  taskOption->put(PREF_DIR, option_->get(PREF_DIR));
  taskOption->put(PREF_ENABLE_RPC, A2_V_TRUE);
  taskOption->put(PREF_PAUSE_METADATA, A2_V_TRUE);
  auto group = std::make_shared<RequestGroup>(GroupId::create(), taskOption);
  auto download = BtDownload::fromFile(A2_TEST_DIR "/test.torrent", {});
  auto context = std::make_shared<DownloadContext>();
  download->configure(taskOption.get());
  download->populateDownloadContext(context, taskOption.get());
  download->updateSelection(context);
  download->setGroup(group.get());
  group->setDownloadContext(context);
  group->setBtDownload(download);
  group->setPauseRequested(true);
  download->beginFileSelectionPause();
  download->requestStop(BtDownload::StopReason::FileSelection);
  download->finishStopping();
  e_->getRequestGroupMan()->addReservedGroup(group);

  REQUIRE(download->awaitingFileSelection());
  REQUIRE_EQ(BtSnapshot::State::Paused, download->snapshot().state);
  download->applyTransportState(BtSnapshot::State::Downloading);
  REQUIRE_EQ(BtSnapshot::State::Paused, download->snapshot().state);
  BtErrorSnapshot injectedError;
  injectedError.present = true;
  injectedError.recoverable = true;
  injectedError.kind = "storage";
  injectedError.message = "file too short";
  download->setError(std::move(injectedError));
  REQUIRE(download->awaitingFileSelection());
  REQUIRE_EQ(BtSnapshot::State::Error, download->snapshot().state);

  GetFilesRpcMethod getFiles;
  auto request = createReq(GetFilesRpcMethod::getMethodName());
  request.params->append(GroupId::toHex(group->getGID()));
  auto response = getFiles.execute(std::move(request), e_.get());
  REQUIRE_EQ(0, response.code);
  REQUIRE_EQ((size_t)2, downcast<List>(response.param)->size());

  TellWaitingRpcMethod tellWaiting;
  request = createReq(TellWaitingRpcMethod::getMethodName());
  request.params->append(Integer::g(0));
  request.params->append(Integer::g(1));
  auto waitingKeys = List::g();
  waitingKeys->append("status");
  waitingKeys->append("seeder");
  waitingKeys->append("files");
  waitingKeys->append("bittorrent");
  request.params->append(std::move(waitingKeys));
  response = tellWaiting.execute(std::move(request), e_.get());
  REQUIRE_EQ(0, response.code);
  const auto waiting = downcast<List>(response.param);
  REQUIRE_EQ((size_t)1, waiting->size());
  const auto waitingTask = downcast<Dict>(waiting->get(0));
  REQUIRE_EQ(std::string("paused"), getString(waitingTask, "status"));
  REQUIRE_EQ(std::string("false"), getString(waitingTask, "seeder"));
  REQUIRE_EQ((size_t)2, downcast<List>(waitingTask->get("files"))->size());
  const auto waitingBt = downcast<Dict>(waitingTask->get("bittorrent"));
  REQUIRE_EQ(std::string("error"), getString(waitingBt, "state"));
  REQUIRE_EQ(std::string("awaiting"),
             getString(waitingBt, "fileSelectionState"));
  REQUIRE_EQ(std::string("0.000000"), getString(waitingBt, "progress"));
  REQUIRE(waitingBt->containsKey("error"));
  REQUIRE_EQ(std::string("true"),
             getString(downcast<Dict>(waitingBt->get("error")), "recoverable"));

  UnpauseRpcMethod unpause;
  request = createReq(UnpauseRpcMethod::getMethodName());
  request.params->append(GroupId::toHex(group->getGID()));
  response = unpause.execute(std::move(request), e_.get());
  REQUIRE_EQ(1, response.code);
  const auto unpauseError = downcast<Dict>(response.param);
  const auto errorKey =
      unpauseError->containsKey("message") ? "message" : "faultString";
  REQUIRE(
      getString(unpauseError, errorKey).find("awaiting a valid select-file") !=
      std::string::npos);
  REQUIRE(group->isPauseRequested());
  REQUIRE(download->awaitingFileSelection());
  REQUIRE(taskOption->getAsBool(PREF_PAUSE_METADATA));

  ChangeOptionRpcMethod changeOption;
  request = createReq(ChangeOptionRpcMethod::getMethodName());
  request.params->append(GroupId::toHex(group->getGID()));
  auto emptyOptions = Dict::g();
  emptyOptions->put(PREF_SELECT_FILE->k, "");
  request.params->append(std::move(emptyOptions));
  response = changeOption.execute(std::move(request), e_.get());
  REQUIRE_EQ(1, response.code);
  REQUIRE(download->awaitingFileSelection());

  request = createReq(ChangeOptionRpcMethod::getMethodName());
  request.params->append(GroupId::toHex(group->getGID()));
  auto invalidOptions = Dict::g();
  invalidOptions->put(PREF_SELECT_FILE->k, "3");
  request.params->append(std::move(invalidOptions));
  response = changeOption.execute(std::move(request), e_.get());
  REQUIRE_EQ(1, response.code);
  REQUIRE(download->awaitingFileSelection());

  request = createReq(ChangeOptionRpcMethod::getMethodName());
  request.params->append(GroupId::toHex(group->getGID()));
  auto options = Dict::g();
  options->put(PREF_SELECT_FILE->k, "2");
  request.params->append(std::move(options));
  response = changeOption.execute(std::move(request), e_.get());
  REQUIRE_EQ(0, response.code);
  REQUIRE(!download->awaitingFileSelection());
  REQUIRE(download->fileSelectionReady());
  REQUIRE_EQ(BtSnapshot::State::Error, download->snapshot().state);

  request = createReq(UnpauseRpcMethod::getMethodName());
  request.params->append(GroupId::toHex(group->getGID()));
  response = unpause.execute(std::move(request), e_.get());
  REQUIRE_EQ(0, response.code);
  REQUIRE(!group->isPauseRequested());
  REQUIRE(download->fileSelectionApplying());
  REQUIRE(!taskOption->getAsBool(PREF_PAUSE_METADATA));

  download->failFileSelectionApply();
  group->setPauseRequested(true);
  REQUIRE(download->fileSelectionReady());
  request = createReq(UnpauseRpcMethod::getMethodName());
  request.params->append(GroupId::toHex(group->getGID()));
  response = unpause.execute(std::move(request), e_.get());
  REQUIRE_EQ(0, response.code);
  REQUIRE(download->fileSelectionApplying());
  REQUIRE(!taskOption->getAsBool(PREF_PAUSE_METADATA));
}

TEST_CASE_FIXTURE(RpcMethodTest, "RpcMethodTest.testBtSharingContract")
{
  auto taskOption = std::make_shared<Option>(*option_);
  auto group = std::make_shared<RequestGroup>(GroupId::create(), taskOption);
  auto download = BtDownload::fromFile(A2_TEST_DIR "/test.torrent", {});
  auto context = std::make_shared<DownloadContext>();
  download->configure(taskOption.get());
  download->populateDownloadContext(context, taskOption.get());
  download->setGroup(group.get());
  group->setDownloadContext(context);
  group->setBtDownload(download);
  group->setState(RequestGroup::STATE_ACTIVE);
  e_->getRequestGroupMan()->addReservedGroup(group);

  auto& snapshot = download->mutableSnapshot();
  snapshot.state = BtSnapshot::State::Finished;
  snapshot.selectedComplete = true;
  snapshot.complete = false;
  snapshot.activeTime = 120;
  snapshot.finishedTime = 45;
  snapshot.seedingTime = 0;

  TellStatusRpcMethod tellStatus;
  auto query = [&]() {
    auto request = createReq(TellStatusRpcMethod::getMethodName());
    request.params->append(GroupId::toHex(group->getGID()));
    auto keys = List::g();
    keys->append("status");
    keys->append("seeder");
    keys->append("bittorrent");
    request.params->append(std::move(keys));
    auto response = tellStatus.execute(std::move(request), e_.get());
    REQUIRE_EQ(0, response.code);
    return response;
  };

  auto response = query();
  auto result = downcast<Dict>(response.param);
  REQUIRE_EQ(std::string("active"), getString(result, "status"));
  REQUIRE_EQ(std::string("true"), getString(result, "seeder"));
  auto bt = downcast<Dict>(result->get("bittorrent"));
  REQUIRE_EQ(std::string("finished"), getString(bt, "state"));
  REQUIRE_EQ(std::string("45"), getString(bt, "finishedTime"));
  REQUIRE_EQ(std::string("0"), getString(bt, "seedingTime"));

  snapshot.state = BtSnapshot::State::Seeding;
  snapshot.complete = true;
  snapshot.finishedTime = 62;
  snapshot.seedingTime = 17;
  response = query();
  result = downcast<Dict>(response.param);
  REQUIRE_EQ(std::string("true"), getString(result, "seeder"));
  bt = downcast<Dict>(result->get("bittorrent"));
  REQUIRE_EQ(std::string("seeding"), getString(bt, "state"));
  REQUIRE_EQ(std::string("62"), getString(bt, "finishedTime"));
  REQUIRE_EQ(std::string("17"), getString(bt, "seedingTime"));

  group->setState(RequestGroup::STATE_WAITING);
  group->setPauseRequested(true);
  response = query();
  result = downcast<Dict>(response.param);
  REQUIRE_EQ(std::string("paused"), getString(result, "status"));
  REQUIRE_EQ(std::string("true"), getString(result, "seeder"));
}

TEST_CASE_FIXTURE(RpcMethodTest, "RpcMethodTest.testBtResumeProgressAuthority")
{
  auto taskOption = std::make_shared<Option>();
  OptionParser::getInstance()->parseDefaultValues(*taskOption);
  taskOption->put(PREF_DIR, option_->get(PREF_DIR));
  auto group = std::make_shared<RequestGroup>(GroupId::create(), taskOption);
  auto download = BtDownload::fromFile(A2_TEST_DIR "/test.torrent", {});
  auto context = std::make_shared<DownloadContext>();
  download->configure(taskOption.get());
  download->populateDownloadContext(context, taskOption.get());
  download->setGroup(group.get());
  group->setDownloadContext(context);
  group->setBtDownload(download);

  auto& snapshot = download->mutableSnapshot();
  REQUIRE_EQ((size_t)2, snapshot.files.size());
  snapshot.files[0].completedLength = 200;
  snapshot.files[1].completedLength = 50;
  download->updateSelection(context);
  snapshot.progressPpm = 650000;

  download->requestStop(BtDownload::StopReason::Pause);
  download->finishStopping();
  group->setPauseRequested(true);
  e_->getRequestGroupMan()->addReservedGroup(group);

  const auto assertProgress = [&](int64_t completed,
                                  const std::vector<int64_t>& files,
                                  const std::string& progress) {
    TellStatusRpcMethod tellStatus;
    auto request = createReq(TellStatusRpcMethod::getMethodName());
    request.params->append(GroupId::toHex(group->getGID()));
    auto keys = List::g();
    keys->append("completedLength");
    keys->append("files");
    keys->append("bittorrent");
    request.params->append(std::move(keys));
    auto response = tellStatus.execute(std::move(request), e_.get());
    REQUIRE_EQ(0, response.code);
    const auto task = downcast<Dict>(response.param);
    REQUIRE_EQ(util::itos(completed), getString(task, "completedLength"));
    const auto rpcFiles = downcast<List>(task->get("files"));
    REQUIRE_EQ(files.size(), rpcFiles->size());
    for (size_t i = 0; i < files.size(); ++i) {
      REQUIRE_EQ(
          util::itos(files[i]),
          getString(downcast<Dict>(rpcFiles->get(i)), "completedLength"));
    }
    REQUIRE_EQ(progress,
               getString(downcast<Dict>(task->get("bittorrent")), "progress"));
  };

  assertProgress(250, {200, 50}, "0.650000");
  download->prepareStart();
  group->setPauseRequested(false);
  download->beginProgressVerification();
  download->applyFileProgress({0, 0});
  assertProgress(250, {200, 50}, "0.650000");

  download->beginProgressRefresh();
  download->applyFileProgress({0, 0});
  assertProgress(0, {0, 0}, "0.000000");

  download->beginProgressRefresh();
  download->applyFileProgress({220, 80});
  assertProgress(300, {220, 80}, "0.781250");

  download->beginProgressVerification();
  download->applyFileProgress({0, 0});
  assertProgress(300, {220, 80}, "0.781250");

  download->beginProgressRefresh();
  download->applyFileProgress({100, 20});
  assertProgress(120, {100, 20}, "0.312500");

  const auto firstLength = snapshot.files[0].length;
  const auto secondLength = snapshot.files[1].length;
  download->beginProgressRefresh();
  download->applyNativeCompletion(false, false);
  download->applyFileProgress({firstLength, secondLength});
  assertProgress(firstLength + secondLength, {firstLength, secondLength},
                 "1.000000");
  REQUIRE(!snapshot.selectedComplete);
  download->applyNativeCompletion(true, false);
  REQUIRE(snapshot.selectedComplete);
}

#endif

} // namespace aria2::rpc
