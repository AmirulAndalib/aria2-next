/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
#include "ContextAttribute.h"
#include "GroupId.h"
#include "ValueBase.h"
#include "error_code.h"
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <string>
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

namespace {
void addUri(const std::string& uri, const std::shared_ptr<DownloadEngine>& e)
{
  AddUriRpcMethod m;
  auto req = createReq(AddUriRpcMethod::getMethodName());
  auto urisParam = List::g();
  urisParam->append(uri);
  req.params->append(std::move(urisParam));
  REQUIRE_EQ(0, m.execute(std::move(req), e.get()).code);
}
#ifdef ENABLE_BITTORRENT
void addTorrent(const std::string& torrentFile,
                const std::shared_ptr<DownloadEngine>& e)
{
  AddTorrentRpcMethod m;
  auto req = createReq(AddTorrentRpcMethod::getMethodName());
  req.params->append(readFile(torrentFile));
  auto res = m.execute(std::move(req), e.get());
}
#endif
} // namespace

TEST_CASE_FIXTURE(RpcMethodTest, "RpcMethodTest.testTellStatus_withoutGid")
{
  TellStatusRpcMethod m;
  auto res =
      m.execute(createReq(TellStatusRpcMethod::getMethodName()), e_.get());
  REQUIRE_EQ(1, res.code);
}

