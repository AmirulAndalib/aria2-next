#include "RpcMethod.h"

#include "a2doctest.h"

#include "DownloadEngine.h"
#include "SelectEventPoll.h"
#include "Option.h"
#include "RequestGroupMan.h"
#include "RequestGroup.h"
#include "RpcMethodImpl.h"
#include "OptionParser.h"
#include "OptionHandler.h"
#include "RpcRequest.h"
#include "RpcResponse.h"
#include "prefs.h"
#include "TestUtil.h"
#include "DownloadContext.h"
#include "FeatureConfig.h"
#include "util.h"
#include "array_fun.h"
#include "base64.h"
#include "download_helper.h"
#include "FileEntry.h"
#include "DefaultPieceStorage.h"
#include "RpcMethodFactory.h"
#include "Ed2kAttribute.h"
#include "ed2k_hash.h"
#include "ed2k_link.h"
#include "ed2k_search.h"
#ifdef ENABLE_BITTORRENT
#  include "BtDownload.h"
#  include "BtSession.h"
#  include "BtMetadata.h"
#endif // ENABLE_BITTORRENT

namespace aria2 {

namespace rpc {

class RpcMethodTest {

private:
  std::shared_ptr<DownloadEngine> e_;
  std::shared_ptr<Option> option_;

public:
  void setUp()
  {
    option_ = std::make_shared<Option>();
    option_->put(PREF_DIR, A2_TEST_OUT_DIR "/aria2_RpcMethodTest");
    option_->put(PREF_PIECE_LENGTH, "1048576");
    option_->put(PREF_MAX_DOWNLOAD_RESULT, "10");
    option_->put(PREF_BT_MAX_OPEN_FILES, "100");
    option_->put(PREF_BT_IO_THREADS, "10");
    option_->put(PREF_BT_HASHING_THREADS, "1");
    File(option_->get(PREF_DIR)).mkdirs();
    e_ = make_unique<DownloadEngine>(make_unique<SelectEventPoll>());
    e_->setOption(option_.get());
    e_->setRequestGroupMan(make_unique<RequestGroupMan>(
        std::vector<std::shared_ptr<RequestGroup>>{}, 1, option_.get()));
  }

