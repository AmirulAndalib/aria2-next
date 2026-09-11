/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
#include "GroupId.h"
#include "ValueBase.h"
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <utility>
#include <vector>
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
#include "prefs.h"
#include "TestUtil.h"
#include "DownloadContext.h"
#include "download_helper.h"
#include "FileEntry.h"
#include "DefaultPieceStorage.h"
#include "Ed2kAttribute.h"
#ifdef ENABLE_BITTORRENT
#  include "BtDownload.h"
#  include "BtSession.h"
#  include "BtStateStore.h"
#endif // ENABLE_BITTORRENT

namespace aria2::rpc {

namespace {
RpcRequest createChangeUriReq(a2_gid_t gid, size_t fileIndex)
{
  auto req = createReq(ChangeUriRpcMethod::getMethodName());

  req.params->append(GroupId::toHex(gid));   // GID
  req.params->append(Integer::g(fileIndex)); // index of FileEntry
  auto removeuris = List::g();
  removeuris->append("http://example.org/mustremove1");
  removeuris->append("http://example.org/mustremove2");
  removeuris->append("http://example.org/notexist");
  req.params->append(std::move(removeuris));
  return req;
}

RpcRequest createChangeUriEmptyReq(a2_gid_t gid, size_t fileIndex)
{
  auto req = createReq(ChangeUriRpcMethod::getMethodName());

  req.params->append(GroupId::toHex(gid));   // GID
  req.params->append(Integer::g(fileIndex)); // index of FileEntry
  req.params->append(List::g());             // remove uris
  req.params->append(List::g());             // append uris
  return req;
}
} // namespace

TEST_CASE_FIXTURE(RpcMethodTest, "RpcMethodTest.testChangePosition")
{
  e_->getRequestGroupMan()->addReservedGroup(std::make_shared<RequestGroup>(
      GroupId::create(), std::make_shared<Option>(*option_)));
  e_->getRequestGroupMan()->addReservedGroup(std::make_shared<RequestGroup>(
      GroupId::create(), std::make_shared<Option>(*option_)));

  a2_gid_t gid = getReservedGroup(e_->getRequestGroupMan().get(), 0)->getGID();
  ChangePositionRpcMethod m;
  auto req = createReq(ChangePositionRpcMethod::getMethodName());
  req.params->append(GroupId::toHex(gid));
  req.params->append(Integer::g(1));
  req.params->append("POS_SET");
  auto res = m.execute(std::move(req), e_.get());
  REQUIRE_EQ(0, res.code);
  REQUIRE_EQ((int64_t)1, downcast<Integer>(res.param)->i());
  REQUIRE_EQ(gid,
             getReservedGroup(e_->getRequestGroupMan().get(), 1)->getGID());
}

TEST_CASE_FIXTURE(RpcMethodTest, "RpcMethodTest.testChangePosition_fail")
{
  ChangePositionRpcMethod m;
  auto res =
      m.execute(createReq(ChangePositionRpcMethod::getMethodName()), e_.get());
  REQUIRE_EQ(1, res.code);

  auto req = createReq(ChangePositionRpcMethod::getMethodName());
  req.params->append("1");
  req.params->append(Integer::g(2));
  req.params->append("bad keyword");
  res = m.execute(std::move(req), e_.get());
  REQUIRE_EQ(1, res.code);
}

TEST_CASE_FIXTURE(RpcMethodTest, "RpcMethodTest.testChangeUri")
{
  std::shared_ptr<FileEntry> files[3];
  for (int i = 0; i < 3; ++i) {
    files[i].reset(new FileEntry());
  }
  files[1]->addUri("http://example.org/aria2.tar.bz2");
  files[1]->addUri("http://example.org/mustremove1");
  files[1]->addUri("http://example.org/mustremove2");
  auto dctx = std::make_shared<DownloadContext>();
  dctx->setFileEntries(&files[0], &files[3]);
  auto group = std::make_shared<RequestGroup>(GroupId::create(), option_);
  group->setDownloadContext(dctx);
  e_->getRequestGroupMan()->addReservedGroup(group);

  ChangeUriRpcMethod m;
  auto req = createChangeUriReq(group->getGID(), 2);
  auto adduris = List::g();
  adduris->append("http://example.org/added1");
  adduris->append("http://example.org/added2");
  adduris->append("baduri");
  adduris->append("http://example.org/added3");
  req.params->append(std::move(adduris));
  auto res = m.execute(std::move(req), e_.get());
  REQUIRE_EQ(0, res.code);
  REQUIRE_EQ((int64_t)2,
             downcast<Integer>(downcast<List>(res.param)->get(0))->i());
  REQUIRE_EQ((int64_t)3,
             downcast<Integer>(downcast<List>(res.param)->get(1))->i());
  REQUIRE_EQ((size_t)0, files[0]->getRemainingUris().size());
  REQUIRE_EQ((size_t)0, files[2]->getRemainingUris().size());
  std::deque<std::string> uris = files[1]->getRemainingUris();
  REQUIRE_EQ((size_t)4, uris.size());
  REQUIRE_EQ(std::string("http://example.org/aria2.tar.bz2"), uris[0]);
  REQUIRE_EQ(std::string("http://example.org/added1"), uris[1]);
  REQUIRE_EQ(std::string("http://example.org/added2"), uris[2]);
  REQUIRE_EQ(std::string("http://example.org/added3"), uris[3]);

  req = createChangeUriReq(group->getGID(), 2);
  // Change adduris
  adduris = List::g();
  adduris->append("http://example.org/added1-1");
  adduris->append("http://example.org/added1-2");
  req.params->append(std::move(adduris));
  // Set position parameter
  req.params->append(Integer::g(2));
  res = m.execute(std::move(req), e_.get());
  REQUIRE_EQ(0, res.code);
  REQUIRE_EQ((int64_t)0,
             downcast<Integer>(downcast<List>(res.param)->get(0))->i());
  REQUIRE_EQ((int64_t)2,
             downcast<Integer>(downcast<List>(res.param)->get(1))->i());
  uris = files[1]->getRemainingUris();
  REQUIRE_EQ((size_t)6, uris.size());
  REQUIRE_EQ(std::string("http://example.org/added1-1"), uris[2]);
  REQUIRE_EQ(std::string("http://example.org/added1-2"), uris[3]);

  // Change index of FileEntry
  req = createChangeUriReq(group->getGID(), 1);
  adduris = List::g();
  adduris->append("http://example.org/added1-1");
  adduris->append("http://example.org/added1-2");
  req.params->append(std::move(adduris));
  // Set position far beyond the size of uris in FileEntry.
  req.params->append(Integer::g(1000));
  res = m.execute(std::move(req), e_.get());
  REQUIRE_EQ(0, res.code);
  REQUIRE_EQ((int64_t)0,
             downcast<Integer>(downcast<List>(res.param)->get(0))->i());
  REQUIRE_EQ((int64_t)2,
             downcast<Integer>(downcast<List>(res.param)->get(1))->i());
  uris = files[0]->getRemainingUris();
  REQUIRE_EQ((size_t)2, uris.size());
  REQUIRE_EQ(std::string("http://example.org/added1-1"), uris[0]);
  REQUIRE_EQ(std::string("http://example.org/added1-2"), uris[1]);
}

TEST_CASE_FIXTURE(RpcMethodTest, "RpcMethodTest.testChangeUri_fail")
{
  std::shared_ptr<FileEntry> files[3];
  for (int i = 0; i < 3; ++i) {
    files[i] = std::make_shared<FileEntry>();
  }
  auto dctx = std::make_shared<DownloadContext>();
  dctx->setFileEntries(&files[0], &files[3]);
  auto group = std::make_shared<RequestGroup>(GroupId::create(), option_);
  group->setDownloadContext(dctx);
  e_->getRequestGroupMan()->addReservedGroup(group);

  ChangeUriRpcMethod m;
  auto req = createChangeUriEmptyReq(group->getGID(), 1);
  auto res = m.execute(std::move(req), e_.get());
  REQUIRE_EQ(0, res.code);

  req = createChangeUriEmptyReq(group->getGID(), 0);
  res = m.execute(std::move(req), e_.get());
  // RPC request fails because 2nd argument is less than 1.
  REQUIRE_EQ(1, res.code);

  req = createChangeUriEmptyReq(GroupId::create()->getNumericId(), 1);
  res = m.execute(std::move(req), e_.get());
  // RPC request fails because the given GID does not exist.
  REQUIRE_EQ(1, res.code);

  req = createChangeUriEmptyReq(group->getGID(), 4);
  res = m.execute(std::move(req), e_.get());
  // RPC request fails because FileEntry#3 does not exist.
  REQUIRE_EQ(1, res.code);

  req = createChangeUriEmptyReq(group->getGID(), 1);
  req.params->set(1, String::g("0"));
  res = m.execute(std::move(req), e_.get());
  // RPC request fails because index of FileEntry is string.
  REQUIRE_EQ(1, res.code);

  req = createChangeUriEmptyReq(group->getGID(), 1);
  req.params->set(2, String::g("http://url"));
  res = m.execute(std::move(req), e_.get());
  // RPC request fails because 3rd param is not list.
  REQUIRE_EQ(1, res.code);

  req = createChangeUriEmptyReq(group->getGID(), 1);
  req.params->set(2, List::g());
  req.params->set(3, String::g("http://url"));
  res = m.execute(std::move(req), e_.get());
  // RPC request fails because 4th param is not list.
  REQUIRE_EQ(1, res.code);
}

TEST_CASE_FIXTURE(RpcMethodTest, "RpcMethodTest.testPause")
{
  std::vector<std::string> uris{
      "http://url1",
      "http://url2",
      "http://url3",
  };
  option_->put(PREF_FORCE_SEQUENTIAL, A2_V_TRUE);
  std::vector<std::shared_ptr<RequestGroup>> groups;
  createRequestGroupForUri(groups, option_, uris);
  REQUIRE_EQ((size_t)3, groups.size());
  e_->getRequestGroupMan()->addReservedGroup(groups);
  {
    PauseRpcMethod m;
    auto req = createReq(PauseRpcMethod::getMethodName());
    req.params->append(GroupId::toHex(groups[0]->getGID()));
    auto res = m.execute(std::move(req), e_.get());
    REQUIRE_EQ(0, res.code);
  }
  REQUIRE(groups[0]->isPauseRequested());
  {
    UnpauseRpcMethod m;
    auto req = createReq(UnpauseRpcMethod::getMethodName());
    req.params->append(GroupId::toHex(groups[0]->getGID()));
    auto res = m.execute(std::move(req), e_.get());
    REQUIRE_EQ(0, res.code);
  }
  REQUIRE(!groups[0]->isPauseRequested());
  {
    PauseAllRpcMethod m;
    auto req = createReq(PauseAllRpcMethod::getMethodName());
    auto res = m.execute(std::move(req), e_.get());
    REQUIRE_EQ(0, res.code);
  }
  for (size_t i = 0; i < groups.size(); ++i) {
    REQUIRE(groups[i]->isPauseRequested());
  }
  {
    UnpauseAllRpcMethod m;
    auto req = createReq(UnpauseAllRpcMethod::getMethodName());
    auto res = m.execute(std::move(req), e_.get());
    REQUIRE_EQ(0, res.code);
  }
  for (size_t i = 0; i < groups.size(); ++i) {
    REQUIRE(!groups[i]->isPauseRequested());
  }
  {
    ForcePauseAllRpcMethod m;
    auto req = createReq(ForcePauseAllRpcMethod::getMethodName());
    auto res = m.execute(std::move(req), e_.get());
    REQUIRE_EQ(0, res.code);
  }
  for (size_t i = 0; i < groups.size(); ++i) {
    REQUIRE(groups[i]->isPauseRequested());
  }
}

} // namespace aria2::rpc
