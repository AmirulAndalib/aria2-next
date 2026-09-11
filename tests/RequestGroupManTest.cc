#include "FileEntry.h"
#include "GroupId.h"
#include "aria2/aria2.h"
#include "error_code.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>
#include "RequestGroupMan.h"

#include <fstream>
#include <iostream>

#include "a2doctest.h"

#include "TestUtil.h"
#include "Ed2kAttribute.h"
#include "prefs.h"
#include "DownloadContext.h"
#include "RequestGroup.h"
#include "Option.h"
#include "DownloadResult.h"
#include "ServerStatMan.h"
#include "ServerStat.h"
#include "File.h"
#include "RecoverableException.h"
#include "a2functional.h"
#include "DownloadEngine.h"
#include "SelectEventPoll.h"
#include "UriListParser.h"
#include "Command.h"
#include "wallclock.h"

namespace aria2 {

namespace {
class ActiveDownloadCommand : public Command {
private:
  RequestGroup* requestGroup_;

public:
  ActiveDownloadCommand(cuid_t cuid, RequestGroup* requestGroup)
      : Command(cuid), requestGroup_(requestGroup)
  {
    setStatusActive();
    requestGroup_->increaseNumCommand();
  }

  ~ActiveDownloadCommand() { requestGroup_->decreaseNumCommand(); }

