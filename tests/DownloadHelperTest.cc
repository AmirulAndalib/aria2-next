/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
#include "GroupId.h"
#include "RecoverableException.h"
#include <ios>
#include <iterator>
#include <memory>
#include "download_helper.h"

#include <string>
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <vector>

#include "a2doctest.h"

#include "RequestGroup.h"
#include "DownloadEngine.h"
#include "DownloadContext.h"
#include "DefaultPieceStorage.h"
#include "DiskAdaptor.h"
#include "DlRetryEx.h"
#include "Ed2kAttribute.h"
#include "Ed2kKadCommand.h"
#include "Ed2kPeerTransfer.h"
#include "Ed2kUploadQueue.h"
#include "Option.h"
#include "Piece.h"
#include "RequestGroupMan.h"
#include "SelectEventPoll.h"
#include "Segment.h"
#include "SegmentMan.h"
#include "array_fun.h"
#include "base32.h"
#include "base64.h"
#include "ed2k_constants.h"
#include "ed2k_aich.h"
#include "ed2k_endpoint.h"
#include "ed2k_hash.h"
#include "ed2k_kad.h"
#include "ed2k_link.h"
#include "ed2k_packet.h"
#include "ed2k_peer.h"
#include "ed2k_policy.h"
#include "ed2k_search.h"
#include "ed2k_server.h"
#include "prefs.h"
#include "Exception.h"
#include "TestUtil.h"
#include "support/Numbers.h"
#include "support/Encoding.h"
#include "support/FilePath.h"
#include "a2functional.h"
#include "FileEntry.h"
#ifdef ENABLE_BITTORRENT
#  include "BtDownload.h"
#endif // ENABLE_BITTORRENT

