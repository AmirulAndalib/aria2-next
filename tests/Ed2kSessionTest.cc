#include "Ed2kSession.h"

#include "a2doctest.h"

#include "DownloadContext.h"
#include "DiskAdaptor.h"
#include "Ed2kAttribute.h"
#include "Ed2kUploadQueue.h"
#include "File.h"
#include "GroupId.h"
#include "Option.h"
#include "Piece.h"
#include "PieceStorage.h"
#include "RequestGroup.h"
#include "TestUtil.h"
#include "download_helper.h"
#include "ed2k_constants.h"
#include "ed2k_hash.h"
#include "wallclock.h"

namespace aria2 {

namespace ed2k {

namespace {

std::shared_ptr<RequestGroup> createEd2kGroup(const std::string& clientHash)
{
  auto group = std::make_shared<RequestGroup>(GroupId::create(),
                                              std::make_shared<Option>());
  auto context = std::make_shared<DownloadContext>(PIECE_LENGTH, 1,
                                                   A2_TEST_OUT_DIR "/state");
  auto attrs = std::make_shared<Ed2kAttribute>();
  attrs->link.hash = std::string(HASH_LENGTH, '\x71');
  attrs->link.size = 1;
  attrs->clientHash = clientHash;
  attrs->kadUdpVerifyKey = 0x10203040;
  attrs->kadRoutingTable =
      std::make_shared<KadRoutingTable>(ed2kHashToKadId(clientHash));
  context->setAttribute(CTX_ATTR_ED2K, attrs);
  group->setDownloadContext(context);
  return group;
}

} // namespace

class Ed2kSessionTest {
public:
  void testRestoresRuntimeStateAcrossRestart();
  void testRestoresPartialDownloadAfterPause();
  void testRestoresSharingLifecycleWithoutControlFile();
  void testSelectsAlternativeDownloadForPeer();
};

A2_TEST(Ed2kSessionTest, testRestoresRuntimeStateAcrossRestart)
A2_TEST(Ed2kSessionTest, testRestoresPartialDownloadAfterPause)
A2_TEST(Ed2kSessionTest, testRestoresSharingLifecycleWithoutControlFile)
A2_TEST(Ed2kSessionTest, testSelectsAlternativeDownloadForPeer)

void Ed2kSessionTest::testRestoresRuntimeStateAcrossRestart()
{
  const std::string stateFile = A2_TEST_OUT_DIR "/ed2k-runtime.db";
  File(stateFile).remove();
  const std::string clientHash(HASH_LENGTH, '\x31');
  const std::string peerHash(HASH_LENGTH, '\x42');

  {
    auto group = createEd2kGroup(clientHash);
    UploadQueue queue;
    Ed2kSession session(&queue, stateFile);
    session.registerDownload(group.get());
    auto attrs = getEd2kAttrs(group->getDownloadContext());

    KadContact contact;
    contact.id = std::string(HASH_LENGTH, '\x53');
    contact.host = "203.0.113.8";
    contact.udpPort = 4672;
    contact.tcpPort = 4662;
    contact.version = 8;
    attrs->kadRoutingTable->nodeSeen(contact, 100);
    attrs->lastKadFirewalledCheck = 500;
    attrs->lastKadSourcePublish = 600;
    attrs->kadObservedAddresses.push_back("203.0.113.55");
    attrs->kadFirewalled = false;

    ServerState server;
    server.endpoint.host = "203.0.113.10";
    server.endpoint.port = 4661;
    server.name = "Peer Server";
    server.users = 1234;
    server.connected = true;
    server.handshakeCompleted = true;
    attrs->serverStates.push_back(server);

    Endpoint source;
    source.host = "203.0.113.20";
    source.port = 4662;
    source.userHash = std::string(HASH_LENGTH, '\x63');
    source.cryptOptions = SOURCE_CRYPT_SUPPORT | SOURCE_CRYPT_REQUEST;
    REQUIRE(addEd2kPeer(attrs, source, PEER_SOURCE_SERVER));
    REQUIRE(markEd2kPeerQueued(attrs, source, 5, std::vector<bool>{true}));

    queue.credits().addUploaded(peerHash, 1234);
    queue.credits().addDownloaded(peerHash, 5678);
  }

  REQUIRE(File(stateFile).isFile());

  {
    auto group = createEd2kGroup(std::string(HASH_LENGTH, '\x61'));
    UploadQueue queue;
    Ed2kSession session(&queue, stateFile);
    session.registerDownload(group.get());
    auto attrs = getEd2kAttrs(group->getDownloadContext());

    REQUIRE_EQ(clientHash, attrs->clientHash);
    REQUIRE_EQ((uint32_t)0x10203040, attrs->kadUdpVerifyKey);
    REQUIRE_EQ((size_t)1, attrs->kadRoutingTable->liveSize());
    REQUIRE_EQ((int64_t)500, attrs->lastKadFirewalledCheck);
    REQUIRE_EQ((int64_t)600, attrs->lastKadSourcePublish);
    REQUIRE(!attrs->kadFirewalled);
    REQUIRE_EQ((size_t)1, attrs->kadObservedAddresses.size());
    REQUIRE_EQ(std::string("203.0.113.55"),
               attrs->kadObservedAddresses.front());
    REQUIRE_EQ((size_t)1, attrs->serverStates.size());
    REQUIRE_EQ(std::string("Peer Server"), attrs->serverStates.front().name);
    REQUIRE_EQ((uint32_t)1234, attrs->serverStates.front().users);
    REQUIRE(!attrs->serverStates.front().connected);
    REQUIRE(!attrs->serverStates.front().handshakeCompleted);
    REQUIRE_EQ((size_t)1, attrs->peerStates.size());
    REQUIRE_EQ(std::string("203.0.113.20"),
               attrs->peerStates.front().endpoint.host);
    REQUIRE_EQ((uint16_t)4662, attrs->peerStates.front().endpoint.port);
    REQUIRE((attrs->peerStates.front().sourceFlags & PEER_SOURCE_PERSISTED) !=
            0);
    REQUIRE_EQ((size_t)1, queue.credits().list().size());
    REQUIRE_EQ(peerHash, queue.credits().list().front().userHash);
    REQUIRE_EQ((uint64_t)1234, queue.credits().list().front().uploaded);
    REQUIRE_EQ((uint64_t)5678, queue.credits().list().front().downloaded);
  }

  File(stateFile).remove();
}

void Ed2kSessionTest::testRestoresPartialDownloadAfterPause()
{
  const std::string database = A2_TEST_OUT_DIR "/ed2k-partial.db";
  const std::string output = A2_TEST_OUT_DIR "/ed2k-partial.bin";
  File(database).remove();
  File(database + "-wal").remove();
  File(database + "-shm").remove();
  File(output).remove();
  File(output + ".1").remove();

  const std::string uri = "ed2k://|file|ed2k-partial.bin|10000000|"
                          "0123456789abcdef0123456789abcdef|/";
  auto option = std::make_shared<Option>();
  option->put(PREF_DIR, A2_TEST_OUT_DIR);
  option->put(PREF_OUT, "ed2k-partial.bin");
  auto group = createEd2kFileRequestGroup(uri, option);
  const auto gid = GroupId::toHex(group->getGID());
  group->initPieceStorage();
  auto storage = group->getPieceStorage();
  storage->getDiskAdaptor()->openFile();
  const unsigned char zero = 0;
  storage->getDiskAdaptor()->writeData(&zero, 1, 9999999);
  storage->getDiskAdaptor()->closeFile();
  storage->markPiecesDone(PIECE_LENGTH);
  auto partial = storage->getMissingPiece(1, 1);
  REQUIRE(partial);
  partial->completeBlock(0);
  const auto completed = group->getCompletedLength();
  REQUIRE(completed > 0);
  REQUIRE(completed < group->getTotalLength());
  group->setPauseRequested(true);

  {
    UploadQueue queue;
    Ed2kSession session(&queue, database);
    REQUIRE(session.checkpointDownload(group.get()));
  }

  group.reset();
  auto restoredOption = std::make_shared<Option>();
  restoredOption->put(PREF_DIR, A2_TEST_OUT_DIR);
  restoredOption->put(PREF_OUT, "ed2k-partial.bin");
  restoredOption->put(PREF_GID, gid);
  auto restored = createEd2kFileRequestGroup(uri, restoredOption);
  restored->initPieceStorage();
  {
    UploadQueue queue;
    Ed2kSession session(&queue, database);
    REQUIRE(session.loadDownloadState(restored.get()) ==
            DownloadStateLoadResult::Loaded);
  }
  REQUIRE_EQ(gid, GroupId::toHex(restored->getGID()));
  REQUIRE_EQ(output, restored->getDownloadContext()->getBasePath());
  REQUIRE_EQ(completed, restored->getCompletedLength());
  REQUIRE(!File(output + ".1").exists());

  restored.reset();
  File(database).remove();
  File(database + "-wal").remove();
  File(database + "-shm").remove();
  File(output).remove();
}

void Ed2kSessionTest::testRestoresSharingLifecycleWithoutControlFile()
{
  global::wallclock().reset(24_h);
  const std::string database = A2_TEST_OUT_DIR "/ed2k-progress.db";
  const std::string output = A2_TEST_OUT_DIR "/ed2k-progress.bin";
  File(database).remove();
  File(output).remove();
  File(output + ".aria2").remove();

  const std::string uri = "ed2k://|file|ed2k-progress.bin|10000000|"
                          "0123456789abcdef0123456789abcdef|/";
  auto option = std::make_shared<Option>();
  option->put(PREF_DIR, A2_TEST_OUT_DIR);
  option->put(PREF_OUT, "ed2k-progress.bin");
  option->put(PREF_ENABLE_RPC, A2_V_TRUE);
  auto group = createEd2kFileRequestGroup(uri, option);
  const auto gid = GroupId::toHex(group->getGID());
  group->initPieceStorage();
  auto storage = group->getPieceStorage();
  storage->getDiskAdaptor()->openFile();
  const unsigned char zero = 0;
  storage->getDiskAdaptor()->writeData(&zero, 1, 9999999);
  storage->getDiskAdaptor()->closeFile();
  storage->markAllPiecesDone();
  group->setState(RequestGroup::STATE_ACTIVE);
  global::wallclock().advance(7_s);
  group->setPauseRequested(true);
  REQUIRE_EQ((int64_t)7, group->getEd2kSharingTime());

  {
    UploadQueue queue;
    Ed2kSession session(&queue, database);
    REQUIRE(session.checkpointDownload(group.get()));
  }
  REQUIRE(File(database).isFile());
  REQUIRE(!File(output + ".aria2").exists());

  group.reset();
  auto restoredOption = std::make_shared<Option>();
  restoredOption->put(PREF_ENABLE_RPC, A2_V_TRUE);
  std::vector<std::shared_ptr<RequestGroup>> restoredGroups;
  {
    UploadQueue queue;
    Ed2kSession session(&queue, database);
    REQUIRE_EQ((size_t)1,
               session.restoreDownloads(restoredOption.get(), restoredGroups));
    REQUIRE_EQ((size_t)1, restoredGroups.size());
    auto restored = restoredGroups.front();
    REQUIRE_EQ(gid, GroupId::toHex(restored->getGID()));
    REQUIRE(restored->isPauseRequested());
    REQUIRE_EQ((int64_t)10000000, restored->getTotalLength());
    REQUIRE_EQ((int64_t)10000000, restored->getCompletedLength());
    REQUIRE(restored->isSeeder());
    REQUIRE_EQ((int64_t)7, restored->getEd2kSharingTime());

    global::wallclock().advance(5_s);
    REQUIRE_EQ((int64_t)7, restored->getEd2kSharingTime());
    restored->setPauseRequested(false);
    restored->setState(RequestGroup::STATE_ACTIVE);
    global::wallclock().advance(3_s);
    REQUIRE_EQ((int64_t)10, restored->getEd2kSharingTime());
    REQUIRE(session.checkpointDownload(restored.get()));
  }

  restoredGroups.clear();
  {
    UploadQueue queue;
    Ed2kSession session(&queue, database);
    REQUIRE_EQ((size_t)1,
               session.restoreDownloads(restoredOption.get(), restoredGroups));
    auto restored = restoredGroups.front();
    REQUIRE(!restored->isPauseRequested());
    REQUIRE(restored->isSeeder());
    REQUIRE_EQ((int64_t)10, restored->getEd2kSharingTime());
    restored->setState(RequestGroup::STATE_ACTIVE);
    global::wallclock().advance(2_s);
    REQUIRE_EQ((int64_t)12, restored->getEd2kSharingTime());
    REQUIRE(session.discardDownload(restored.get()));
  }

  restoredGroups.clear();
  {
    UploadQueue queue;
    Ed2kSession session(&queue, database);
    REQUIRE_EQ((size_t)0,
               session.restoreDownloads(restoredOption.get(), restoredGroups));
  }
  File(database).remove();
  File(output).remove();
  global::wallclock().reset();
}

void Ed2kSessionTest::testSelectsAlternativeDownloadForPeer()
{
  const std::string clientHash(HASH_LENGTH, '\x31');
  auto current = createEd2kGroup(clientHash);
  auto alternative = createEd2kGroup(clientHash);
  getEd2kAttrs(alternative->getDownloadContext())->link.hash =
      std::string(HASH_LENGTH, '\x72');
  current->initPieceStorage();
  alternative->initPieceStorage();

  Endpoint peer;
  peer.host = "203.0.113.60";
  peer.port = 4662;
  REQUIRE(addEd2kPeer(getEd2kAttrs(alternative->getDownloadContext()), peer,
                      PEER_SOURCE_EXCHANGE));
  REQUIRE(
      updateEd2kPeerPartStatus(getEd2kAttrs(alternative->getDownloadContext()),
                               peer, std::vector<bool>{true}));

  UploadQueue queue;
  Ed2kSession session(&queue, "");
  session.registerDownload(current.get());
  session.registerDownload(alternative.get());
  REQUIRE_EQ(alternative.get(),
             session.findAlternativeDownload(current.get(), peer));
  alternative->setPauseRequested(true);
  REQUIRE(!session.findAlternativeDownload(current.get(), peer));
}

} // namespace ed2k

} // namespace aria2