  bool execute() override { return requestGroup_->isHaltRequested(); }
};
} // namespace

class RequestGroupManTest {
protected:
  std::unique_ptr<DownloadEngine> e_;
  std::shared_ptr<Option> option_;
  RequestGroupMan* rgman_;

public:
  RequestGroupManTest()
  {
    option_ = std::make_shared<Option>();
    option_->put(PREF_PIECE_LENGTH, "1048576");
    option_->put(PREF_DIR, A2_TEST_OUT_DIR "/aria2_RequestGroupManTest");
    // To enable paused RequestGroup
    option_->put(PREF_ENABLE_RPC, A2_V_TRUE);
    File(option_->get(PREF_DIR)).mkdirs();
    e_ = make_unique<DownloadEngine>(make_unique<SelectEventPoll>());
    e_->setOption(option_.get());
    auto rgman = make_unique<RequestGroupMan>(
        std::vector<std::shared_ptr<RequestGroup>>{}, 3, option_.get());
    rgman_ = rgman.get();
    e_->setRequestGroupMan(std::move(rgman));
  }
  void testLoadServerStat();
};

TEST_CASE_FIXTURE(RequestGroupManTest, "RequestGroupManTest.testLoadServerStat")
{
  testLoadServerStat();
}

TEST_CASE_FIXTURE(RequestGroupManTest,
                  "RequestGroupManTest.testMergedTransferStat")
{
  global::wallclock().reset(24_h);
  RequestGroupMan manager({}, 1, option_.get());
  auto group = std::make_shared<RequestGroup>(GroupId::create(), option_);
  auto context = std::make_shared<DownloadContext>();
  group->setDownloadContext(context);
  group->setRequestGroupMan(&manager);
  group->setState(RequestGroup::STATE_ACTIVE);
  manager.addRequestGroup(group);
  context->resetDownloadStartTime();
  context->updateDownload(4096);
  context->updateUpload(1024);

  const auto stat = manager.calculateStat();
  REQUIRE_EQ(16384, stat.downloadSpeed);
  REQUIRE_EQ(4096, stat.uploadSpeed);
  REQUIRE_EQ((int64_t)4096, stat.sessionDownloadLength);
  REQUIRE_EQ((int64_t)1024, stat.sessionUploadLength);

  manager.setMaxOverallDownloadSpeedLimit(200);
  manager.setMaxOverallUploadSpeedLimit(50);
  REQUIRE(manager.doesOverallDownloadSpeedExceed());
  REQUIRE(manager.doesOverallUploadSpeedExceed());

  group->setPauseRequested(true);
  REQUIRE(!manager.doesOverallDownloadSpeedExceed());
  REQUIRE(!manager.doesOverallUploadSpeedExceed());
}

TEST_CASE_FIXTURE(RequestGroupManTest,
                  "RequestGroupManTest.testIsSameFileBeingDownloaded")
{
  std::shared_ptr<RequestGroup> rg1(
      new RequestGroup(GroupId::create(), std::make_shared<Option>(*option_)));
  std::shared_ptr<RequestGroup> rg2(
      new RequestGroup(GroupId::create(), std::make_shared<Option>(*option_)));

  std::shared_ptr<DownloadContext> dctx1(
      new DownloadContext(0, 0, "aria2.tar.bz2"));
  std::shared_ptr<DownloadContext> dctx2(
      new DownloadContext(0, 0, "aria2.tar.bz2"));

  rg1->setDownloadContext(dctx1);
  rg2->setDownloadContext(dctx2);

  RequestGroupMan gm(std::vector<std::shared_ptr<RequestGroup>>(), 1,
                     option_.get());

  gm.addRequestGroup(rg1);
  gm.addRequestGroup(rg2);

  REQUIRE(gm.isSameFileBeingDownloaded(rg1.get()));

  dctx2->getFirstFileEntry()->setPath("aria2.tar.gz");

  REQUIRE(!gm.isSameFileBeingDownloaded(rg1.get()));
}

TEST_CASE_FIXTURE(RequestGroupManTest,
                  "RequestGroupManTest.testGetInitialCommands")
{
  // TODO implement later
}

TEST_CASE_FIXTURE(RequestGroupManTest, "RequestGroupManTest.testSaveServerStat")
{
  RequestGroupMan rm(std::vector<std::shared_ptr<RequestGroup>>(), 0,
                     option_.get());
  std::shared_ptr<ServerStat> ss_localhost(new ServerStat("localhost", "http"));
  rm.addServerStat(ss_localhost);
  File f(A2_TEST_OUT_DIR "/aria2_RequestGroupManTest_testSaveServerStat");
  if (f.exists()) {
    f.remove();
  }
  REQUIRE(rm.saveServerStat(f.getPath()));
  REQUIRE(f.isFile());

  f.remove();
  REQUIRE(f.mkdirs());
  REQUIRE(!rm.saveServerStat(f.getPath()));
}

void RequestGroupManTest::testLoadServerStat()
{
  File f(A2_TEST_OUT_DIR "/aria2_RequestGroupManTest_testLoadServerStat");
  std::ofstream o(f.getPath().c_str(), std::ios::binary);
  o << "host=localhost, protocol=http, dl_speed=0, last_updated=1219505257,"
    << "status=OK";
  o.close();

  RequestGroupMan rm(std::vector<std::shared_ptr<RequestGroup>>(), 0,
                     option_.get());
  std::cerr << "testLoadServerStat" << std::endl;
  REQUIRE(rm.loadServerStat(f.getPath()));
  std::shared_ptr<ServerStat> ss_localhost =
      rm.findServerStat("localhost", "http");
  REQUIRE(ss_localhost);
  REQUIRE_EQ(std::string("localhost"), ss_localhost->getHostname());
}

TEST_CASE_FIXTURE(RequestGroupManTest,
                  "RequestGroupManTest.testChangeReservedGroupPosition")
{
  std::vector<std::shared_ptr<RequestGroup>> gs{
      std::make_shared<RequestGroup>(GroupId::create(),
                                     std::make_shared<Option>(*option_)),
      std::make_shared<RequestGroup>(GroupId::create(),
                                     std::make_shared<Option>(*option_)),
      std::make_shared<RequestGroup>(GroupId::create(),
                                     std::make_shared<Option>(*option_)),
      std::make_shared<RequestGroup>(GroupId::create(),
                                     std::make_shared<Option>(*option_))};
  RequestGroupMan rm(gs, 0, option_.get());

  REQUIRE_EQ((size_t)0, rm.changeReservedGroupPosition(gs[0]->getGID(), 0,
                                                       OFFSET_MODE_SET));
  REQUIRE_EQ((size_t)1, rm.changeReservedGroupPosition(gs[0]->getGID(), 1,
                                                       OFFSET_MODE_SET));
  REQUIRE_EQ((size_t)3, rm.changeReservedGroupPosition(gs[0]->getGID(), 10,
                                                       OFFSET_MODE_SET));
  REQUIRE_EQ((size_t)0, rm.changeReservedGroupPosition(gs[0]->getGID(), -10,
                                                       OFFSET_MODE_SET));

  REQUIRE_EQ((size_t)1, rm.changeReservedGroupPosition(gs[1]->getGID(), 0,
                                                       OFFSET_MODE_CUR));
  REQUIRE_EQ((size_t)2, rm.changeReservedGroupPosition(gs[1]->getGID(), 1,
                                                       OFFSET_MODE_CUR));
  REQUIRE_EQ((size_t)1, rm.changeReservedGroupPosition(gs[1]->getGID(), -1,
                                                       OFFSET_MODE_CUR));
  REQUIRE_EQ((size_t)0, rm.changeReservedGroupPosition(gs[1]->getGID(), -10,
                                                       OFFSET_MODE_CUR));
  REQUIRE_EQ((size_t)1, rm.changeReservedGroupPosition(gs[1]->getGID(), 1,
                                                       OFFSET_MODE_CUR));
  REQUIRE_EQ((size_t)3, rm.changeReservedGroupPosition(gs[1]->getGID(), 10,
                                                       OFFSET_MODE_CUR));
  REQUIRE_EQ((size_t)1, rm.changeReservedGroupPosition(gs[1]->getGID(), -2,
                                                       OFFSET_MODE_CUR));

  REQUIRE_EQ((size_t)3, rm.changeReservedGroupPosition(gs[3]->getGID(), 0,
                                                       OFFSET_MODE_END));
  REQUIRE_EQ((size_t)2, rm.changeReservedGroupPosition(gs[3]->getGID(), -1,
                                                       OFFSET_MODE_END));
  REQUIRE_EQ((size_t)0, rm.changeReservedGroupPosition(gs[3]->getGID(), -10,
                                                       OFFSET_MODE_END));
  REQUIRE_EQ((size_t)3, rm.changeReservedGroupPosition(gs[3]->getGID(), 10,
                                                       OFFSET_MODE_END));

  REQUIRE_EQ((size_t)4, rm.getReservedGroups().size());

  try {
    rm.changeReservedGroupPosition(GroupId::create()->getNumericId(), 0,
                                   OFFSET_MODE_CUR);
    FAIL("exception must be thrown.");
  }
  catch (RecoverableException& e) {
    // success
  }
}

TEST_CASE_FIXTURE(RequestGroupManTest,
                  "RequestGroupManTest.testFillRequestGroupFromReserver")
{
  std::shared_ptr<RequestGroup> rgs[] = {
      createRequestGroup(0, 0, "foo1", "http://host/foo1",
                         std::make_shared<Option>(*option_)),
      createRequestGroup(0, 0, "foo2", "http://host/foo2",
                         std::make_shared<Option>(*option_)),
      createRequestGroup(0, 0, "foo3", "http://host/foo3",
                         std::make_shared<Option>(*option_)),
      // Intentionally same path/URI for first RequestGroup and set
      // length explicitly to do duplicate filename check.
      createRequestGroup(0, 10, "foo1", "http://host/foo1",
                         std::make_shared<Option>(*option_)),
      createRequestGroup(0, 0, "foo4", "http://host/foo4",
                         std::make_shared<Option>(*option_)),
      createRequestGroup(0, 0, "foo5", "http://host/foo5",
                         std::make_shared<Option>(*option_))};
  rgs[1]->setPauseRequested(true);
  for (const auto& i : rgs) {
    rgman_->addReservedGroup(i);
  }
  rgman_->fillRequestGroupFromReserver(e_.get());

  REQUIRE_EQ((size_t)1, rgman_->getReservedGroups().size());
}

TEST_CASE_FIXTURE(
    RequestGroupManTest,
    "RequestGroupManTest.testFillRequestGroupFromReserver_uriParser")
{
  std::shared_ptr<RequestGroup> rgs[] = {
      createRequestGroup(0, 0, "mem1", "http://mem1",
                         std::make_shared<Option>(*option_)),
      createRequestGroup(0, 0, "mem2", "http://mem2",
                         std::make_shared<Option>(*option_)),
  };
  rgs[0]->setPauseRequested(true);
  for (const auto& i : rgs) {
    rgman_->addReservedGroup(i);
  }

  std::shared_ptr<UriListParser> flp(
      new UriListParser(A2_TEST_DIR "/filelist2.txt"));
  rgman_->setUriListParser(flp);

  rgman_->fillRequestGroupFromReserver(e_.get());

  RequestGroupList::const_iterator itr;
  REQUIRE_EQ((size_t)1, rgman_->getReservedGroups().size());
  itr = rgman_->getReservedGroups().begin();
  REQUIRE_EQ(rgs[0]->getGID(), (*itr)->getGID());
  REQUIRE_EQ((size_t)3, rgman_->getRequestGroups().size());
}

TEST_CASE_FIXTURE(RequestGroupManTest,
                  "RequestGroupManTest.testReduceMaxConcurrentDownloads")
{
  std::vector<std::shared_ptr<RequestGroup>> rgs{
      createRequestGroup(0, 0, "active1", "http://host/active1",
                         std::make_shared<Option>(*option_)),
      createRequestGroup(0, 0, "active2", "http://host/active2",
                         std::make_shared<Option>(*option_)),
      createRequestGroup(0, 0, "active3", "http://host/active3",
                         std::make_shared<Option>(*option_))};
  for (const auto& rg : rgs) {
    rg->setRequestGroupMan(rgman_);
    rg->setState(RequestGroup::STATE_ACTIVE);
    rgman_->addRequestGroup(rg);
    e_->addCommand(make_unique<ActiveDownloadCommand>(e_->newCUID(), rg.get()));
  }

  rgman_->setMaxConcurrentDownloads(1);
  rgman_->reduceActiveDownloadsToLimit(e_.get());
  REQUIRE(!rgs[0]->isHaltRequested());
  REQUIRE(rgs[1]->isHaltRequested());
  REQUIRE(rgs[2]->isHaltRequested());

  while (e_->run(true) != 0)
    ;

  REQUIRE_EQ((size_t)1, rgman_->getRequestGroups().size());
  auto active = rgman_->getRequestGroups().begin();
  REQUIRE_EQ(rgs[0]->getGID(), (*active)->getGID());
  REQUIRE_EQ((size_t)2, rgman_->getReservedGroups().size());
  REQUIRE(findReservedGroup(rgman_, rgs[1]->getGID()));
  REQUIRE(findReservedGroup(rgman_, rgs[2]->getGID()));
  REQUIRE(!rgs[1]->isPauseRequested());
  REQUIRE(!rgs[2]->isPauseRequested());
}

TEST_CASE_FIXTURE(RequestGroupManTest,
                  "RequestGroupManTest.testInsertReservedGroup")
{
  std::vector<std::shared_ptr<RequestGroup>> rgs1{
      std::shared_ptr<RequestGroup>(new RequestGroup(
          GroupId::create(), std::make_shared<Option>(*option_))),
      std::shared_ptr<RequestGroup>(new RequestGroup(
          GroupId::create(), std::make_shared<Option>(*option_)))};
  std::vector<std::shared_ptr<RequestGroup>> rgs2{
      std::shared_ptr<RequestGroup>(new RequestGroup(
          GroupId::create(), std::make_shared<Option>(*option_))),
      std::shared_ptr<RequestGroup>(new RequestGroup(
          GroupId::create(), std::make_shared<Option>(*option_)))};
  rgman_->insertReservedGroup(0, rgs1);
  REQUIRE_EQ((size_t)2, rgman_->getReservedGroups().size());
  RequestGroupList::const_iterator itr;
  itr = rgman_->getReservedGroups().begin();
  REQUIRE_EQ(rgs1[0]->getGID(), (*itr++)->getGID());
  REQUIRE_EQ(rgs1[1]->getGID(), (*itr++)->getGID());

  rgman_->insertReservedGroup(1, rgs2);
  REQUIRE_EQ((size_t)4, rgman_->getReservedGroups().size());
  itr = rgman_->getReservedGroups().begin();
  ++itr;
  REQUIRE_EQ(rgs2[0]->getGID(), (*itr++)->getGID());
  REQUIRE_EQ(rgs2[1]->getGID(), (*itr++)->getGID());
}

TEST_CASE_FIXTURE(RequestGroupManTest,
                  "RequestGroupManTest.testAddDownloadResult")
{
  std::string uri = "http://example.org";
  rgman_->setMaxDownloadResult(3);
  rgman_->addDownloadResult(createDownloadResult(error_code::TIME_OUT, uri));
  rgman_->addDownloadResult(createDownloadResult(error_code::FINISHED, uri));
  rgman_->addDownloadResult(createDownloadResult(error_code::FINISHED, uri));
  rgman_->addDownloadResult(createDownloadResult(error_code::FINISHED, uri));
  rgman_->addDownloadResult(createDownloadResult(error_code::FINISHED, uri));
  REQUIRE_EQ(error_code::TIME_OUT,
             rgman_->getDownloadStat().getLastErrorResult());
}

} // namespace aria2
