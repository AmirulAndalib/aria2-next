/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
#include "FileEntry.h"
#include "DownloadContext.h"
#include "Option.h"
#include "GroupId.h"
#include "ValueBase.h"
#include <cstddef>
#include <utility>
#include "RpcTestSupport.h"
#include "RpcMethod.h"

#include "a2doctest.h"

#include "DownloadEngine.h"
#include "RequestGroupMan.h"
#include "RequestGroup.h"
#include "rpc/RpcMethods.h"
#include "rpc/RpcStatus.h"
#include "RpcRequest.h"
#include "RpcResponse.h"
#include "prefs.h"
#include "TestUtil.h"
#include "support/Text.h"
#include "base64.h"
#include "download_helper.h"

namespace aria2::rpc {

TEST_CASE_FIXTURE(RpcMethodTest, "RpcMethodTest.testAddUri")
{
  AddUriRpcMethod m;
  {
    auto req = createReq(AddUriRpcMethod::getMethodName());
    auto urisParam = List::g();
    urisParam->append("http://localhost/");
    req.params->append(std::move(urisParam));
    auto res = m.execute(std::move(req), e_.get());
    REQUIRE_EQ(0, res.code);
    const RequestGroupList& rgs = e_->getRequestGroupMan()->getReservedGroups();
    REQUIRE_EQ((size_t)1, rgs.size());
    REQUIRE_EQ(std::string("http://localhost/"), (*rgs.begin())
                                                     ->getDownloadContext()
                                                     ->getFirstFileEntry()
                                                     ->getRemainingUris()
                                                     .front());
  }
  {
    auto req = createReq(AddUriRpcMethod::getMethodName());
    auto urisParam = List::g();
    urisParam->append("http://localhost/");
    req.params->append(std::move(urisParam));
    // with options
    auto opt = Dict::g();
    opt->put(PREF_DIR->k, "/sink");
    req.params->append(std::move(opt));
    auto res = m.execute(std::move(req), e_.get());
    REQUIRE_EQ(0, res.code);
    a2_gid_t gid;
    REQUIRE_EQ(
        0, GroupId::toNumericId(gid, downcast<String>(res.param)->s().c_str()));
    REQUIRE_EQ(std::string("/sink"),
               findReservedGroup(e_->getRequestGroupMan().get(), gid)
                   ->getOption()
                   ->get(PREF_DIR));
  }
}

TEST_CASE_FIXTURE(RpcMethodTest, "RpcMethodTest.testAddUri_thunder")
{
  const std::string url = "https://example.com/file.bin";
  std::string payload = "AA" + url + "ZZ";
  std::string thunder =
      "thunder://" + base64::encode(payload.begin(), payload.end());
  thunder.erase(thunder.find_last_not_of('=') + 1);

  AddUriRpcMethod m;
  auto req = createReq(AddUriRpcMethod::getMethodName());
  auto urisParam = List::g();
  urisParam->append(thunder);
  req.params->append(std::move(urisParam));

  auto res = m.execute(std::move(req), e_.get());

  REQUIRE_EQ(0, res.code);
  const RequestGroupList& rgs = e_->getRequestGroupMan()->getReservedGroups();
  REQUIRE_EQ((size_t)1, rgs.size());
  REQUIRE_EQ(url, (*rgs.begin())
                      ->getDownloadContext()
                      ->getFirstFileEntry()
                      ->getRemainingUris()
                      .front());
}

TEST_CASE_FIXTURE(RpcMethodTest, "RpcMethodTest.testAddUri_badThunder")
{
  AddUriRpcMethod m;
  auto req = createReq(AddUriRpcMethod::getMethodName());
  auto urisParam = List::g();
  urisParam->append("thunder://bad!");
  req.params->append(std::move(urisParam));

  auto res = m.execute(std::move(req), e_.get());

  REQUIRE_EQ(1, res.code);
  const auto error = downcast<Dict>(res.param);
  REQUIRE(error);
  REQUIRE(util::startsWith(getString(error, "faultString"),
                           "Malformed Thunder URI"));
}

TEST_CASE_FIXTURE(RpcMethodTest,
                  "RpcMethodTest.testAddUri_acceptsJsonBoolOption")
{
  option_->put(PREF_ENABLE_RPC, A2_V_TRUE);

  AddUriRpcMethod m;
  auto req = createReq(AddUriRpcMethod::getMethodName());
  auto urisParam = List::g();
  urisParam->append("http://localhost/");
  req.params->append(std::move(urisParam));
  auto opt = Dict::g();
  opt->put(PREF_PAUSE->k, Bool::gTrue());
  req.params->append(std::move(opt));

  auto res = m.execute(std::move(req), e_.get());

  REQUIRE_EQ(0, res.code);
  const RequestGroupList& rgs = e_->getRequestGroupMan()->getReservedGroups();
  REQUIRE_EQ((size_t)1, rgs.size());
  REQUIRE((*rgs.begin())->isPauseRequested());
}

TEST_CASE_FIXTURE(RpcMethodTest, "RpcMethodTest.testAddUri_withoutUri")
{
  AddUriRpcMethod m;
  auto res = m.execute(createReq(AddUriRpcMethod::getMethodName()), e_.get());
  REQUIRE_EQ(1, res.code);
}

TEST_CASE_FIXTURE(RpcMethodTest, "RpcMethodTest.testAddUri_notUri")
{
  AddUriRpcMethod m;
  auto req = createReq(AddUriRpcMethod::getMethodName());
  auto urisParam = List::g();
  urisParam->append("not uri");
  req.params->append(std::move(urisParam));
  auto res = m.execute(std::move(req), e_.get());
  REQUIRE_EQ(1, res.code);
}

TEST_CASE_FIXTURE(RpcMethodTest, "RpcMethodTest.testAddUri_withBadOption")
{
  AddUriRpcMethod m;
  auto req = createReq(AddUriRpcMethod::getMethodName());
  auto urisParam = List::g();
  urisParam->append("http://localhost");
  req.params->append(std::move(urisParam));
  auto opt = Dict::g();
  opt->put(PREF_FILE_ALLOCATION->k, "badvalue");
  req.params->append(std::move(opt));
  auto res = m.execute(std::move(req), e_.get());
  REQUIRE_EQ(1, res.code);
}

TEST_CASE_FIXTURE(RpcMethodTest, "RpcMethodTest.testAddUri_withPosition")
{
  AddUriRpcMethod m;
  auto req1 = createReq(AddUriRpcMethod::getMethodName());
  auto urisParam1 = List::g();
  urisParam1->append("http://uri1");
  req1.params->append(std::move(urisParam1));
  auto res1 = m.execute(std::move(req1), e_.get());
  REQUIRE_EQ(0, res1.code);

  auto req2 = createReq(AddUriRpcMethod::getMethodName());
  auto urisParam2 = List::g();
  urisParam2->append("http://uri2");
  req2.params->append(std::move(urisParam2));
  req2.params->append(Dict::g());
  req2.params->append(Integer::g(0));
  m.execute(std::move(req2), e_.get());

  std::string uri = getReservedGroup(e_->getRequestGroupMan().get(), 0)
                        ->getDownloadContext()
                        ->getFirstFileEntry()
                        ->getRemainingUris()[0];

  REQUIRE_EQ(std::string("http://uri2"), uri);
}

TEST_CASE_FIXTURE(RpcMethodTest, "RpcMethodTest.testAddUri_withBadPosition")
{
  AddUriRpcMethod m;
  auto req = createReq(AddUriRpcMethod::getMethodName());
  auto urisParam = List::g();
  urisParam->append("http://localhost/");
  req.params->append(std::move(urisParam));
  req.params->append(Dict::g());
  req.params->append(Integer::g(-1));
  auto res = m.execute(std::move(req), e_.get());
  REQUIRE_EQ(1, res.code);
}

} // namespace aria2::rpc
