/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
#include "ValueBase.h"
#include "error_code.h"
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

TEST_CASE_FIXTURE(RpcMethodTest, "RpcMethodTest.testGetOption")
{
  auto group = std::make_shared<RequestGroup>(GroupId::create(), option_);
  group->getOption()->put(PREF_DIR, "alpha");
#ifdef ENABLE_BITTORRENT
  group->getOption()->put(PREF_BT_ENCRYPTION, V_REQUIRED);
#endif
  e_->getRequestGroupMan()->addReservedGroup(group);
  auto dr = createDownloadResult(error_code::FINISHED, "http://host/fin");
  dr->option->put(PREF_DIR, "bravo");
  e_->getRequestGroupMan()->addDownloadResult(dr);

  GetOptionRpcMethod m;
  auto req = createReq(GetOptionRpcMethod::getMethodName());
  req.params->append(GroupId::toHex(group->getGID()));
  auto res = m.execute(std::move(req), e_.get());
  REQUIRE_EQ(0, res.code);
  const Dict* resopt = downcast<Dict>(res.param);
  REQUIRE_EQ(std::string("alpha"),
             downcast<String>(resopt->get(PREF_DIR->k))->s());
  req = createReq(GetOptionRpcMethod::getMethodName());
  req.params->append(dr->gid->toHex());
  res = m.execute(std::move(req), e_.get());
  REQUIRE_EQ(0, res.code);
  resopt = downcast<Dict>(res.param);
  REQUIRE_EQ(std::string("bravo"),
             downcast<String>(resopt->get(PREF_DIR->k))->s());
  // Invalid GID
  req = createReq(GetOptionRpcMethod::getMethodName());
  req.params->append(GroupId::create()->toHex());
  res = m.execute(std::move(req), e_.get());
  REQUIRE_EQ(1, res.code);
}

TEST_CASE_FIXTURE(RpcMethodTest, "RpcMethodTest.testChangeOption")
{
  auto group = std::make_shared<RequestGroup>(GroupId::create(), option_);
  e_->getRequestGroupMan()->addReservedGroup(group);

  ChangeOptionRpcMethod m;
  auto req = createReq(ChangeOptionRpcMethod::getMethodName());
  req.params->append(GroupId::toHex(group->getGID()));
  auto opt = Dict::g();
  opt->put(PREF_MAX_DOWNLOAD_LIMIT->k, "100K");
#ifdef ENABLE_BITTORRENT
  opt->put(PREF_BT_MAX_PEERS->k, "100");
  opt->put(PREF_MAX_UPLOAD_LIMIT->k, "50K");
#endif // ENABLE_BITTORRENT
  req.params->append(std::move(opt));
  auto res = m.execute(std::move(req), e_.get());

  auto option = group->getOption();

  REQUIRE_EQ(0, res.code);
  REQUIRE_EQ((int)100_k, group->getMaxDownloadSpeedLimit());
  REQUIRE_EQ(std::string("102400"), option->get(PREF_MAX_DOWNLOAD_LIMIT));
#ifdef ENABLE_BITTORRENT
  REQUIRE_EQ(std::string("100"), option->get(PREF_BT_MAX_PEERS));
  REQUIRE_EQ((int)50_k, group->getMaxUploadSpeedLimit());
  REQUIRE_EQ(std::string("51200"), option->get(PREF_MAX_UPLOAD_LIMIT));
#endif // ENABLE_BITTORRENT
}

TEST_CASE_FIXTURE(RpcMethodTest, "RpcMethodTest.testChangeOption_withBadOption")
{
  auto group = std::make_shared<RequestGroup>(GroupId::create(), option_);
  e_->getRequestGroupMan()->addReservedGroup(group);

  ChangeOptionRpcMethod m;
  auto req = createReq(ChangeOptionRpcMethod::getMethodName());
  req.params->append(GroupId::toHex(group->getGID()));
  auto opt = Dict::g();
  opt->put(PREF_MAX_DOWNLOAD_LIMIT->k, "badvalue");
  req.params->append(std::move(opt));
  auto res = m.execute(std::move(req), e_.get());
  REQUIRE_EQ(1, res.code);
}

TEST_CASE_FIXTURE(RpcMethodTest,
                  "RpcMethodTest.testChangeOption_withNotAllowedOption")
{
  auto group = std::make_shared<RequestGroup>(GroupId::create(), option_);
  e_->getRequestGroupMan()->addReservedGroup(group);

  ChangeOptionRpcMethod m;
  auto req = createReq(ChangeOptionRpcMethod::getMethodName());
  req.params->append(GroupId::toHex(group->getGID()));
  auto opt = Dict::g();
  opt->put(PREF_MAX_OVERALL_DOWNLOAD_LIMIT->k, "100K");
  req.params->append(std::move(opt));
  auto res = m.execute(std::move(req), e_.get());
  // The unacceptable options are just ignored.
  REQUIRE_EQ(0, res.code);
}