  void testAuthorize();
  void testAddUri();
  void testAddUri_thunder();
  void testAddUri_badThunder();
  void testAddUri_acceptsJsonBoolOption();
  void testAddUri_withoutUri();
  void testAddUri_notUri();
  void testAddUri_withBadOption();
  void testAddUri_withPosition();
  void testAddUri_withBadPosition();
#ifdef ENABLE_BITTORRENT
  void testAddTorrent();
  void testAddTorrent_withoutTorrent();
  void testAddTorrent_notBase64Torrent();
  void testAddTorrent_withPosition();
  void testInspectTorrent();
  void testInspectTorrentErrors();
#endif // ENABLE_BITTORRENT
#ifdef ENABLE_METALINK
  void testAddMetalink();
  void testAddMetalink_withoutMetalink();
  void testAddMetalink_notBase64Metalink();
  void testAddMetalink_withPosition();
#endif // ENABLE_METALINK
  void testGetOption();
  void testChangeOption();
  void testChangeOption_withBadOption();
  void testChangeOption_withNotAllowedOption();
  void testChangeOption_withoutGid();
  void testChangeGlobalOption();
  void testChangeGlobalOption_withLegacyOptions();
  void testChangeGlobalOption_withUnknownOption();
  void testChangeGlobalOption_withBadOption();
  void testChangeGlobalOption_withNotAllowedOption();
  void testTellStatus_withoutGid();
  void testTellWaiting();
  void testTellWaiting_fail();
  void testGetVersion();
  void testNoSuchMethod();
  void testEd2kSearchResults();
  void testEd2kSearchResultLinkCreatesDownload();
  void testGatherStoppedDownload();
  void testGatherProgressEd2kStatus();
#ifdef ENABLE_BITTORRENT
  void testGatherStoppedDownload_bt();
  void testGetPeers();
  void testGetBtTrackers();
  void testReplaceBtTrackers();
  void testGetBtSessionStatus();
  void testSetBtPeerBlocklist();
  void testBtGlobalStat();
  void testBtFileSelectionGate();
  void testBtSharingContract();
  void testBtResumeProgressAuthority();
#endif // ENABLE_BITTORRENT
  void testGatherProgressCommon();
  void testChangePosition();
  void testChangePosition_fail();
  void testGetSessionInfo();
  void testChangeUri();
  void testChangeUri_fail();
  void testPause();
  void testSystemMulticall();
  void testSystemMulticall_fail();
  void testSystemListMethods();
  void testSystemListNotifications();
};

A2_TEST(RpcMethodTest, testAuthorize)
A2_TEST(RpcMethodTest, testAddUri)
A2_TEST(RpcMethodTest, testAddUri_thunder)
A2_TEST(RpcMethodTest, testAddUri_badThunder)
A2_TEST(RpcMethodTest, testAddUri_acceptsJsonBoolOption)
A2_TEST(RpcMethodTest, testAddUri_withoutUri)
A2_TEST(RpcMethodTest, testAddUri_notUri)
A2_TEST(RpcMethodTest, testAddUri_withBadOption)
A2_TEST(RpcMethodTest, testAddUri_withPosition)
A2_TEST(RpcMethodTest, testAddUri_withBadPosition)
#ifdef ENABLE_BITTORRENT
A2_TEST(RpcMethodTest, testAddTorrent)
A2_TEST(RpcMethodTest, testAddTorrent_withoutTorrent)
A2_TEST(RpcMethodTest, testAddTorrent_notBase64Torrent)
A2_TEST(RpcMethodTest, testAddTorrent_withPosition)
A2_TEST(RpcMethodTest, testInspectTorrent)
A2_TEST(RpcMethodTest, testInspectTorrentErrors)
#endif // ENABLE_BITTORRENT
#ifdef ENABLE_METALINK
A2_TEST(RpcMethodTest, testAddMetalink)
A2_TEST(RpcMethodTest, testAddMetalink_withoutMetalink)
A2_TEST(RpcMethodTest, testAddMetalink_notBase64Metalink)
A2_TEST(RpcMethodTest, testAddMetalink_withPosition)
#endif // ENABLE_METALINK
A2_TEST(RpcMethodTest, testGetOption)
A2_TEST(RpcMethodTest, testChangeOption)
A2_TEST(RpcMethodTest, testChangeOption_withBadOption)
A2_TEST(RpcMethodTest, testChangeOption_withNotAllowedOption)
A2_TEST(RpcMethodTest, testChangeOption_withoutGid)
A2_TEST(RpcMethodTest, testChangeGlobalOption)
A2_TEST(RpcMethodTest, testChangeGlobalOption_withLegacyOptions)
A2_TEST(RpcMethodTest, testChangeGlobalOption_withUnknownOption)
A2_TEST(RpcMethodTest, testChangeGlobalOption_withBadOption)
A2_TEST(RpcMethodTest, testChangeGlobalOption_withNotAllowedOption)
A2_TEST(RpcMethodTest, testTellStatus_withoutGid)
A2_TEST(RpcMethodTest, testTellWaiting)
A2_TEST(RpcMethodTest, testTellWaiting_fail)
A2_TEST(RpcMethodTest, testGetVersion)
A2_TEST(RpcMethodTest, testNoSuchMethod)
A2_TEST(RpcMethodTest, testEd2kSearchResults)
A2_TEST(RpcMethodTest, testEd2kSearchResultLinkCreatesDownload)
A2_TEST(RpcMethodTest, testGatherStoppedDownload)
A2_TEST(RpcMethodTest, testGatherProgressEd2kStatus)
#ifdef ENABLE_BITTORRENT
A2_TEST(RpcMethodTest, testGatherStoppedDownload_bt)
A2_TEST(RpcMethodTest, testGetPeers)
A2_TEST(RpcMethodTest, testGetBtTrackers)
A2_TEST(RpcMethodTest, testReplaceBtTrackers)
A2_TEST(RpcMethodTest, testGetBtSessionStatus)
A2_TEST(RpcMethodTest, testSetBtPeerBlocklist)
A2_TEST(RpcMethodTest, testBtGlobalStat)
A2_TEST(RpcMethodTest, testBtFileSelectionGate)
A2_TEST(RpcMethodTest, testBtSharingContract)
A2_TEST(RpcMethodTest, testBtResumeProgressAuthority)
#endif // ENABLE_BITTORRENT
A2_TEST(RpcMethodTest, testGatherProgressCommon)
A2_TEST(RpcMethodTest, testChangePosition)
A2_TEST(RpcMethodTest, testChangePosition_fail)
A2_TEST(RpcMethodTest, testGetSessionInfo)
A2_TEST(RpcMethodTest, testChangeUri)
A2_TEST(RpcMethodTest, testChangeUri_fail)
A2_TEST(RpcMethodTest, testPause)
A2_TEST(RpcMethodTest, testSystemMulticall)
A2_TEST(RpcMethodTest, testSystemMulticall_fail)
A2_TEST(RpcMethodTest, testSystemListMethods)
A2_TEST(RpcMethodTest, testSystemListNotifications)

namespace {
std::string getString(const Dict* dict, const std::string& key)
{
  return downcast<String>(dict->get(key))->s();
}
} // namespace

namespace {
RpcRequest createReq(std::string methodName)
{
  return {std::move(methodName), List::g()};
}
} // namespace

void RpcMethodTest::testAuthorize()
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

void RpcMethodTest::testAddUri()
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

void RpcMethodTest::testAddUri_thunder()
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

void RpcMethodTest::testAddUri_badThunder()
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

void RpcMethodTest::testAddUri_acceptsJsonBoolOption()
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

void RpcMethodTest::testAddUri_withoutUri()
{
  AddUriRpcMethod m;
  auto res = m.execute(createReq(AddUriRpcMethod::getMethodName()), e_.get());
  REQUIRE_EQ(1, res.code);
}

void RpcMethodTest::testAddUri_notUri()
{
  AddUriRpcMethod m;
  auto req = createReq(AddUriRpcMethod::getMethodName());
  auto urisParam = List::g();
  urisParam->append("not uri");
  req.params->append(std::move(urisParam));
  auto res = m.execute(std::move(req), e_.get());
  REQUIRE_EQ(1, res.code);
}

void RpcMethodTest::testAddUri_withBadOption()
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

void RpcMethodTest::testAddUri_withPosition()
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

void RpcMethodTest::testAddUri_withBadPosition()
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

#ifdef ENABLE_BITTORRENT
namespace {
RpcRequest createAddTorrentReq()
{
  auto req = createReq(AddTorrentRpcMethod::getMethodName());
  req.params->append(readFile(A2_TEST_DIR "/single.torrent"));
  auto uris = List::g();
  uris->append("http://localhost/aria2-0.8.2.tar.bz2");
  req.params->append(std::move(uris));
  return req;
}
} // namespace

void RpcMethodTest::testAddTorrent()
{
  File(e_->getOption()->get(PREF_DIR) +
       "/0a3893293e27ac0490424c06de4d09242215f0a6.torrent")
      .remove();
  AddTorrentRpcMethod m;
  {
    // Saving upload metadata is disabled by option.
    auto res = m.execute(createAddTorrentReq(), e_.get());
    REQUIRE(!File(e_->getOption()->get(PREF_DIR) +
                  "/0a3893293e27ac0490424c06de4d09242215f0a6.torrent")
                 .exists());
    REQUIRE_EQ(0, res.code);
    REQUIRE_EQ(sizeof(a2_gid_t) * 2, downcast<String>(res.param)->s().size());
  }
  e_->getOption()->put(PREF_RPC_SAVE_UPLOAD_METADATA, A2_V_TRUE);
  {
    auto res = m.execute(createAddTorrentReq(), e_.get());
    REQUIRE(File(e_->getOption()->get(PREF_DIR) +
                 "/0a3893293e27ac0490424c06de4d09242215f0a6.torrent")
                .exists());
    REQUIRE_EQ(0, res.code);
    a2_gid_t gid;
    REQUIRE_EQ(
        0, GroupId::toNumericId(gid, downcast<String>(res.param)->s().c_str()));

    auto group = findReservedGroup(e_->getRequestGroupMan().get(), gid);
    REQUIRE(group);
    REQUIRE_EQ(e_->getOption()->get(PREF_DIR) + "/aria2-0.8.2.tar.bz2",
               group->getFirstFilePath());
    REQUIRE_EQ((size_t)0, group->getDownloadContext()
                              ->getFirstFileEntry()
                              ->getRemainingUris()
                              .size());
  }
  {
    auto req = createAddTorrentReq();
    // with options
    std::string dir = A2_TEST_OUT_DIR "/aria2_RpcMethodTest_testAddTorrent";
    File(dir).mkdirs();
    auto opt = Dict::g();
    opt->put(PREF_DIR->k, dir);
    File(dir + "/0a3893293e27ac0490424c06de4d09242215f0a6.torrent").remove();
    req.params->append(std::move(opt));

    auto res = m.execute(std::move(req), e_.get());
    REQUIRE_EQ(0, res.code);
    a2_gid_t gid;
    REQUIRE_EQ(
        0, GroupId::toNumericId(gid, downcast<String>(res.param)->s().c_str()));
    REQUIRE_EQ(dir + "/aria2-0.8.2.tar.bz2",
               findReservedGroup(e_->getRequestGroupMan().get(), gid)
                   ->getFirstFilePath());
    REQUIRE(File(dir + "/0a3893293e27ac0490424c06de4d09242215f0a6.torrent")
                .exists());
  }
}

void RpcMethodTest::testAddTorrent_withoutTorrent()
{
  AddTorrentRpcMethod m;
  auto res =
      m.execute(createReq(AddTorrentRpcMethod::getMethodName()), e_.get());
  REQUIRE_EQ(1, res.code);
}

void RpcMethodTest::testAddTorrent_notBase64Torrent()
{
  AddTorrentRpcMethod m;
  auto req = createReq(AddTorrentRpcMethod::getMethodName());
  req.params->append("not torrent");
  auto res = m.execute(std::move(req), e_.get());
  REQUIRE_EQ(1, res.code);
}

void RpcMethodTest::testAddTorrent_withPosition()
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

void RpcMethodTest::testInspectTorrent()
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

void RpcMethodTest::testInspectTorrentErrors()
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

#endif // ENABLE_BITTORRENT

#ifdef ENABLE_METALINK
namespace {
RpcRequest createAddMetalinkReq()
{
  auto req = createReq(AddMetalinkRpcMethod::getMethodName());
  req.params->append(readFile(A2_TEST_DIR "/2files.metalink"));
  return req;
}
} // namespace

void RpcMethodTest::testAddMetalink()
{
  File(e_->getOption()->get(PREF_DIR) +
       "/c908634fbc257fd56f0114912c2772aeeb4064f4.meta4")
      .remove();
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
    REQUIRE(!File(e_->getOption()->get(PREF_DIR) +
                  "/c908634fbc257fd56f0114912c2772aeeb4064f4.meta4")
                 .exists());
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
    REQUIRE(File(e_->getOption()->get(PREF_DIR) +
                 "/c908634fbc257fd56f0114912c2772aeeb4064f4.meta4")
                .exists());

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
    File(dir + "/c908634fbc257fd56f0114912c2772aeeb4064f4.meta4").remove();
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
    REQUIRE(
        File(dir + "/c908634fbc257fd56f0114912c2772aeeb4064f4.meta4").exists());
  }
}

void RpcMethodTest::testAddMetalink_withoutMetalink()
{
  AddMetalinkRpcMethod m;
  auto res =
      m.execute(createReq(AddMetalinkRpcMethod::getMethodName()), e_.get());
  REQUIRE_EQ(1, res.code);
}

void RpcMethodTest::testAddMetalink_notBase64Metalink()
{
  AddMetalinkRpcMethod m;
  auto req = createReq(AddMetalinkRpcMethod::getMethodName());
  req.params->append("not metalink");
  auto res = m.execute(std::move(req), e_.get());
  REQUIRE_EQ(1, res.code);
}

void RpcMethodTest::testAddMetalink_withPosition()
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

#endif // ENABLE_METALINK

void RpcMethodTest::testGetOption()
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

void RpcMethodTest::testChangeOption()
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

void RpcMethodTest::testChangeOption_withBadOption()
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

void RpcMethodTest::testChangeOption_withNotAllowedOption()
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

void RpcMethodTest::testChangeOption_withoutGid()
{
  ChangeOptionRpcMethod m;
  auto res =
      m.execute(createReq(ChangeOptionRpcMethod::getMethodName()), e_.get());
  REQUIRE_EQ(1, res.code);
}

void RpcMethodTest::testChangeGlobalOption()
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

void RpcMethodTest::testChangeGlobalOption_withLegacyOptions()
{
  ChangeGlobalOptionRpcMethod method;
  auto request = createReq(ChangeGlobalOptionRpcMethod::getMethodName());
  auto options = Dict::g();
  options->put("split", "8");
  options->put("max-connection-per-server", "3");
  options->put("ftp-user", "anonymous");
  options->put("listen-port", "6881-6999");
  options->put("metalink-preferred-protocol", "ftp");
  options->put(PREF_MAX_CONCURRENT_DOWNLOADS->k, "7");
  request.params->append(std::move(options));

  const auto response = method.execute(std::move(request), e_.get());
  CHECK_EQ(0, response.code);
  CHECK_EQ("3", e_->getOption()->get(PREF_STREAM_MAX_CONNECTIONS));
  CHECK_EQ("6881", e_->getOption()->get(PREF_LISTEN_PORT));
  CHECK_EQ("7", e_->getOption()->get(PREF_MAX_CONCURRENT_DOWNLOADS));
}

void RpcMethodTest::testChangeGlobalOption_withUnknownOption()
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

void RpcMethodTest::testChangeGlobalOption_withBadOption()
{
  ChangeGlobalOptionRpcMethod m;
  auto req = createReq(ChangeGlobalOptionRpcMethod::getMethodName());
  auto opt = Dict::g();
  opt->put(PREF_MAX_OVERALL_DOWNLOAD_LIMIT->k, "badvalue");
  req.params->append(std::move(opt));
  auto res = m.execute(std::move(req), e_.get());
  REQUIRE_EQ(1, res.code);
}

void RpcMethodTest::testChangeGlobalOption_withNotAllowedOption()
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

void RpcMethodTest::testNoSuchMethod()
{
  NoSuchMethodRpcMethod m;
  auto res = m.execute(createReq("make.hamburger"), e_.get());
  REQUIRE_EQ(1, res.code);
  REQUIRE_EQ(std::string("No such method: make.hamburger"),
             getString(downcast<Dict>(res.param), "faultString"));
}

void RpcMethodTest::testEd2kSearchResults()
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

void RpcMethodTest::testEd2kSearchResultLinkCreatesDownload()
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

void RpcMethodTest::testTellStatus_withoutGid()
{
  TellStatusRpcMethod m;
  auto res =
      m.execute(createReq(TellStatusRpcMethod::getMethodName()), e_.get());
  REQUIRE_EQ(1, res.code);
}

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
} // namespace