TEST_CASE_FIXTURE(RpcMethodTest, "RpcMethodTest.testTellWaiting")
{
  addUri("http://1/", e_);
  addUri("http://2/", e_);
  addUri("http://3/", e_);
#ifdef ENABLE_BITTORRENT
  addTorrent(A2_TEST_DIR "/single.torrent", e_);
#else  // !ENABLE_BITTORRENT
  addUri("http://4/", e_);
#endif // !ENABLE_BITTORRENT
  auto& rgman = e_->getRequestGroupMan();
  TellWaitingRpcMethod m;
  auto req = createReq(TellWaitingRpcMethod::getMethodName());
  req.params->append(Integer::g(1));
  req.params->append(Integer::g(2));
  auto res = m.execute(std::move(req), e_.get());
  REQUIRE_EQ(0, res.code);
  const List* resParams = downcast<List>(res.param);
  REQUIRE_EQ((size_t)2, resParams->size());
  REQUIRE_EQ(GroupId::toHex(getReservedGroup(rgman.get(), 1)->getGID()),
             getString(downcast<Dict>(resParams->get(0)), "gid"));
  REQUIRE_EQ(GroupId::toHex(getReservedGroup(rgman.get(), 2)->getGID()),
             getString(downcast<Dict>(resParams->get(1)), "gid"));
  // waiting.size() == offset+num
  req = createReq(TellWaitingRpcMethod::getMethodName());
  req.params->append(Integer::g(1));
  req.params->append(Integer::g(3));
  res = m.execute(std::move(req), e_.get());
  REQUIRE_EQ(0, res.code);
  resParams = downcast<List>(res.param);
  REQUIRE_EQ((size_t)3, resParams->size());
  // waiting.size() < offset+num
  req = createReq(TellWaitingRpcMethod::getMethodName());
  req.params->append(Integer::g(1));
  req.params->append(Integer::g(4));
  res = m.execute(std::move(req), e_.get());
  REQUIRE_EQ(0, res.code);
  resParams = downcast<List>(res.param);
  REQUIRE_EQ((size_t)3, resParams->size());

  // offset = INT32_MAX
  req = createReq(TellWaitingRpcMethod::getMethodName());
  req.params->append(Integer::g(INT32_MAX));
  req.params->append(Integer::g(1));
  res = m.execute(std::move(req), e_.get());
  REQUIRE_EQ(0, res.code);
  resParams = downcast<List>(res.param);
  REQUIRE_EQ((size_t)0, resParams->size());
  // num = INT32_MAX
  req = createReq(TellWaitingRpcMethod::getMethodName());
  req.params->append(Integer::g(1));
  req.params->append(Integer::g(INT32_MAX));
  res = m.execute(std::move(req), e_.get());
  REQUIRE_EQ(0, res.code);
  resParams = downcast<List>(res.param);
  REQUIRE_EQ((size_t)3, resParams->size());
  // offset=INT32_MAX and num = INT32_MAX
  req = createReq(TellWaitingRpcMethod::getMethodName());
  req.params->append(Integer::g(INT32_MAX));
  req.params->append(Integer::g(INT32_MAX));
  res = m.execute(std::move(req), e_.get());
  REQUIRE_EQ(0, res.code);
  resParams = downcast<List>(res.param);
  REQUIRE_EQ((size_t)0, resParams->size());
  // offset=INT32_MIN and num = INT32_MAX
  req = createReq(TellWaitingRpcMethod::getMethodName());
  req.params->append(Integer::g(INT32_MIN));
  req.params->append(Integer::g(INT32_MAX));
  res = m.execute(std::move(req), e_.get());
  REQUIRE_EQ(0, res.code);
  resParams = downcast<List>(res.param);
  REQUIRE_EQ((size_t)0, resParams->size());

  // negative offset
  req = createReq(TellWaitingRpcMethod::getMethodName());
  req.params->append(Integer::g(-1));
  req.params->append(Integer::g(2));
  res = m.execute(std::move(req), e_.get());
  REQUIRE_EQ(0, res.code);
  resParams = downcast<List>(res.param);
  REQUIRE_EQ((size_t)2, resParams->size());
  REQUIRE_EQ(GroupId::toHex(getReservedGroup(rgman.get(), 3)->getGID()),
             getString(downcast<Dict>(resParams->get(0)), "gid"));
  REQUIRE_EQ(GroupId::toHex(getReservedGroup(rgman.get(), 2)->getGID()),
             getString(downcast<Dict>(resParams->get(1)), "gid"));
  // negative offset and size < num
  req = RpcRequest(TellWaitingRpcMethod::getMethodName(), List::g());
  req.params->append(Integer::g(-1));
  req.params->append(Integer::g(100));
  res = m.execute(std::move(req), e_.get());
  REQUIRE_EQ(0, res.code);
  resParams = downcast<List>(res.param);
  REQUIRE_EQ((size_t)4, resParams->size());
  // negative offset and normalized offset < 0
  req = RpcRequest(TellWaitingRpcMethod::getMethodName(), List::g());
  req.params->append(Integer::g(-5));
  req.params->append(Integer::g(100));
  res = m.execute(std::move(req), e_.get());
  REQUIRE_EQ(0, res.code);
  resParams = downcast<List>(res.param);
  REQUIRE_EQ((size_t)0, resParams->size());
  // negative offset and normalized offset == 0
  req = RpcRequest(TellWaitingRpcMethod::getMethodName(), List::g());
  req.params->append(Integer::g(-4));
  req.params->append(Integer::g(100));
  res = m.execute(std::move(req), e_.get());
  REQUIRE_EQ(0, res.code);
  resParams = downcast<List>(res.param);
  REQUIRE_EQ((size_t)1, resParams->size());
}

TEST_CASE_FIXTURE(RpcMethodTest, "RpcMethodTest.testTellWaiting_fail")
{
  TellWaitingRpcMethod m;
  auto res =
      m.execute(createReq(TellWaitingRpcMethod::getMethodName()), e_.get());
  REQUIRE_EQ(1, res.code);
}

