/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
#include "ValueBase.h"
#include "timegm.h"
#include <cstddef>
#include <cstdint>
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
#include "OptionParser.h"
#include "OptionHandler.h"
#include "RpcRequest.h"
#include "RpcResponse.h"
#include "prefs.h"
#include "TestUtil.h"
#include "FeatureConfig.h"
#include "support/Numbers.h"
#include "support/Encoding.h"
#include "download_helper.h"
#include "DefaultPieceStorage.h"
#include "RpcMethodFactory.h"
#include "Ed2kAttribute.h"
#ifdef ENABLE_BITTORRENT
#  include "BtDownload.h"
#  include "BtSession.h"
#  include "BtStateStore.h"
#endif // ENABLE_BITTORRENT

namespace aria2::rpc {

TEST_CASE_FIXTURE(RpcMethodTest, "RpcMethodTest.testAuthorize")
{
  // Select RPC method which takes non-string parameter to make sure
  // that token: prefixed parameter is stripped before the call.
  TellActiveRpcMethod m;
  // no secret token set and no token: prefixed parameter is given
  {
    auto req = createReq(TellActiveRpcMethod::getMethodName());
    auto res = m.execute(std::move(req), e_.get());
    REQUIRE_EQ(0, res.code);
  }
  // no secret token set and token: prefixed parameter is given
  {
    auto req = createReq(GetVersionRpcMethod::getMethodName());
    req.params->append("token:foo");
    auto res = m.execute(std::move(req), e_.get());
    REQUIRE_EQ(0, res.code);
  }
  e_->getOption()->put(PREF_RPC_SECRET, "foo");
  // secret token set and no token: prefixed parameter is given
  {
    auto req = createReq(GetVersionRpcMethod::getMethodName());
    auto res = m.execute(std::move(req), e_.get());
    REQUIRE_EQ(1, res.code);
  }
  // secret token set and token: prefixed parameter is given
  {
    auto req = createReq(GetVersionRpcMethod::getMethodName());
    req.params->append("token:foo");
    auto res = m.execute(std::move(req), e_.get());
    REQUIRE_EQ(0, res.code);
  }
  // secret token set and bad token: prefixed parameter is given
  {
    auto req = createReq(GetVersionRpcMethod::getMethodName());
    req.params->append("token:foo2");
    auto res = m.execute(std::move(req), e_.get());
    REQUIRE_EQ(1, res.code);
  }
}

TEST_CASE_FIXTURE(RpcMethodTest, "RpcMethodTest.testNoSuchMethod")
{
  NoSuchMethodRpcMethod m;
  auto res = m.execute(createReq("make.hamburger"), e_.get());
  REQUIRE_EQ(1, res.code);
  REQUIRE_EQ(std::string("No such method: make.hamburger"),
             getString(downcast<Dict>(res.param), "faultString"));
}

TEST_CASE_FIXTURE(RpcMethodTest, "RpcMethodTest.testGetVersion")
{
  GetVersionRpcMethod m;
  auto res =
      m.execute(createReq(GetVersionRpcMethod::getMethodName()), e_.get());
  REQUIRE_EQ(0, res.code);
  const Dict* resParams = downcast<Dict>(res.param);
  REQUIRE_EQ(std::string("aria2-next"), getString(resParams, "product"));
  REQUIRE_EQ(std::string(PACKAGE_VERSION), getString(resParams, "version"));
  REQUIRE_EQ(std::string("1.1.0"), getString(resParams, "rpcVersion"));
  const List* featureList = downcast<List>(resParams->get("enabledFeatures"));
  std::string features;
  for (auto i = featureList->begin(); i != featureList->end(); ++i) {
    const String* s = downcast<String>(*i);
    features += s->s();
    features += ", ";
  }
  REQUIRE_EQ(featureSummary() + ", ", features);
}

TEST_CASE_FIXTURE(RpcMethodTest, "RpcMethodTest.testGetSessionInfo")
{
  GetSessionInfoRpcMethod m;
  auto res =
      m.execute(createReq(GetSessionInfoRpcMethod::getMethodName()), e_.get());
  REQUIRE_EQ(0, res.code);
  REQUIRE_EQ(util::toHex(e_->getSessionId()),
             getString(downcast<Dict>(res.param), "sessionId"));
}

TEST_CASE_FIXTURE(RpcMethodTest, "RpcMethodTest.testSystemMulticall")
{
  SystemMulticallRpcMethod m;
  auto req = createReq("system.multicall");
  auto reqparams = List::g();
  for (int i = 0; i < 2; ++i) {
    auto dict = Dict::g();
    dict->put("methodName", AddUriRpcMethod::getMethodName());
    auto params = List::g();
    auto urisParam = List::g();
    urisParam->append("http://localhost/" + util::itos(i));
    params->append(std::move(urisParam));
    dict->put("params", std::move(params));
    reqparams->append(std::move(dict));
  }
  {
    auto dict = Dict::g();
    dict->put("methodName", "not exists");
    dict->put("params", List::g());
    reqparams->append(std::move(dict));
  }
  {
    reqparams->append("not struct");
  }
  {
    auto dict = Dict::g();
    dict->put("methodName", "system.multicall");
    dict->put("params", List::g());
    reqparams->append(std::move(dict));
  }
  {
    // missing params
    auto dict = Dict::g();
    dict->put("methodName", GetVersionRpcMethod::getMethodName());
    reqparams->append(std::move(dict));
  }
  {
    auto dict = Dict::g();
    dict->put("methodName", GetVersionRpcMethod::getMethodName());
    dict->put("params", List::g());
    reqparams->append(std::move(dict));
  }
  req.params->append(std::move(reqparams));
  auto res = m.execute(std::move(req), e_.get());
  REQUIRE_EQ(0, res.code);
  const List* resParams = downcast<List>(res.param);
  REQUIRE_EQ((size_t)7, resParams->size());
  auto& rgman = e_->getRequestGroupMan();
  REQUIRE_EQ(GroupId::toHex(getReservedGroup(rgman.get(), 0)->getGID()),
             downcast<String>(downcast<List>(resParams->get(0))->get(0))->s());
  REQUIRE_EQ(GroupId::toHex(getReservedGroup(rgman.get(), 1)->getGID()),
             downcast<String>(downcast<List>(resParams->get(1))->get(0))->s());
  REQUIRE_EQ(
      (int64_t)1,
      downcast<Integer>(downcast<Dict>(resParams->get(2))->get("faultCode"))
          ->i());
  REQUIRE_EQ(
      (int64_t)1,
      downcast<Integer>(downcast<Dict>(resParams->get(3))->get("faultCode"))
          ->i());
  REQUIRE_EQ(
      (int64_t)1,
      downcast<Integer>(downcast<Dict>(resParams->get(4))->get("faultCode"))
          ->i());
  REQUIRE(downcast<List>(resParams->get(5)));
  REQUIRE(downcast<List>(resParams->get(6)));
}

TEST_CASE_FIXTURE(RpcMethodTest, "RpcMethodTest.testSystemMulticall_fail")
{
  SystemMulticallRpcMethod m;
  auto res = m.execute(createReq("system.multicall"), e_.get());
  REQUIRE_EQ(1, res.code);
}

TEST_CASE_FIXTURE(RpcMethodTest, "RpcMethodTest.testSystemListMethods")
{
  SystemListMethodsRpcMethod m;
  auto res = m.execute(createReq("system.listMethods"), e_.get());
  REQUIRE_EQ(0, res.code);

  const auto resParams = downcast<List>(res.param);
  auto& allNames = allMethodNames();

  REQUIRE_EQ(allNames.size(), resParams->size());

  for (size_t i = 0; i < allNames.size(); ++i) {
    const auto s = downcast<String>(resParams->get(i));
    REQUIRE(s);
    REQUIRE_EQ(allNames[i], s->s());
  }
}

TEST_CASE_FIXTURE(RpcMethodTest, "RpcMethodTest.testSystemListNotifications")
{
  SystemListNotificationsRpcMethod m;
  auto res = m.execute(createReq("system.listNotifications"), e_.get());
  REQUIRE_EQ(0, res.code);

  const auto resParams = downcast<List>(res.param);
  auto& allNames = allNotificationsNames();

  REQUIRE_EQ(allNames.size(), resParams->size());

  for (size_t i = 0; i < allNames.size(); ++i) {
    const auto s = downcast<String>(resParams->get(i));
    REQUIRE(s);
    REQUIRE_EQ(allNames[i], s->s());
  }
}

} // namespace aria2::rpc
