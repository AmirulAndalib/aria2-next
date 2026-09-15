/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
#include "GroupId.h"
#include "ValueBase.h"
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
#ifdef ENABLE_METALINK
RpcRequest createAddMetalinkReq()
{
  auto req = createReq(AddMetalinkRpcMethod::getMethodName());
  req.params->append(readFile(A2_TEST_DIR "/2files.metalink"));
  return req;
}
#endif
} // namespace

#ifdef ENABLE_METALINK

TEST_CASE_FIXTURE(RpcMethodTest, "RpcMethodTest.testAddMetalink")
{
  const auto metadata = readFile(A2_TEST_DIR "/2files.metalink");
  auto digest = MessageDigest::sha1();
  digest->update(metadata.data(), metadata.size());
  const auto metadataName = util::toHex(digest->digest()) + ".meta4";
  const auto metadataPath = e_->getOption()->get(PREF_DIR) + "/" + metadataName;
  File(metadataPath).remove();
  AddMetalinkRpcMethod m;
  {
    // Saving upload metadata is disabled by option.
    auto res = m.execute(createAddMetalinkReq(), e_.get());
    REQUIRE_EQ(0, res.code);
    const List* resParams = downcast<List>(res.param);
    REQUIRE_EQ((size_t)2, resParams->size());
    a2_gid_t gid1, gid2;
    REQUIRE_EQ(0, GroupId::toNumericId(
                      gid1, downcast<String>(resParams->get(0))->s().c_str()));
    REQUIRE_EQ(0, GroupId::toNumericId(
                      gid2, downcast<String>(resParams->get(1))->s().c_str()));
    REQUIRE(!File(metadataPath).exists());
  }
  e_->getOption()->put(PREF_RPC_SAVE_UPLOAD_METADATA, A2_V_TRUE);
  {
    auto res = m.execute(createAddMetalinkReq(), e_.get());
    REQUIRE_EQ(0, res.code);
    const List* resParams = downcast<List>(res.param);
    REQUIRE_EQ((size_t)2, resParams->size());
    a2_gid_t gid3, gid4;
    REQUIRE_EQ(0, GroupId::toNumericId(
                      gid3, downcast<String>(resParams->get(0))->s().c_str()));
    REQUIRE_EQ(0, GroupId::toNumericId(
                      gid4, downcast<String>(resParams->get(1))->s().c_str()));
    REQUIRE(File(metadataPath).exists());
    REQUIRE_EQ(metadata, readFile(metadataPath));

    auto tar = findReservedGroup(e_->getRequestGroupMan().get(), gid3);
    REQUIRE(tar);
    REQUIRE_EQ(e_->getOption()->get(PREF_DIR) + "/aria2-5.0.0.tar.bz2",
               tar->getFirstFilePath());
    auto deb = findReservedGroup(e_->getRequestGroupMan().get(), gid4);
    REQUIRE(deb);
    REQUIRE_EQ(e_->getOption()->get(PREF_DIR) + "/aria2-5.0.0.deb",
               deb->getFirstFilePath());
  }
  {
    auto req = createAddMetalinkReq();
    // with options
    std::string dir = A2_TEST_OUT_DIR "/aria2_RpcMethodTest_testAddMetalink";
    File(dir).mkdirs();
    auto opt = Dict::g();
    opt->put(PREF_DIR->k, dir);
    File(dir + "/" + metadataName).remove();
    req.params->append(std::move(opt));

    auto res = m.execute(std::move(req), e_.get());
    REQUIRE_EQ(0, res.code);
    const List* resParams = downcast<List>(res.param);
    REQUIRE_EQ((size_t)2, resParams->size());
    a2_gid_t gid5;
    REQUIRE_EQ(0, GroupId::toNumericId(
                      gid5, downcast<String>(resParams->get(0))->s().c_str()));
    REQUIRE_EQ(dir + "/aria2-5.0.0.tar.bz2",
               findReservedGroup(e_->getRequestGroupMan().get(), gid5)
                   ->getFirstFilePath());
    REQUIRE(File(dir + "/" + metadataName).exists());
  }
}

TEST_CASE_FIXTURE(RpcMethodTest,
                  "RpcMethodTest.testAddMetalink_withoutMetalink")
{
  AddMetalinkRpcMethod m;
  auto res =
      m.execute(createReq(AddMetalinkRpcMethod::getMethodName()), e_.get());
  REQUIRE_EQ(1, res.code);
}

TEST_CASE_FIXTURE(RpcMethodTest,
                  "RpcMethodTest.testAddMetalink_notBase64Metalink")
{
  AddMetalinkRpcMethod m;
  auto req = createReq(AddMetalinkRpcMethod::getMethodName());
  req.params->append("not metalink");
  auto res = m.execute(std::move(req), e_.get());
  REQUIRE_EQ(1, res.code);
}

TEST_CASE_FIXTURE(RpcMethodTest, "RpcMethodTest.testAddMetalink_withPosition")
{
  AddUriRpcMethod m1;
  auto req1 = createReq(AddUriRpcMethod::getMethodName());
  auto urisParam1 = List::g();
  urisParam1->append("http://uri");
  req1.params->append(std::move(urisParam1));
  auto res1 = m1.execute(std::move(req1), e_.get());
  REQUIRE_EQ(0, res1.code);

  AddMetalinkRpcMethod m2;
  auto req2 = createReq(AddMetalinkRpcMethod::getMethodName());
  req2.params->append(readFile(A2_TEST_DIR "/2files.metalink"));
  req2.params->append(Dict::g());
  req2.params->append(Integer::g(0));
  auto res2 = m2.execute(std::move(req2), e_.get());
  REQUIRE_EQ(0, res2.code);

  REQUIRE_EQ(
      e_->getOption()->get(PREF_DIR) + "/aria2-5.0.0.tar.bz2",
      getReservedGroup(e_->getRequestGroupMan().get(), 0)->getFirstFilePath());
}

#endif

} // namespace aria2::rpc