TEST_CASE_FIXTURE(RpcMethodTest, "RpcMethodTest.testGatherStoppedDownload")
{
  std::vector<std::shared_ptr<FileEntry>> fileEntries;
  std::vector<a2_gid_t> followedBy;
  followedBy.push_back(3);
  followedBy.push_back(4);
  auto d = std::make_shared<DownloadResult>();
  d->gid = GroupId::create();
  d->fileEntries = fileEntries;
  d->inMemoryDownload = false;
  d->sessionDownloadLength = UINT64_MAX;
  d->sessionTime = 1_s;
  d->result = error_code::FINISHED;
  d->followedBy = followedBy;
  d->following = 1;
  d->belongsTo = 2;
  auto entry = Dict::g();
  std::vector<std::string> keys;
  gatherStoppedDownload(entry.get(), d, keys);

  const List* followedByRes = downcast<List>(entry->get("followedBy"));
  REQUIRE_EQ(GroupId::toHex(3), downcast<String>(followedByRes->get(0))->s());
  REQUIRE_EQ(GroupId::toHex(4), downcast<String>(followedByRes->get(1))->s());
  REQUIRE_EQ(GroupId::toHex(1), downcast<String>(entry->get("following"))->s());
  REQUIRE_EQ(GroupId::toHex(2), downcast<String>(entry->get("belongsTo"))->s());

  keys.push_back("gid");

  entry = Dict::g();
  gatherStoppedDownload(entry.get(), d, keys);
  REQUIRE_EQ((size_t)1, entry->size());
  REQUIRE(entry->containsKey("gid"));
}

#ifdef ENABLE_BITTORRENT

TEST_CASE_FIXTURE(RpcMethodTest, "RpcMethodTest.testGatherStoppedDownload_bt")
{
  auto d = std::make_shared<DownloadResult>();
  d->gid = GroupId::create();
  d->infoHash = "2089b05ecca3d829cee5497d2703803b52216d19";
  d->attrs = std::vector<std::shared_ptr<ContextAttribute>>(MAX_CTX_ATTR);

  auto torrentAttr = std::make_shared<BtMetadata>();
  torrentAttr->creationDate = 1000000007;
  d->attrs[CTX_ATTR_BT] = torrentAttr;

  auto entry = Dict::g();
  gatherStoppedDownload(entry.get(), d, {});

  auto btDict = downcast<Dict>(entry->get("bittorrent"));
  REQUIRE(btDict);

  REQUIRE_EQ((int64_t)1000000007,
             downcast<Integer>(btDict->get("creationDate"))->i());
}

#endif

TEST_CASE_FIXTURE(RpcMethodTest, "RpcMethodTest.testGatherProgressCommon")
{
  auto dctx = std::make_shared<DownloadContext>(0, 0, "aria2.tar.bz2");
  std::string uris[] = {"http://localhost/aria2.tar.bz2"};
  dctx->getFirstFileEntry()->addUris(std::begin(uris), std::end(uris));
  auto group = std::make_shared<RequestGroup>(
      GroupId::create(), std::make_shared<Option>(*option_));
  group->setDownloadContext(dctx);
  std::vector<std::shared_ptr<RequestGroup>> followedBy;
  for (int i = 0; i < 2; ++i) {
    followedBy.push_back(std::make_shared<RequestGroup>(
        GroupId::create(), std::make_shared<Option>(*option_)));
  }

  group->followedBy(followedBy.begin(), followedBy.end());
  auto leader = GroupId::create();
  group->following(leader->getNumericId());
  auto parent = GroupId::create();
  group->belongsTo(parent->getNumericId());

  auto entry = Dict::g();
  std::vector<std::string> keys;
  gatherProgressCommon(entry.get(), group, keys);

  const List* followedByRes = downcast<List>(entry->get("followedBy"));
  REQUIRE_EQ(GroupId::toHex(followedBy[0]->getGID()),
             downcast<String>(followedByRes->get(0))->s());
  REQUIRE_EQ(GroupId::toHex(followedBy[1]->getGID()),
             downcast<String>(followedByRes->get(1))->s());
  REQUIRE_EQ(leader->toHex(), downcast<String>(entry->get("following"))->s());
  REQUIRE_EQ(parent->toHex(), downcast<String>(entry->get("belongsTo"))->s());
  const List* files = downcast<List>(entry->get("files"));
  REQUIRE_EQ((size_t)1, files->size());
  const Dict* file = downcast<Dict>(files->get(0));
  REQUIRE_EQ(std::string("aria2.tar.bz2"),
             downcast<String>(file->get("path"))->s());
  REQUIRE_EQ(
      uris[0],
      downcast<String>(
          downcast<Dict>(downcast<List>(file->get("uris"))->get(0))->get("uri"))
          ->s());
  REQUIRE_EQ(e_->getOption()->get(PREF_DIR),
             downcast<String>(entry->get("dir"))->s());

  keys = {"seeder"};
  entry = Dict::g();
  gatherProgressCommon(entry.get(), group, keys);

  REQUIRE_EQ((size_t)1, entry->size());
  REQUIRE(entry->containsKey("seeder"));
  REQUIRE_EQ(std::string("false"), getString(entry.get(), "seeder"));

  keys = {"gid"};
  entry = Dict::g();
  gatherProgressCommon(entry.get(), group, keys);

  REQUIRE_EQ((size_t)1, entry->size());
  REQUIRE(entry->containsKey("gid"));
}