#ifdef ENABLE_BITTORRENT
namespace {
void addTorrent(const std::string& torrentFile,
                const std::shared_ptr<DownloadEngine>& e)
{
  AddTorrentRpcMethod m;
  auto req = createReq(AddTorrentRpcMethod::getMethodName());
  req.params->append(readFile(torrentFile));
  auto res = m.execute(std::move(req), e.get());
}
} // namespace
#endif // ENABLE_BITTORRENT

void RpcMethodTest::testTellWaiting()
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

void RpcMethodTest::testTellWaiting_fail()
{
  TellWaitingRpcMethod m;
  auto res =
      m.execute(createReq(TellWaitingRpcMethod::getMethodName()), e_.get());
  REQUIRE_EQ(1, res.code);
}

void RpcMethodTest::testGetVersion()
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

void RpcMethodTest::testGatherStoppedDownload()
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

void RpcMethodTest::testGatherProgressEd2kStatus()
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
  auto group =
      std::make_shared<RequestGroup>(GroupId::create(), util::copy(option_));
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

#ifdef ENABLE_BITTORRENT
void RpcMethodTest::testGatherStoppedDownload_bt()
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
#endif // ENABLE_BITTORRENT

void RpcMethodTest::testGatherProgressCommon()
{
  auto dctx = std::make_shared<DownloadContext>(0, 0, "aria2.tar.bz2");
  std::string uris[] = {"http://localhost/aria2.tar.bz2"};
  dctx->getFirstFileEntry()->addUris(std::begin(uris), std::end(uris));
  auto group =
      std::make_shared<RequestGroup>(GroupId::create(), util::copy(option_));
  group->setDownloadContext(dctx);
  std::vector<std::shared_ptr<RequestGroup>> followedBy;
  for (int i = 0; i < 2; ++i) {
    followedBy.push_back(
        std::make_shared<RequestGroup>(GroupId::create(), util::copy(option_)));
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

#ifdef ENABLE_BITTORRENT
void RpcMethodTest::testGetPeers()
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

void RpcMethodTest::testGetBtTrackers()
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

void RpcMethodTest::testReplaceBtTrackers()
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

void RpcMethodTest::testGetBtSessionStatus()
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

void RpcMethodTest::testSetBtPeerBlocklist()
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

void RpcMethodTest::testBtGlobalStat()
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

void RpcMethodTest::testBtFileSelectionGate()
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

void RpcMethodTest::testBtSharingContract()
{
  auto taskOption = util::copy(option_);
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

void RpcMethodTest::testBtResumeProgressAuthority()
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
}
#endif // ENABLE_BITTORRENT

void RpcMethodTest::testChangePosition()
{
  e_->getRequestGroupMan()->addReservedGroup(
      std::make_shared<RequestGroup>(GroupId::create(), util::copy(option_)));
  e_->getRequestGroupMan()->addReservedGroup(
      std::make_shared<RequestGroup>(GroupId::create(), util::copy(option_)));

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

void RpcMethodTest::testChangePosition_fail()
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
} // namespace

void RpcMethodTest::testChangeUri()
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

namespace {
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

void RpcMethodTest::testChangeUri_fail()
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

void RpcMethodTest::testGetSessionInfo()
{
  GetSessionInfoRpcMethod m;
  auto res =
      m.execute(createReq(GetSessionInfoRpcMethod::getMethodName()), e_.get());
  REQUIRE_EQ(0, res.code);
  REQUIRE_EQ(util::toHex(e_->getSessionId()),
             getString(downcast<Dict>(res.param), "sessionId"));
}

void RpcMethodTest::testPause()
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

void RpcMethodTest::testSystemMulticall()
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

void RpcMethodTest::testSystemMulticall_fail()
{
  SystemMulticallRpcMethod m;
  auto res = m.execute(createReq("system.multicall"), e_.get());
  REQUIRE_EQ(1, res.code);
}

void RpcMethodTest::testSystemListMethods()
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

void RpcMethodTest::testSystemListNotifications()
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

} // namespace rpc

} // namespace aria2