TEST_CASE_FIXTURE(RpcMethodTest, "RpcMethodTest.testChangeOption_withoutGid")
{
  ChangeOptionRpcMethod m;
  auto res =
      m.execute(createReq(ChangeOptionRpcMethod::getMethodName()), e_.get());
  REQUIRE_EQ(1, res.code);
}

TEST_CASE_FIXTURE(RpcMethodTest, "RpcMethodTest.testChangeGlobalOption")
{
  ChangeGlobalOptionRpcMethod m;
  auto req = createReq(ChangeGlobalOptionRpcMethod::getMethodName());
  auto opt = Dict::g();
  opt->put(PREF_MAX_OVERALL_DOWNLOAD_LIMIT->k, "100K");
#ifdef ENABLE_BITTORRENT
  opt->put(PREF_MAX_OVERALL_UPLOAD_LIMIT->k, "50K");
  opt->put(PREF_BT_ENCRYPTION->k, V_PREFERRED);
#endif // ENABLE_BITTORRENT
  req.params->append(std::move(opt));
  auto res = m.execute(std::move(req), e_.get());

  REQUIRE_EQ(0, res.code);
  REQUIRE_EQ((int)100_k,
             e_->getRequestGroupMan()->getMaxOverallDownloadSpeedLimit());
  REQUIRE_EQ(std::string("102400"),
             e_->getOption()->get(PREF_MAX_OVERALL_DOWNLOAD_LIMIT));
#ifdef ENABLE_BITTORRENT
  REQUIRE_EQ((int)50_k,
             e_->getRequestGroupMan()->getMaxOverallUploadSpeedLimit());
  REQUIRE_EQ(std::string("51200"),
             e_->getOption()->get(PREF_MAX_OVERALL_UPLOAD_LIMIT));
  REQUIRE_EQ(V_PREFERRED, e_->getOption()->get(PREF_BT_ENCRYPTION));
#endif // ENABLE_BITTORRENT
}

TEST_CASE_FIXTURE(RpcMethodTest,
                  "RpcMethodTest.testChangeGlobalOption_withLegacyOptions")
{
  ChangeGlobalOptionRpcMethod method;
  auto request = createReq(ChangeGlobalOptionRpcMethod::getMethodName());
  auto options = Dict::g();
  options->put("split", "8");
  options->put("max-connection-per-server", "3");
  options->put("ftp-user", "anonymous");
#ifdef ENABLE_BITTORRENT
  options->put("listen-port", "6881-6999");
#endif
  options->put("metalink-preferred-protocol", "ftp");
  options->put(PREF_MAX_CONCURRENT_DOWNLOADS->k, "7");
  request.params->append(std::move(options));

  const auto response = method.execute(std::move(request), e_.get());
  REQUIRE_EQ(0, response.code);
  CHECK_EQ("3", e_->getOption()->get(PREF_STREAM_MAX_CONNECTIONS));
#ifdef ENABLE_BITTORRENT
  CHECK_EQ("6881", e_->getOption()->get(PREF_LISTEN_PORT));
#endif
  CHECK_EQ("7", e_->getOption()->get(PREF_MAX_CONCURRENT_DOWNLOADS));
}

TEST_CASE_FIXTURE(RpcMethodTest,
                  "RpcMethodTest.testChangeGlobalOption_withUnknownOption")
{
  e_->getOption()->put(PREF_MAX_CONCURRENT_DOWNLOADS, "7");
  ChangeGlobalOptionRpcMethod method;
  auto request = createReq(ChangeGlobalOptionRpcMethod::getMethodName());
  auto options = Dict::g();
  options->put("definitely-not-an-aria2-option", "1");
  options->put(PREF_MAX_CONCURRENT_DOWNLOADS->k, "9");
  request.params->append(std::move(options));

  const auto response = method.execute(std::move(request), e_.get());
  CHECK_EQ(1, response.code);
  CHECK_EQ("7", e_->getOption()->get(PREF_MAX_CONCURRENT_DOWNLOADS));
}

TEST_CASE_FIXTURE(RpcMethodTest,
                  "RpcMethodTest.testChangeGlobalOption_withBadOption")
{
  ChangeGlobalOptionRpcMethod m;
  auto req = createReq(ChangeGlobalOptionRpcMethod::getMethodName());
  auto opt = Dict::g();
  opt->put(PREF_MAX_OVERALL_DOWNLOAD_LIMIT->k, "badvalue");
  req.params->append(std::move(opt));
  auto res = m.execute(std::move(req), e_.get());
  REQUIRE_EQ(1, res.code);
}

TEST_CASE_FIXTURE(RpcMethodTest,
                  "RpcMethodTest.testChangeGlobalOption_withNotAllowedOption")
{
  ChangeGlobalOptionRpcMethod m;
  auto req = createReq(ChangeGlobalOptionRpcMethod::getMethodName());
  auto opt = Dict::g();
  opt->put(PREF_ENABLE_RPC->k, "100K");
  req.params->append(std::move(opt));
  auto res = m.execute(std::move(req), e_.get());
  // The unacceptable options are just ignored.
  REQUIRE_EQ(0, res.code);
}

} // namespace aria2::rpc