TEST_CASE_FIXTURE(RpcMethodTest,
                  "RpcMethodTest.testFileProgressIncludesInFlightBlocks")
{
  constexpr int32_t pieceLength = 64_k;
  constexpr int64_t totalLength = 128_k;
  auto dctx = std::make_shared<DownloadContext>(
      pieceLength, totalLength, A2_TEST_OUT_DIR "/file-progress");
  std::vector<std::shared_ptr<FileEntry>> entries = {
      std::make_shared<FileEntry>("first.bin", 20_k, 0),
      std::make_shared<FileEntry>("second.bin", 44_k, 20_k),
      std::make_shared<FileEntry>("third.bin", 64_k, 64_k)};
  dctx->setFileEntries(entries.begin(), entries.end());

  auto group = std::make_shared<RequestGroup>(
      GroupId::create(), std::make_shared<Option>(*option_));
  group->setDownloadContext(dctx);
  auto storage =
      std::make_shared<DefaultPieceStorage>(dctx, group->getOption().get());
  group->setPieceStorage(storage);

  const unsigned char completedPieces[] = {0x40};
  storage->setBitfield(completedPieces, sizeof(completedPieces));
  auto inFlight = std::make_shared<Piece>(0, pieceLength);
  inFlight->completeBlock(0);
  inFlight->completeBlock(2);
  storage->addInFlightPiece({inFlight});

  REQUIRE_EQ((int64_t)96_k, storage->getCompletedLength());
  REQUIRE_EQ((int64_t)16_k, storage->getCompletedLength(0, 20_k));
  REQUIRE_EQ((int64_t)16_k, storage->getCompletedLength(20_k, 44_k));
  REQUIRE_EQ((int64_t)64_k, storage->getCompletedLength(64_k, 64_k));

  const auto progress = group->getFileCompletedLengths();
  REQUIRE_EQ(std::vector<int64_t>{16_k, 16_k, 64_k}, progress);

  auto result = group->createDownloadResult();
  REQUIRE_EQ(progress, result->fileCompletedLengths);

  auto stopped = Dict::g();
  gatherStoppedDownload(stopped.get(), result, {"completedLength", "files"});
  const auto stoppedFiles = downcast<List>(stopped->get("files"));
  REQUIRE_EQ(
      std::string("16384"),
      getString(downcast<Dict>(stoppedFiles->get(0)), "completedLength"));

  auto rpc = Dict::g();
  gatherProgressCommon(rpc.get(), group, {"completedLength", "files"});
  REQUIRE_EQ(std::string("98304"), getString(rpc.get(), "completedLength"));
  const auto files = downcast<List>(rpc->get("files"));
  REQUIRE_EQ(std::string("16384"),
             getString(downcast<Dict>(files->get(0)), "completedLength"));
  REQUIRE_EQ(std::string("16384"),
             getString(downcast<Dict>(files->get(1)), "completedLength"));
  REQUIRE_EQ(std::string("65536"),
             getString(downcast<Dict>(files->get(2)), "completedLength"));
}

} // namespace aria2::rpc
