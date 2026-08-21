#include "Ed2kSession.h"

#include "a2doctest.h"

#include "DownloadContext.h"
#include "Ed2kAttribute.h"
#include "Ed2kUploadQueue.h"
#include "File.h"
#include "GroupId.h"
#include "Option.h"
#include "RequestGroup.h"
#include "TestUtil.h"
#include "ed2k_constants.h"
#include "ed2k_hash.h"

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
};

A2_TEST(Ed2kSessionTest, testRestoresRuntimeStateAcrossRestart)

void Ed2kSessionTest::testRestoresRuntimeStateAcrossRestart()
{
  const std::string stateFile = A2_TEST_OUT_DIR "/ed2k-runtime.state";
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
    REQUIRE((attrs->peerStates.front().sourceFlags & PEER_SOURCE_PERSISTED) != 0);
    REQUIRE_EQ((size_t)1, queue.credits().list().size());
    REQUIRE_EQ(peerHash, queue.credits().list().front().userHash);
    REQUIRE_EQ((uint64_t)1234, queue.credits().list().front().uploaded);
    REQUIRE_EQ((uint64_t)5678, queue.credits().list().front().downloaded);
  }

  File(stateFile).remove();
}

} // namespace ed2k

} // namespace aria2
