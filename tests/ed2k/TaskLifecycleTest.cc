/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
#include "Option.h"
#include "ContextAttribute.h"
#include "Ed2kStore.h"
#include "a2netcompat.h"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <ios>
#include <string>
#include <utility>
#include "CommandTestSupport.h"
#include "Ed2kCommand.h"

#include <array>
#include <fstream>
#include "a2doctest.h"
#include <memory>
#include <vector>

#include "DownloadContext.h"
#include "DownloadEngine.h"
#include "Ed2kAttribute.h"
#include "Ed2kKadCommand.h"
#include "Ed2kListenCommand.h"
#include "Ed2kSession.h"
#include "Ed2kSharingTimeSeedCriteria.h"
#include "DefaultPieceStorage.h"
#include "DiskAdaptor.h"
#include "DownloadResult.h"
#include "SeedCheckCommand.h"
#include "ShareRatioSeedCriteria.h"
#include "FileEntry.h"
#include "File.h"
#include "RequestGroup.h"
#include "RequestGroupMan.h"
#include "SelectEventPoll.h"
#include "SocketCore.h"
#include "ed2k_constants.h"
#include "ed2k_hash.h"
#include "ed2k_link.h"
#include "ed2k_packet.h"
#include "ed2k_peer.h"
#include "prefs.h"
#include "a2functional.h"

