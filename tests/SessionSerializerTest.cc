#include "SessionSerializer.h"

#include <iostream>
#include <fstream>

#include "a2doctest.h"

#include "TestUtil.h"
#include "RequestGroupMan.h"
#include "array_fun.h"
#include "download_helper.h"
#include "UriListParser.h"
#include "DefaultPieceStorage.h"
#include "prefs.h"
#include "Option.h"
#include "a2functional.h"
#include "FileEntry.h"
#include "SelectEventPoll.h"
#include "DownloadEngine.h"
#include "Ed2kAttribute.h"
#include "Ed2kKadState.h"
#include "Ed2kUploadQueue.h"
#include "util.h"

namespace aria2 {

class SessionSerializerTest {

public:
  void testSave();
  void testSaveErrorDownload();
  void testSaveEd2kDownload();
  void testSaveActiveEd2kSharing();
};

A2_TEST(SessionSerializerTest, testSave)
A2_TEST(SessionSerializerTest, testSaveErrorDownload)
A2_TEST(SessionSerializerTest, testSaveEd2kDownload)
A2_TEST(SessionSerializerTest, testSaveActiveEd2kSharing)

void SessionSerializerTest::testSave()
{
#if defined(ENABLE_BITTORRENT) && defined(ENABLE_METALINK)
  std::vector<std::string> uris{
      "http://localhost/file", "http://mirror/file",
      A2_TEST_DIR "/test.torrent", A2_TEST_DIR "/serialize_session.meta4",
      "magnet:?xt=urn:btih:248D0A1CD08284299DE78D5C1ED359BB46717D8C"};
  std::vector<std::shared_ptr<RequestGroup>> result;
  std::shared_ptr<Option> option(new Option());
  option->put(PREF_DIR, "/tmp");
  createRequestGroupForUri(result, option, uris);
  REQUIRE_EQ((size_t)5, result.size());
  result[4]->getOption()->put(PREF_PAUSE, A2_V_TRUE);
  option->put(PREF_MAX_DOWNLOAD_RESULT, "10");
  RequestGroupMan rgman{result, 1, option.get()};
  SessionSerializer s(&rgman);
  std::shared_ptr<DownloadResult> drs[] = {
      // REMOVED downloads will not be saved.
      createDownloadResult(error_code::REMOVED, "http://removed"),
      createDownloadResult(error_code::TIME_OUT, "http://error"),
      createDownloadResult(error_code::FINISHED, "http://finished"),
      createDownloadResult(error_code::FINISHED, "http://force-save")};
  // This URI will be discarded because same URI exists in remaining
  // URIs.
  drs[1]->fileEntries[0]->getRemainingUris().push_back("http://error");
  drs[1]->fileEntries[0]->getRemainingUris().push_back("http://error3");
  // This URI will be discarded because same URI exists in remaining
  // URIs.
  drs[1]->fileEntries[0]->getRemainingUris().push_back("http://error");
  //
  // This URI will be discarded because same URI exists in remaining
  // URIs.
  drs[1]->fileEntries[0]->getSpentUris().push_back("http://error");
  drs[1]->fileEntries[0]->getSpentUris().push_back("http://error2");
  // This URI will be discarded because same URI exists in remaining
  // URIs.
  drs[1]->fileEntries[0]->getSpentUris().push_back("http://error");

  drs[3]->option->put(PREF_FORCE_SAVE, A2_V_TRUE);
  for (size_t i = 0; i < sizeof(drs) / sizeof(drs[0]); ++i) {
    rgman.addDownloadResult(drs[i]);
  }

  DownloadEngine e(make_unique<SelectEventPoll>());
  e.setOption(option.get());
  rgman.fillRequestGroupFromReserver(&e);
  REQUIRE_EQ((size_t)1, rgman.getRequestGroups().size());

  std::string filename =
      A2_TEST_OUT_DIR "/aria2_SessionSerializerTest_testSave";
  s.save(filename);
  std::ifstream ss(filename.c_str(), std::ios::binary);
  std::string line;
  std::getline(ss, line);
  REQUIRE_EQ(std::string("http://error\thttp://error3\thttp://error2\t"), line);
  std::getline(ss, line);
  REQUIRE_EQ(fmt(" gid=%s", drs[1]->gid->toHex().c_str()), line);
  std::getline(ss, line);
  // finished and force-save option
  REQUIRE_EQ(std::string("http://force-save\t"), line);
  std::getline(ss, line);
  REQUIRE_EQ(fmt(" gid=%s", drs[3]->gid->toHex().c_str()), line);
  std::getline(ss, line);
  REQUIRE_EQ(std::string(" force-save=true"), line);
  // Check active download is also saved
  std::getline(ss, line);
  REQUIRE_EQ(uris[1] + "\t" + uris[0] + "\t", line);
  std::getline(ss, line);
  REQUIRE_EQ(fmt(" gid=%s", GroupId::toHex(result[0]->getGID()).c_str()), line);
  std::getline(ss, line);
  REQUIRE_EQ(std::string(" dir=/tmp"), line);
  std::getline(ss, line);
  REQUIRE_EQ(uris[2], line);
  std::getline(ss, line);
  REQUIRE_EQ(fmt(" gid=%s", GroupId::toHex(result[1]->getGID()).c_str()), line);
  std::getline(ss, line);
  REQUIRE_EQ(std::string(" dir=/tmp"), line);
  std::getline(ss, line);
  REQUIRE_EQ(uris[3], line);
  std::getline(ss, line);
  // local metalink download does not save meaningful GID
  REQUIRE(fmt(" gid=%s", GroupId::toHex(result[2]->getGID()).c_str()) != line);
  std::getline(ss, line);
  REQUIRE_EQ(std::string(" dir=/tmp"), line);
  std::getline(ss, line);
  REQUIRE_EQ(uris[4], line);
  std::getline(ss, line);
  REQUIRE_EQ(fmt(" gid=%s", GroupId::toHex(result[4]->getGID()).c_str()), line);
  std::getline(ss, line);
  REQUIRE_EQ(std::string(" dir=/tmp"), line);
  std::getline(ss, line);
  REQUIRE_EQ(std::string(" pause=true"), line);
  std::getline(ss, line);
  REQUIRE(!ss);
#endif // defined(ENABLE_BITTORRENT) && defined(ENABLE_METALINK)
}

void SessionSerializerTest::testSaveErrorDownload()
{
  std::shared_ptr<DownloadResult> dr =
      createDownloadResult(error_code::TIME_OUT, "http://error");
  dr->fileEntries[0]->getSpentUris().swap(
      dr->fileEntries[0]->getRemainingUris());
  std::shared_ptr<Option> option(new Option());
  option->put(PREF_MAX_DOWNLOAD_RESULT, "10");
  RequestGroupMan rgman{std::vector<std::shared_ptr<RequestGroup>>(), 1,
                        option.get()};
  rgman.addDownloadResult(dr);
  SessionSerializer s(&rgman);
  std::string filename =
      A2_TEST_OUT_DIR "/aria2_SessionSerializerTest_testSaveErrorDownload";
  REQUIRE(s.save(filename));
  std::ifstream ss(filename.c_str(), std::ios::binary);
  std::string line;
  std::getline(ss, line);
  REQUIRE_EQ(std::string("http://error\t"), line);
}

void SessionSerializerTest::testSaveEd2kDownload()
{
  std::vector<std::string> uris{"ed2k://|file|aria2%20next.bin|9728001|"
                                "0123456789abcdef0123456789abcdef|"
                                "p=11111111111111111111111111111111:"
                                "22222222222222222222222222222222|/"};
  auto option = std::make_shared<Option>();
  option->put(PREF_DIR, "/tmp");
  std::vector<std::shared_ptr<RequestGroup>> result;
  createRequestGroupForUri(result, option, uris);
  REQUIRE_EQ((size_t)1, result.size());
  auto attrs = getEd2kAttrs(result[0]->getDownloadContext());
  std::string firstPieceHash("33333333333333333333333333333333");
  std::string secondPieceHash("44444444444444444444444444444444");
  attrs->pieceHashes = {
      util::fromHex(firstPieceHash.begin(), firstPieceHash.end()),
      util::fromHex(secondPieceHash.begin(), secondPieceHash.end())};
  ed2k::Endpoint learnedPeer;
  learnedPeer.host = "203.0.113.20";
  learnedPeer.port = 4662;
  attrs->peers.push_back(learnedPeer);
  option->put(PREF_MAX_DOWNLOAD_RESULT, "10");
  RequestGroupMan rgman{result, 1, option.get()};
  SessionSerializer serializer(&rgman);
  std::string filename =
      A2_TEST_OUT_DIR "/aria2_SessionSerializerTest_testSaveEd2kDownload";
  REQUIRE(serializer.save(filename));

  std::ifstream in(filename.c_str(), std::ios::binary);
  std::string line;
  std::getline(in, line);
  REQUIRE(!in);
}

void SessionSerializerTest::testSaveActiveEd2kSharing()
{
  std::vector<std::string> uris{"ed2k://|file|aria2%20sharing.bin|9728001|"
                                "0123456789abcdef0123456789abcdef|/"};
  auto option = std::make_shared<Option>();
  option->put(PREF_DIR, "/tmp");
  option->put(PREF_FORCE_SAVE, A2_V_FALSE);
  option->put(PREF_DETACH_SHARE_ONLY, A2_V_TRUE);
  option->put(PREF_MAX_DOWNLOAD_RESULT, "10");
  std::vector<std::shared_ptr<RequestGroup>> result;
  createRequestGroupForUri(result, option, uris);
  REQUIRE_EQ((size_t)1, result.size());

  RequestGroupMan rgman{std::vector<std::shared_ptr<RequestGroup>>(), 1,
                        option.get()};
  auto group = result[0];
  group->initPieceStorage();
  group->getPieceStorage()->markAllPiecesDone();
  group->setRequestGroupMan(&rgman);
  rgman.addRequestGroup(group);
  group->enableSeedOnly();
  group->setPauseRequested(true);

  SessionSerializer serializer(&rgman);
  std::string filename =
      A2_TEST_OUT_DIR "/aria2_SessionSerializerTest_testSaveActiveEd2kSharing";
  REQUIRE(serializer.save(filename));

  std::ifstream in(filename.c_str(), std::ios::binary);
  std::string line;
  std::getline(in, line);
  REQUIRE(!in);
}

} // namespace aria2