namespace aria2 {

TEST_CASE("DownloadHelperTest.testCreateRequestGroupForUri")
{
  auto option = std::make_shared<Option>();
  std::vector<std::string> uris{"http://alpha/file", "http://bravo/file",
                                "http://charlie/file"};
  option->put(PREF_DIR, "/tmp");
  option->put(PREF_OUT, "file.out");
  {
    std::vector<std::shared_ptr<RequestGroup>> result;
    createRequestGroupForUri(result, option, uris);
    REQUIRE_EQ((size_t)1, result.size());
    std::shared_ptr<RequestGroup> group = result[0];
    auto xuris = group->getDownloadContext()->getFirstFileEntry()->getUris();
    REQUIRE_EQ(uris, xuris);
    REQUIRE(group->getCurlDownload());
    std::shared_ptr<DownloadContext> ctx = group->getDownloadContext();
    REQUIRE_EQ(std::string("/tmp/file.out"), ctx->getBasePath());
  }
  option->put(PREF_FORCE_SEQUENTIAL, A2_V_TRUE);
  {
    std::vector<std::shared_ptr<RequestGroup>> result;
    createRequestGroupForUri(result, option, uris);
    REQUIRE_EQ((size_t)3, result.size());
    // for alpha server
    std::shared_ptr<RequestGroup> alphaGroup = result[0];
    auto alphaURIs =
        alphaGroup->getDownloadContext()->getFirstFileEntry()->getUris();
    REQUIRE_EQ((size_t)1, alphaURIs.size());
    REQUIRE_EQ(uris[0], alphaURIs[0]);
    std::shared_ptr<DownloadContext> alphaCtx =
        alphaGroup->getDownloadContext();
    // See filename is not assigned yet
    REQUIRE_EQ(std::string(""), alphaCtx->getBasePath());
  }
}

TEST_CASE("DownloadHelperTest.testCreateRequestGroupForUri_Thunder")
{
  auto option = std::make_shared<Option>();
  const std::string url = "https://example.com/download/file.bin?token=abc123";
  std::string payload = "AA" + url + "ZZ";
  std::string thunder =
      "thunder://" + base64::encode(payload.begin(), payload.end());
  thunder.erase(thunder.find_last_not_of('=') + 1);
  std::vector<std::string> uris{thunder};

  std::vector<std::shared_ptr<RequestGroup>> result;
  createRequestGroupForUri(result, option, uris, false, false, true);

  REQUIRE_EQ((size_t)1, result.size());
  const auto xuris =
      result[0]->getDownloadContext()->getFirstFileEntry()->getUris();
  REQUIRE_EQ((size_t)1, xuris.size());
  REQUIRE_EQ(url, xuris[0]);
}

TEST_CASE("DownloadHelperTest.testCreateRequestGroupForUri_BadThunder")
{
  auto option = std::make_shared<Option>();
  std::vector<std::string> uris{"thunder://bad!"};
  std::vector<std::shared_ptr<RequestGroup>> result;

  REQUIRE_THROWS_AS(
      createRequestGroupForUri(result, option, uris, false, false, true),
      RecoverableException);
}

TEST_CASE("DownloadHelperTest.testCreateRequestGroupForUri_parameterized")
{
  auto option = std::make_shared<Option>();
  std::vector<std::string> uris{"http://{alpha, bravo}/file",
                                "http://charlie/file"};
  option->put(PREF_DIR, "/tmp");
  option->put(PREF_OUT, "file.out");
  option->put(PREF_PARAMETERIZED_URI, A2_V_TRUE);
  {
    std::vector<std::shared_ptr<RequestGroup>> result;

    createRequestGroupForUri(result, option, uris);

    REQUIRE_EQ((size_t)1, result.size());
    std::shared_ptr<RequestGroup> group = result[0];
    auto uris = group->getDownloadContext()->getFirstFileEntry()->getUris();
    REQUIRE_EQ((size_t)3, uris.size());

    REQUIRE_EQ(std::string("http://alpha/file"), uris[0]);
    REQUIRE_EQ(std::string("http://bravo/file"), uris[1]);
    REQUIRE_EQ(std::string("http://charlie/file"), uris[2]);

    REQUIRE(group->getCurlDownload());
    std::shared_ptr<DownloadContext> ctx = group->getDownloadContext();
    REQUIRE_EQ(std::string("/tmp/file.out"), ctx->getBasePath());
  }
}

#ifdef ENABLE_BITTORRENT

TEST_CASE("DownloadHelperTest.testCreateRequestGroupForUri_BitTorrent")
{
  auto option = std::make_shared<Option>();
  std::vector<std::string> uris{"http://alpha/file",
                                A2_TEST_DIR "/test.torrent",
                                "http://bravo/file", "http://charlie/file"};
  option->put(PREF_DIR, "/tmp");
  option->put(PREF_OUT, "file.out");
  {
    std::vector<std::shared_ptr<RequestGroup>> result;

    createRequestGroupForUri(result, option, uris);

    REQUIRE_EQ((size_t)2, result.size());
    std::shared_ptr<RequestGroup> group = result[0];
    auto xuris = group->getDownloadContext()->getFirstFileEntry()->getUris();
    REQUIRE_EQ((size_t)3, xuris.size());

    REQUIRE_EQ(uris[0], xuris[0]);
    REQUIRE_EQ(uris[2], xuris[1]);
    REQUIRE_EQ(uris[3], xuris[2]);

    REQUIRE(group->getCurlDownload());
    std::shared_ptr<DownloadContext> ctx = group->getDownloadContext();
    REQUIRE_EQ(std::string("/tmp/file.out"), ctx->getBasePath());

    std::shared_ptr<RequestGroup> torrentGroup = result[1];
    auto auxURIs =
        torrentGroup->getDownloadContext()->getFirstFileEntry()->getUris();
    REQUIRE(auxURIs.empty());
    REQUIRE_EQ(1, torrentGroup->getNumConcurrentCommand());
    std::shared_ptr<DownloadContext> btctx = torrentGroup->getDownloadContext();
    REQUIRE_EQ(std::string("/tmp/aria2-test"), btctx->getBasePath());
  }
}

TEST_CASE("DownloadHelperTest.testCreateRequestGroupForUri_MagnetMetadataGate")
{
  auto option = std::make_shared<Option>();
  const std::string magnet =
      "magnet:?xt=urn:btih:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "&dn=metadata-gate-test";
  option->put(PREF_DIR, A2_TEST_OUT_DIR);
  option->put(PREF_ENABLE_RPC, A2_V_TRUE);
  option->put(PREF_PAUSE_METADATA, A2_V_TRUE);

  std::vector<std::shared_ptr<RequestGroup>> result;
  createRequestGroupForUri(result, option, {magnet});

  REQUIRE_EQ((size_t)1, result.size());
  const auto& group = result.front();
  REQUIRE(group->getBtDownload());
  REQUIRE(group->getBtDownload()->shouldPauseAfterMetadata());
  REQUIRE(group->followedBy().empty());
  REQUIRE_EQ((a2_gid_t)0, group->following());

  group->getOption()->put(PREF_PAUSE_METADATA, A2_V_FALSE);
  REQUIRE(!group->getBtDownload()->shouldPauseAfterMetadata());
}

#endif

#ifdef ENABLE_METALINK

TEST_CASE("DownloadHelperTest.testCreateRequestGroupForUri_Metalink")
{
  auto option = std::make_shared<Option>();
  std::vector<std::string> uris{"http://alpha/file", "http://bravo/file",
                                "http://charlie/file", A2_TEST_DIR "/test.xml"};
  option->put(PREF_DIR, "/tmp");
  option->put(PREF_OUT, "file.out");
  {
    std::vector<std::shared_ptr<RequestGroup>> result;

    createRequestGroupForUri(result, option, uris);

    // group1: http://alpha/file, ...
    // group2-7: 6 file entry in Metalink and 1 torrent file download
    REQUIRE_EQ((size_t)6, result.size());

    std::shared_ptr<RequestGroup> group = result[0];
    auto xuris = group->getDownloadContext()->getFirstFileEntry()->getUris();
    REQUIRE_EQ((size_t)3, xuris.size());
    for (size_t i = 0; i < 3; ++i) {
      REQUIRE_EQ(uris[i], xuris[i]);
    }
    REQUIRE(group->getCurlDownload());
    std::shared_ptr<DownloadContext> ctx = group->getDownloadContext();
    REQUIRE_EQ(std::string("/tmp/file.out"), ctx->getBasePath());

    std::shared_ptr<RequestGroup> aria2052Group = result[1];
    REQUIRE(aria2052Group->getCurlDownload());
    std::shared_ptr<DownloadContext> aria2052Ctx =
        aria2052Group->getDownloadContext();
    REQUIRE_EQ(std::string("/tmp/aria2-0.5.2.tar.bz2"),
               aria2052Ctx->getBasePath());

    REQUIRE(result[2]->getCurlDownload());
  }
}

#endif

TEST_CASE("DownloadHelperTest.testCreateRequestGroupForUriList")
{
  auto option = std::make_shared<Option>();
  option->put(PREF_INPUT_FILE, A2_TEST_DIR "/input_uris.txt");
  option->put(PREF_DIR, "/tmp");
  option->put(PREF_OUT, "file.out");

  std::vector<std::shared_ptr<RequestGroup>> result;

  createRequestGroupForUriList(result, option);

  REQUIRE_EQ((size_t)2, result.size());

  std::shared_ptr<RequestGroup> fileGroup = result[0];
  auto fileURIs =
      fileGroup->getDownloadContext()->getFirstFileEntry()->getUris();
  REQUIRE_EQ(std::string("http://alpha/file"), fileURIs[0]);
  REQUIRE_EQ(std::string("http://bravo/file"), fileURIs[1]);
  REQUIRE_EQ(std::string("http://charlie/file"), fileURIs[2]);
  REQUIRE(fileGroup->getCurlDownload());
  std::shared_ptr<DownloadContext> fileCtx = fileGroup->getDownloadContext();
  REQUIRE_EQ(std::string("/mydownloads/myfile.out"), fileCtx->getBasePath());

  std::shared_ptr<RequestGroup> fileISOGroup = result[1];
  std::shared_ptr<DownloadContext> fileISOCtx =
      fileISOGroup->getDownloadContext();
  // PREF_OUT in option must be ignored.
  REQUIRE_EQ(std::string(), fileISOCtx->getBasePath());
}

#ifdef ENABLE_BITTORRENT

TEST_CASE("DownloadHelperTest.testCreateRequestGroupForBitTorrent")
{
  auto option = std::make_shared<Option>();
  std::vector<std::string> auxURIs{"http://alpha/file", "http://bravo/file",
                                   "http://charlie/file"};

  option->put(PREF_TORRENT_FILE, A2_TEST_DIR "/test.torrent");
  option->put(PREF_DIR, "/tmp");
  option->put(PREF_OUT, "file.out");
  option->put(PREF_SELECT_FILE, "2");
  option->put(PREF_BT_EXCLUDE_TRACKER, "http://tracker1");
  option->put(PREF_BT_TRACKER,
              "udp://one.example:1/announce,udp://two.example:2/announce,"
              "udp://three.example:3/announce,udp://four.example:4/announce");
  {
    std::vector<std::shared_ptr<RequestGroup>> result;

    createRequestGroupForBitTorrent(result, option, auxURIs,
                                    option->get(PREF_TORRENT_FILE));

    REQUIRE_EQ((size_t)1, result.size());

    std::shared_ptr<RequestGroup> group = result[0];
    REQUIRE(group->getBtDownload());
    REQUIRE(group->getBtDownload()->hasMetadata());
    REQUIRE_EQ((size_t)3,
               group->getBtDownload()->snapshot().announceList.size());
    REQUIRE_EQ((size_t)2, group->getDownloadContext()->getFileEntries().size());
    REQUIRE(!group->getDownloadContext()->getFileEntries()[0]->isRequested());
    REQUIRE(group->getDownloadContext()->getFileEntries()[1]->isRequested());
    REQUIRE(!group->getBtDownload()->snapshot().files[0].selected);
    REQUIRE(group->getBtDownload()->snapshot().files[1].selected);
  }
  {
    std::ifstream input(A2_TEST_DIR "/test.torrent", std::ios::binary);
    std::string data((std::istreambuf_iterator<char>(input)),
                     std::istreambuf_iterator<char>());
    const auto misplacedPrivateFlag = data.find("7:privatei1e");
    REQUIRE(misplacedPrivateFlag != std::string::npos);
    data.erase(misplacedPrivateFlag, std::string("7:privatei1e").size());
    const auto infoDictionary = data.find("4:infod");
    REQUIRE(infoDictionary != std::string::npos);
    data.insert(infoDictionary + std::string("4:infod").size(), "7:privatei1e");
    auto download = BtDownload::fromBuffer(data, {});
    download->configure(option.get());
    REQUIRE_EQ((size_t)2, download->snapshot().announceList.size());
  }
  {
    // no URIs are given
    std::vector<std::shared_ptr<RequestGroup>> result;
    std::vector<std::string> emptyURIs;
    createRequestGroupForBitTorrent(result, option, emptyURIs,
                                    option->get(PREF_TORRENT_FILE));

    REQUIRE_EQ((size_t)1, result.size());
    std::shared_ptr<RequestGroup> group = result[0];
    auto uris = group->getDownloadContext()->getFirstFileEntry()->getUris();
    REQUIRE_EQ((size_t)0, uris.size());
  }
  option->put(PREF_FORCE_SEQUENTIAL, A2_V_TRUE);
  {
    std::vector<std::shared_ptr<RequestGroup>> result;

    createRequestGroupForBitTorrent(result, option, auxURIs,
                                    option->get(PREF_TORRENT_FILE));

    // See --force-requencial is ignored
    REQUIRE_EQ((size_t)1, result.size());
  }
}

#endif

#ifdef ENABLE_METALINK

TEST_CASE("DownloadHelperTest.testCreateRequestGroupForMetalink")
{
  auto option = std::make_shared<Option>();
  option->put(PREF_METALINK_FILE, A2_TEST_DIR "/test.xml");
  option->put(PREF_DIR, "/tmp");
  option->put(PREF_OUT, "file.out");
  {
    std::vector<std::shared_ptr<RequestGroup>> result;

    createRequestGroupForMetalink(result, option);

    REQUIRE_EQ((size_t)5, result.size());
    std::shared_ptr<RequestGroup> group = result[0];
    auto uris = group->getDownloadContext()->getFirstFileEntry()->getUris();
    std::sort(uris.begin(), uris.end());
    REQUIRE_EQ((size_t)2, uris.size());
    REQUIRE_EQ(std::string("http://httphost/aria2-0.5.2.tar.bz2"), uris[0]);
    REQUIRE_EQ(std::string("sftp://ftphost/aria2-0.5.2.tar.bz2"), uris[1]);
    REQUIRE(group->getCurlDownload());
  }
}

#endif

} // namespace aria2