namespace aria2 {

using namespace test::ed2k_command;

TEST_CASE("Ed2kCommandTest.testFinishedEd2kGroupEntersSeedOnly")
{
  auto option = createOption();
  auto dctx = createEd2kContext();
  auto group = createRequestGroup(option, dctx);
  group->initPieceStorage();
  group->getPieceStorage()->markAllPiecesDone();
  DownloadEngine engine(make_unique<SelectEventPoll>());
  engine.setOption(option.get());
  engine.setRequestGroupMan(make_unique<RequestGroupMan>(
      std::vector<std::shared_ptr<RequestGroup>>{group}, 5, option.get()));
  engine.getRequestGroupMan()->addRequestGroup(group);
  group->setRequestGroupMan(engine.getRequestGroupMan().get());

  group->enableSeedOnly();

  REQUIRE(group->isSeedOnlyEnabled());
  engine.getRequestGroupMan()->removeStoppedGroup(&engine);
  REQUIRE_EQ((size_t)1, engine.getRequestGroupMan()->countRequestGroup());
  REQUIRE_EQ((size_t)0,
             engine.getRequestGroupMan()->getDownloadResults().size());
}

TEST_CASE("Ed2kCommandTest.testCompleteLocalEd2kFileStartsAsSeed")
{
  auto option = createOption();
  option->put(PREF_ALLOW_OVERWRITE, A2_V_FALSE);
  const std::string data = "existing-ed2k-seed-data";
  const std::string path = A2_TEST_OUT_DIR "/ed2k-existing-seed.bin";
  {
    std::ofstream out(path.c_str(), std::ios::binary);
    out << data;
  }

  auto dctx = std::make_shared<DownloadContext>();
  const std::shared_ptr<FileEntry> entries[] = {
      std::make_shared<FileEntry>(path, data.size(), 0)};
  dctx->setFileEntries(std::begin(entries), std::end(entries));
  dctx->setPieceLength(ed2k::PIECE_LENGTH);
  auto attrs = make_unique<Ed2kAttribute>();
  attrs->link.type = ed2k::LinkType::FILE;
  attrs->link.name = "ed2k-existing-seed.bin";
  attrs->link.size = data.size();
  attrs->link.hash = ed2k::md4Digest(data);
  attrs->clientHash =
      normalizeEd2kClientHash(std::string(ed2k::HASH_LENGTH, '\x42'));
  dctx->setAttribute(CTX_ATTR_ED2K, std::move(attrs));

  auto group = createRequestGroup(option, dctx);
  DownloadEngine engine(make_unique<SelectEventPoll>());
  engine.setOption(option.get());
  engine.setRequestGroupMan(make_unique<RequestGroupMan>(
      std::vector<std::shared_ptr<RequestGroup>>{group}, 5, option.get()));
  engine.getRequestGroupMan()->addRequestGroup(group);
  group->setRequestGroupMan(engine.getRequestGroupMan().get());

  std::vector<std::unique_ptr<Command>> commands;
  group->createInitialCommand(commands, &engine);

  REQUIRE(group->downloadFinished());
  REQUIRE(group->isSeedOnlyEnabled());
}

TEST_CASE("Ed2kCommandTest.testInvalidLocalEd2kFileUsesSafetyRename")
{
  auto option = createOption();
  option->put(PREF_ALLOW_OVERWRITE, A2_V_FALSE);
  option->put(PREF_AUTO_FILE_RENAMING, A2_V_TRUE);
  const std::string data = "invalid-ed2k-seed-data";
  const std::string path = A2_TEST_OUT_DIR "/ed2k-invalid-seed.bin";
  File(path).remove();
  const std::string renamedPath = A2_TEST_OUT_DIR "/ed2k-invalid-seed.1.bin";
  File(renamedPath).remove();
  {
    std::ofstream out(path.c_str(), std::ios::binary);
    out << data;
  }

  auto dctx = std::make_shared<DownloadContext>();
  const std::shared_ptr<FileEntry> entries[] = {
      std::make_shared<FileEntry>(path, data.size(), 0)};
  dctx->setFileEntries(std::begin(entries), std::end(entries));
  dctx->setPieceLength(ed2k::PIECE_LENGTH);
  auto attrs = make_unique<Ed2kAttribute>();
  attrs->link.type = ed2k::LinkType::FILE;
  attrs->link.name = "ed2k-invalid-seed.bin";
  attrs->link.size = data.size();
  attrs->link.hash.assign(ed2k::HASH_LENGTH, '\x55');
  attrs->clientHash =
      normalizeEd2kClientHash(std::string(ed2k::HASH_LENGTH, '\x42'));
  dctx->setAttribute(CTX_ATTR_ED2K, std::move(attrs));

  auto group = createRequestGroup(option, dctx);
  DownloadEngine engine(make_unique<SelectEventPoll>());
  engine.setOption(option.get());
  engine.setRequestGroupMan(make_unique<RequestGroupMan>(
      std::vector<std::shared_ptr<RequestGroup>>{group}, 5, option.get()));
  engine.getRequestGroupMan()->addRequestGroup(group);
  group->setRequestGroupMan(engine.getRequestGroupMan().get());

  std::vector<std::unique_ptr<Command>> commands;
  group->createInitialCommand(commands, &engine);

  REQUIRE(!group->downloadFinished());
  REQUIRE(!group->isSeedOnlyEnabled());
  REQUIRE_EQ(renamedPath, group->getFirstFilePath());
}

TEST_CASE("Ed2kCommandTest.testCompletedEd2kSeedServesIncomingPeer")
{
  auto option = createOption();
  const std::string data = "completed-ed2k-seed-data";
  const std::string path = A2_TEST_OUT_DIR "/ed2k-incoming-seed.bin";
  File(path).remove();
  {
    std::ofstream out(path.c_str(), std::ios::binary);
    out << data;
  }

  auto dctx = std::make_shared<DownloadContext>();
  const std::shared_ptr<FileEntry> entries[] = {
      std::make_shared<FileEntry>(path, data.size(), 0)};
  dctx->setFileEntries(std::begin(entries), std::end(entries));
  dctx->setPieceLength(ed2k::PIECE_LENGTH);
  auto attrs = make_unique<Ed2kAttribute>();
  attrs->link.type = ed2k::LinkType::FILE;
  attrs->link.name = "ed2k-incoming-seed.bin";
  attrs->link.size = data.size();
  attrs->link.hash = ed2k::md4Digest(data);
  attrs->clientHash =
      normalizeEd2kClientHash(std::string(ed2k::HASH_LENGTH, '\x42'));
  dctx->setAttribute(CTX_ATTR_ED2K, std::move(attrs));

  auto group = createRequestGroup(option, dctx);
  DownloadEngine engine(make_unique<SelectEventPoll>());
  engine.setOption(option.get());
  engine.setRequestGroupMan(make_unique<RequestGroupMan>(
      std::vector<std::shared_ptr<RequestGroup>>{group}, 5, option.get()));
  engine.getRequestGroupMan()->addRequestGroup(group);
  group->setRequestGroupMan(engine.getRequestGroupMan().get());

  std::vector<std::unique_ptr<Command>> commands;
  group->createInitialCommand(commands, &engine);
  engine.addCommand(std::move(commands));
  runEngineTicks(engine, 1);

  SocketCore client;
  client.establishConnection("127.0.0.1", engine.getEd2kTcpPort());
  for (int i = 0; i < MAX_ENGINE_TICKS && !client.isWritable(0); ++i) {
    engine.run(true);
  }
  REQUIRE(client.isWritable(0));

  auto remoteInfo = ed2k::createLocalEmulePeerInfo();
  client.writeData(ed2k::createPacket(
      ed2k::PROTO_EDONKEY, ed2k::OP_HELLO,
      ed2k::createPeerHelloPayload(std::string(ed2k::HASH_LENGTH, '\x24'),
                                   0x04030201, 4662, ed2k::Endpoint(),
                                   "incoming-peer", remoteInfo, true)));
  runEngineTicks(engine, 1);

  auto helloAnswer = readPacket(
      std::shared_ptr<SocketCore>(&client, [](SocketCore*) {}), engine);
  REQUIRE_EQ(ed2k::OP_HELLOANSWER, packetHeaderOf(helloAnswer).opcode);
  const auto fileHash = getEd2kAttrs(dctx)->link.hash;
  client.writeData(ed2k::createPacket(ed2k::PROTO_EDONKEY,
                                      ed2k::OP_STARTUPLOADREQ, fileHash));
  runEngineTicks(engine, 1);
  auto accept = readPacket(
      std::shared_ptr<SocketCore>(&client, [](SocketCore*) {}), engine);
  REQUIRE_EQ(ed2k::OP_ACCEPTUPLOADREQ, packetHeaderOf(accept).opcode);

  std::vector<ed2k::PartRange> ranges(1);
  ranges[0].begin = 0;
  ranges[0].end = static_cast<int64_t>(data.size());
  client.writeData(ed2k::createPacket(
      ed2k::PROTO_EDONKEY, ed2k::OP_REQUESTPARTS,
      ed2k::createRequestPartsPayload(fileHash, ranges, false)));
  runEngineTicks(engine, 1);
  auto part = readPacket(
      std::shared_ptr<SocketCore>(&client, [](SocketCore*) {}), engine);
  REQUIRE_EQ(ed2k::OP_SENDINGPART, packetHeaderOf(part).opcode);
  const auto body = packetBodyOf(part);
  REQUIRE_EQ(fileHash, body.substr(0, ed2k::HASH_LENGTH));
  REQUIRE_EQ(static_cast<uint32_t>(0),
             ed2k::readUInt32(body.data() + ed2k::HASH_LENGTH));
  REQUIRE_EQ(static_cast<uint32_t>(data.size()),
             ed2k::readUInt32(body.data() + ed2k::HASH_LENGTH + 4));
  REQUIRE_EQ(data, body.substr(ed2k::HASH_LENGTH + 8));
  REQUIRE_EQ(static_cast<int64_t>(data.size()),
             group->calculateStat().allTimeUploadLength);
  REQUIRE_EQ(
      static_cast<int64_t>(data.size()),
      static_cast<int64_t>(
          engine.getRequestGroupMan()->getNetStat().getSessionUploadLength()));

  engine.requestHalt();
  runEngineTicks(engine, 1);
}

TEST_CASE("Ed2kCommandTest.testEd2kShutdownPreservesCompletedState")
{
  const std::string stateDirectory = A2_TEST_OUT_DIR "/ed2k-shutdown-state";
  const std::string database = stateDirectory + "/ed2k/state.db";
  const std::string output = A2_TEST_OUT_DIR "/ed2k-command-test.bin";
  File(database).remove();
  File(database + "-wal").remove();
  File(database + "-shm").remove();
  File(output).remove();

  {
    auto option = createOption();
    option->put(PREF_STATE_DIR, stateDirectory);
    auto group = createRequestGroup(option, createEd2kContext());
    group->initPieceStorage();
    group->getPieceStorage()->getDiskAdaptor()->openFile();
    const unsigned char zero = 0;
    group->getPieceStorage()->getDiskAdaptor()->writeData(&zero, 1,
                                                          ed2k::PIECE_LENGTH);
    group->getPieceStorage()->getDiskAdaptor()->closeFile();
    group->getPieceStorage()->markAllPiecesDone();

    DownloadEngine engine(make_unique<SelectEventPoll>());
    engine.setOption(option.get());
    engine.setRequestGroupMan(make_unique<RequestGroupMan>(
        std::vector<std::shared_ptr<RequestGroup>>{}, 5, option.get()));
    auto manager = engine.getRequestGroupMan().get();
    manager->addRequestGroup(group);
    group->setRequestGroupMan(manager);
    group->setState(RequestGroup::STATE_ACTIVE);

    auto session = manager->getEd2kSession();
    REQUIRE(session->checkpointDownload(group.get()));
    REQUIRE(session->loadDownloadState(group.get()) ==
            ed2k::DownloadStateLoadResult::Loaded);

    group->setHaltRequested(true, RequestGroup::SHUTDOWN_SIGNAL);
    manager->removeStoppedGroup(&engine);

    REQUIRE_EQ((size_t)0, manager->countRequestGroup());
    REQUIRE(session->loadDownloadState(group.get()) ==
            ed2k::DownloadStateLoadResult::Loaded);
    REQUIRE(session->discardDownload(group.get()));
  }

  File(database).remove();
  File(database + "-wal").remove();
  File(database + "-shm").remove();
  File(output).remove();
}

TEST_CASE("Ed2kCommandTest.testEd2kSeedTimeStopsSeedOnlyGroup")
{
  auto option = createOption();
  auto dctx = createEd2kContext();
  auto group = createRequestGroup(option, dctx);
  group->initPieceStorage();
  group->getPieceStorage()->markAllPiecesDone();
  DownloadEngine engine(make_unique<SelectEventPoll>());
  engine.setOption(option.get());
  engine.setRequestGroupMan(make_unique<RequestGroupMan>(
      std::vector<std::shared_ptr<RequestGroup>>{group}, 5, option.get()));
  engine.getRequestGroupMan()->addRequestGroup(group);
  group->setRequestGroupMan(engine.getRequestGroupMan().get());
  group->setState(RequestGroup::STATE_ACTIVE);
  getEd2kAttrs(dctx)->sharingTime.restore(1);
  group->enableSeedOnly();

  auto seedCheck = make_unique<SeedCheckCommand>(
      engine.newCUID(), group.get(), &engine,
      make_unique<Ed2kSharingTimeSeedCriteria>(group.get(), 1_s));
  seedCheck->setPieceStorage(group->getPieceStorage());
  engine.addCommand(std::move(seedCheck));
  runEngineTicks(engine, 3);
  engine.getRequestGroupMan()->removeStoppedGroup(&engine);

  REQUIRE(group->isHaltRequested());
  REQUIRE(group->isShareComplete());
  REQUIRE_EQ((size_t)0, engine.getRequestGroupMan()->countRequestGroup());
  REQUIRE_EQ((size_t)1,
             engine.getRequestGroupMan()->getDownloadResults().size());
}

TEST_CASE("Ed2kCommandTest.testEd2kSeedRatioStopsSeedOnlyGroup")
{
  auto option = createOption();
  auto dctx = createEd2kContext();
  auto group = createRequestGroup(option, dctx);
  group->initPieceStorage();
  group->getPieceStorage()->markAllPiecesDone();
  dctx->getNetStat().updateUpload(
      group->getPieceStorage()->getCompletedLength());
  DownloadEngine engine(make_unique<SelectEventPoll>());
  engine.setOption(option.get());
  engine.setRequestGroupMan(make_unique<RequestGroupMan>(
      std::vector<std::shared_ptr<RequestGroup>>{group}, 5, option.get()));
  engine.getRequestGroupMan()->addRequestGroup(group);
  group->setRequestGroupMan(engine.getRequestGroupMan().get());
  group->enableSeedOnly();

  auto ratioCriteria = make_unique<ShareRatioSeedCriteria>(1.0, dctx);
  ratioCriteria->setPieceStorage(group->getPieceStorage());
  auto seedCheck = make_unique<SeedCheckCommand>(
      engine.newCUID(), group.get(), &engine, std::move(ratioCriteria));
  seedCheck->setPieceStorage(group->getPieceStorage());
  engine.addCommand(std::move(seedCheck));
  runEngineTicks(engine, 3);
  engine.getRequestGroupMan()->removeStoppedGroup(&engine);

  REQUIRE(group->isHaltRequested());
  REQUIRE(group->isShareComplete());
  REQUIRE_EQ((size_t)0, engine.getRequestGroupMan()->countRequestGroup());
  REQUIRE_EQ((size_t)1,
             engine.getRequestGroupMan()->getDownloadResults().size());
}

TEST_CASE("Ed2kCommandTest.testEd2kListenerKeepsMultipleTasks")
{
  auto option = createOption();
  auto dctx1 = createEd2kContext();
  auto dctx2 = createEd2kContext();
  getEd2kAttrs(dctx2)->link.hash.assign(ed2k::HASH_LENGTH, '\x72');
  auto group1 = createRequestGroup(option, dctx1);
  auto group2 = createRequestGroup(option, dctx2);
  DownloadEngine engine(make_unique<SelectEventPoll>());
  engine.setOption(option.get());
  engine.setRequestGroupMan(make_unique<RequestGroupMan>(
      std::vector<std::shared_ptr<RequestGroup>>{group1, group2}, 5,
      option.get()));
  engine.getRequestGroupMan()->addRequestGroup(group1);
  engine.getRequestGroupMan()->addRequestGroup(group2);
  group1->setRequestGroupMan(engine.getRequestGroupMan().get());
  group2->setRequestGroupMan(engine.getRequestGroupMan().get());

  auto command =
      make_unique<Ed2kListenCommand>(engine.newCUID(), &engine, AF_INET);
  REQUIRE(command->bindPort(0));
  engine.addCommand(std::move(command));
  runEngineTicks(engine, 2);

  REQUIRE(engine.isEd2kTcpListenActive());
  engine.requestHalt();
  runEngineTicks(engine, 1);
}

TEST_CASE("Ed2kCommandTest.testPendingConnectDrainsAfterHalt")
{
  auto option = createOption();
  option->put(PREF_CONNECT_TIMEOUT, "60");
  DownloadEngine engine(make_unique<SelectEventPoll>());
  engine.setOption(option.get());
  auto dctx = createEd2kContext();
  auto group = createRequestGroup(option, dctx);
  engine.setRequestGroupMan(make_unique<RequestGroupMan>(
      std::vector<std::shared_ptr<RequestGroup>>{group}, 5, option.get()));
  engine.getRequestGroupMan()->addRequestGroup(group);
  group->setRequestGroupMan(engine.getRequestGroupMan().get());

  ed2k::Endpoint endpoint;
  endpoint.host = "192.0.2.1";
  endpoint.port = 4661;
  engine.addCommand(make_unique<Ed2kCommand>(engine.newCUID(), group.get(),
                                             &engine, endpoint, true, false));

  runEngineTicks(engine, 2);
  REQUIRE_EQ((int32_t)2, group->getNumCommand());

  group->setHaltRequested(true, RequestGroup::USER_REQUEST);
  engine.setRefreshInterval(std::chrono::milliseconds(0));
  for (int i = 0; i < MAX_ENGINE_TICKS && group->getNumCommand() != 0; ++i) {
    engine.run(true);
  }

  REQUIRE_EQ((int32_t)0, group->getNumCommand());
}

} // namespace aria2
