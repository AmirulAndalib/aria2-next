/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
#include "File.h"
#include "ed2k_kad_search.h"
#include <cstdint>
#include <limits>
#include <memory>
#include "download_helper.h"

#include <string>
#include <cstdlib>
#include <vector>

#include "a2doctest.h"

#include "RequestGroup.h"
#include "DownloadEngine.h"
#include "DownloadContext.h"
#include "DefaultPieceStorage.h"
#include "Ed2kAttribute.h"
#include "Ed2kKadCommand.h"
#include "Ed2kUploadQueue.h"
#include "Option.h"
#include "RequestGroupMan.h"
#include "SelectEventPoll.h"
#include "SegmentMan.h"
#include "ed2k_endpoint.h"
#include "ed2k_hash.h"
#include "ed2k_kad.h"
#include "ed2k_link.h"
#include "ed2k_peer.h"
#include "ed2k_policy.h"
#include "ed2k_server.h"
#include "prefs.h"
#include "TestUtil.h"
#include "support/Numbers.h"
#include "a2functional.h"
#ifdef ENABLE_BITTORRENT
#  include "BtDownload.h"
#endif // ENABLE_BITTORRENT

namespace aria2 {

TEST_CASE("DownloadHelperTest.testEd2kPeerDeduplication")
{
  Ed2kAttribute attrs;
  ed2k::Endpoint peer;
  peer.host = "203.0.113.10";
  peer.port = 4662;

  REQUIRE(addEd2kPeer(&attrs, peer));
  REQUIRE(!addEd2kPeer(&attrs, peer));
  REQUIRE_EQ((size_t)1, attrs.peers.size());
  auto state = getEd2kPeerState(&attrs, peer);
  REQUIRE(state);
  REQUIRE_EQ(std::string("203.0.113.10"), state->endpoint.host);
  REQUIRE_EQ((uint16_t)4662, state->endpoint.port);

  peer.port = 4663;
  REQUIRE(addEd2kPeer(&attrs, peer));
  REQUIRE_EQ((size_t)2, attrs.peers.size());

  std::vector<bool> partStatus;
  partStatus.push_back(true);
  partStatus.push_back(false);
  REQUIRE(markEd2kPeerQueued(&attrs, peer, 7, partStatus));
  state = getEd2kPeerState(&attrs, peer);
  REQUIRE(state->queued);
  REQUIRE(!state->dead);
  REQUIRE_EQ((uint16_t)7, state->queueRank);
  REQUIRE_EQ((size_t)2, state->partStatus.size());

  REQUIRE(markEd2kPeerDead(&attrs, peer, 100, 30));
  REQUIRE(!state->queued);
  REQUIRE(state->dead);
  REQUIRE_EQ((uint32_t)1, state->failCount);
  REQUIRE_EQ((int64_t)100, state->lastFailureTime);
  REQUIRE_EQ((int64_t)130, state->nextRetryTime);

  Ed2kAttribute identities;
  identities.clientHash = std::string(ed2k::HASH_LENGTH, '\x11');
  ed2k::Endpoint identified;
  identified.host = "203.0.113.20";
  identified.port = 4662;
  identified.userHash = std::string(ed2k::HASH_LENGTH, '\x22');
  REQUIRE(addEd2kPeer(&identities, identified, ed2k::PEER_SOURCE_SERVER));
  identified.host = "203.0.113.21";
  identified.port = 4663;
  REQUIRE(!addEd2kPeer(&identities, identified, ed2k::PEER_SOURCE_KAD));
  REQUIRE_EQ((size_t)1, identities.peerStates.size());
  REQUIRE_EQ(std::string("203.0.113.21"),
             identities.peerStates.front().endpoint.host);
  REQUIRE((identities.peerStates.front().sourceFlags &
           ed2k::PEER_SOURCE_SERVER) != 0);
  REQUIRE((identities.peerStates.front().sourceFlags & ed2k::PEER_SOURCE_KAD) !=
          0);

  identified.userHash = identities.clientHash;
  REQUIRE(!addEd2kPeer(&identities, identified));
  identified.userHash.clear();
  identified.host = "127.0.0.1";
  REQUIRE(!addEd2kPeer(&identities, identified));
}

TEST_CASE("DownloadHelperTest.testEd2kKadSourcePeerMergePreservesUdpMetadata")
{
  Ed2kAttribute attrs;
  ed2k::KadSourceEndpoint source;
  source.endpoint.host = "203.0.113.44";
  source.endpoint.port = 4662;
  source.endpoint.userHash = std::string(ed2k::HASH_LENGTH, '\x44');
  source.endpoint.cryptOptions = 0x03;
  source.udpPort = 4672;
  source.sourceType = 1;

  REQUIRE(addEd2kKadSourcePeer(&attrs, source, ed2k::PEER_SOURCE_KAD));
  REQUIRE_EQ((size_t)1, attrs.peers.size());
  auto state = getEd2kPeerState(&attrs, source.endpoint);
  REQUIRE(state);
  REQUIRE_EQ(std::string(ed2k::HASH_LENGTH, '\x44'), state->endpoint.userHash);
  REQUIRE_EQ((uint16_t)0x03, state->endpoint.cryptOptions);
  REQUIRE_EQ((uint16_t)4672, state->udpPort);
  REQUIRE_EQ((uint8_t)4, state->udpVersion);

  source.udpPort = 4682;
  REQUIRE(!addEd2kKadSourcePeer(&attrs, source, ed2k::PEER_SOURCE_KAD));
  REQUIRE_EQ((uint16_t)4682, state->udpPort);

  source.endpoint.host = "203.0.113.45";
  source.endpoint.userHash = std::string(ed2k::HASH_LENGTH, '\x45');
  source.sourceType = 3;
  source.buddyIp = ed2k::ipv4ToEndpointValue("203.0.113.99");
  source.buddyPort = 4672;
  source.buddyHash = std::string(ed2k::HASH_LENGTH, '\x55');
  REQUIRE(addEd2kKadSourcePeer(&attrs, source, ed2k::PEER_SOURCE_KAD));
  REQUIRE_EQ((size_t)2, attrs.peers.size());
  auto callbackState = getEd2kPeerState(&attrs, source.endpoint);
  REQUIRE(callbackState);
  REQUIRE(callbackState->lowId);
  REQUIRE(callbackState->callbackRequested);
  REQUIRE_EQ(ed2k::LowIdCallbackState::REQUESTED,
             callbackState->lowIdCallbackState);
  REQUIRE_EQ(std::string("203.0.113.99"), callbackState->callbackBuddy.host);
  REQUIRE_EQ((uint16_t)4672, callbackState->callbackBuddy.port);
  REQUIRE_EQ(ed2k::ed2kHashToKadId(std::string(ed2k::HASH_LENGTH, '\x55')),
             callbackState->callbackBuddyId);

  source.endpoint.host = "203.0.113.46";
  source.endpoint.userHash = std::string(ed2k::HASH_LENGTH, '\x46');
  source.endpoint.cryptOptions =
      ed2k::SOURCE_CRYPT_SUPPORT | ed2k::SOURCE_CRYPT_DIRECT_CALLBACK;
  source.sourceType = 6;
  source.udpPort = 4692;
  source.buddyIp = 0;
  source.buddyPort = 0;
  source.buddyHash.clear();
  source.buddyId.clear();
  REQUIRE(!addEd2kKadSourcePeer(&attrs, source, ed2k::PEER_SOURCE_KAD));
  attrs.serverStates.push_back(ed2k::ServerState());
  attrs.serverStates.back().connected = true;
  attrs.serverStates.back().handshakeCompleted = true;
  attrs.serverStates.back().highId = true;
  REQUIRE(addEd2kKadSourcePeer(&attrs, source, ed2k::PEER_SOURCE_KAD));
  REQUIRE_EQ((size_t)3, attrs.peers.size());
  auto directCallbackState = getEd2kPeerState(&attrs, source.endpoint);
  REQUIRE(directCallbackState);
  REQUIRE(directCallbackState->lowId);
  REQUIRE(directCallbackState->callbackRequested);
  REQUIRE_EQ(ed2k::LowIdCallbackState::REQUESTED,
             directCallbackState->lowIdCallbackState);
  REQUIRE_EQ((uint16_t)4692, directCallbackState->udpPort);
  REQUIRE_EQ((uint8_t)4, directCallbackState->udpVersion);
  REQUIRE_EQ(ed2k::CallbackKind::DIRECT, directCallbackState->callbackKind);
}

TEST_CASE(
    "DownloadHelperTest.testEd2kServerSourceMergeAcceptsRequiredEncryption")
{
  Ed2kAttribute attrs;
  ed2k::FoundSource direct;
  direct.endpoint.host = "203.0.113.10";
  direct.endpoint.port = 4662;
  ed2k::FoundSource lowId;
  lowId.endpoint.host = "120.0.0.0";
  lowId.endpoint.port = 4662;
  lowId.clientId = 0x01020304;
  lowId.lowId = true;
  ed2k::FoundSource cryptRequired;
  cryptRequired.endpoint.host = "203.0.113.11";
  cryptRequired.endpoint.port = 4662;
  cryptRequired.endpoint.cryptOptions = ed2k::SOURCE_CRYPT_REQUIRE;

  REQUIRE_EQ((size_t)2,
             mergeEd2kServerSources(
                 &attrs,
                 std::vector<ed2k::FoundSource>{direct, lowId, cryptRequired},
                 ed2k::PEER_SOURCE_SERVER));
  REQUIRE_EQ((size_t)2, attrs.peers.size());
  REQUIRE_EQ(direct.endpoint.host, attrs.peers[0].host);
  auto state = getEd2kPeerState(&attrs, direct.endpoint);
  REQUIRE(state);
  REQUIRE((state->sourceFlags & ed2k::PEER_SOURCE_SERVER) != 0);
  state = getEd2kPeerState(&attrs, lowId.endpoint);
  REQUIRE(state);
  REQUIRE(state->lowId);
  REQUIRE(state->callbackImpossible);
  REQUIRE(!state->callbackRequested);
  REQUIRE((state->sourceFlags & ed2k::PEER_SOURCE_SERVER) != 0);
  REQUIRE_EQ((size_t)2, attrs.peers.size());
  REQUIRE_EQ((size_t)3, attrs.peerStates.size());
  REQUIRE(!ed2k::selectConnectPeer(attrs.peerStates, 0)->lowId);

  Ed2kAttribute callbackAttrs;
  REQUIRE(!addEd2kFoundSource(&callbackAttrs, lowId, ed2k::PEER_SOURCE_SERVER,
                              true));
  state = getEd2kPeerState(&callbackAttrs, lowId.endpoint);
  REQUIRE(state);
  REQUIRE(state->lowId);
  REQUIRE(state->callbackRequested);
  REQUIRE(!state->callbackImpossible);
  REQUIRE(!ed2k::selectConnectPeer(callbackAttrs.peerStates, 0));
  REQUIRE(markEd2kCallbackFailed(&callbackAttrs, lowId.clientId));
  REQUIRE(!state->callbackRequested);
  REQUIRE(state->callbackImpossible);
}

TEST_CASE("DownloadHelperTest.testEd2kAichHashRequiresQuorum")
{
  Ed2kAttribute attrs;
  const auto root = std::string(ed2k::AICH_HASH_LENGTH, '\x42');
  for (size_t i = 0; i < 9; ++i) {
    REQUIRE(!recordEd2kAichHashVote(&attrs, root,
                                    "203.0.113." + util::uitos(i + 1)));
  }
  REQUIRE(!attrs.aichRootTrusted);
  REQUIRE(recordEd2kAichHashVote(&attrs, root, "203.0.113.10"));
  REQUIRE(attrs.aichRootTrusted);
  REQUIRE_EQ(root, attrs.aichRootHash);
  REQUIRE(recordEd2kAichHashVote(&attrs, root, "203.0.113.10"));

  Ed2kAttribute linked;
  linked.link.aichHash = root;
  REQUIRE(recordEd2kAichHashVote(&linked, root, "203.0.113.1"));
  REQUIRE(linked.aichRootTrusted);
}

TEST_CASE("DownloadHelperTest.testEd2kSourceExchangeMergePolicy")
{
  Ed2kAttribute attrs;
  ed2k::Endpoint remote;
  remote.host = "203.0.113.20";
  remote.port = 4662;

  std::string userHash(16, '\x11');
  ed2k::SourceExchangeEntry first;
  first.endpoint.host = "203.0.113.10";
  first.endpoint.port = 4662;
  first.userHash = userHash;
  first.cryptOptions = 0x83;

  ed2k::SourceExchangeEntry duplicate = first;
  ed2k::SourceExchangeEntry self;
  self.endpoint = remote;
  ed2k::SourceExchangeEntry loopback;
  loopback.endpoint.host = "127.0.0.1";
  loopback.endpoint.port = 4662;

  std::vector<ed2k::SourceExchangeEntry> entries{first, duplicate, self,
                                                 loopback};
  REQUIRE_EQ((size_t)1, mergeEd2kSourceExchangePeers(&attrs, entries, remote));
  REQUIRE_EQ((size_t)1, attrs.peers.size());
  auto state = getEd2kPeerState(&attrs, first.endpoint);
  REQUIRE(state);
  REQUIRE_EQ(userHash, state->endpoint.userHash);
  REQUIRE_EQ((uint16_t)0x83, state->endpoint.cryptOptions);
  REQUIRE((state->sourceFlags & ed2k::PEER_SOURCE_EXCHANGE) != 0);
}

TEST_CASE("DownloadHelperTest.testEd2kSourcePolicyRanksSources")
{
  Ed2kAttribute attrs;
  ed2k::Endpoint sx;
  sx.host = "203.0.113.10";
  sx.port = 4662;
  ed2k::Endpoint kad;
  kad.host = "203.0.113.11";
  kad.port = 4662;
  ed2k::Endpoint server;
  server.host = "203.0.113.12";
  server.port = 4662;
  addEd2kPeer(&attrs, sx, ed2k::PEER_SOURCE_EXCHANGE);
  addEd2kPeer(&attrs, kad, ed2k::PEER_SOURCE_KAD);
  addEd2kPeer(&attrs, server, ed2k::PEER_SOURCE_SERVER);
  auto serverState = getEd2kPeerState(&attrs, server);
  serverState->failCount = 2;

  auto selected = ed2k::selectConnectPeer(attrs.peerStates, 0);

  REQUIRE(selected);
  REQUIRE_EQ(std::string("203.0.113.11"), selected->endpoint.host);

  auto kadState = getEd2kPeerState(&attrs, kad);
  REQUIRE(markEd2kPeerDead(&attrs, kad, 10, 30));
  selected = ed2k::selectConnectPeer(attrs.peerStates, 20);

  REQUIRE(selected);
  REQUIRE_EQ(std::string("203.0.113.10"), selected->endpoint.host);

  ed2k::Endpoint cryptRequired;
  cryptRequired.host = "203.0.113.13";
  cryptRequired.port = 4662;
  cryptRequired.cryptOptions = ed2k::SOURCE_CRYPT_REQUIRE;
  addEd2kPeer(&attrs, cryptRequired, ed2k::PEER_SOURCE_SERVER);
  auto cryptState = getEd2kPeerState(&attrs, cryptRequired);
  cryptState->failCount = 0;
  selected = ed2k::selectConnectPeer(attrs.peerStates, 40);

  REQUIRE(selected);
  REQUIRE_EQ(cryptRequired.host, selected->endpoint.host);
}

TEST_CASE("DownloadHelperTest.testEd2kPieceSelectionMatchesAMulePriorities")
{
  ed2k::PieceSelectionCandidate rare;
  rare.frequency = 1;
  rare.completedBlocks = 10;
  rare.totalBlocks = 100;

  auto common = rare;
  common.frequency = 20;
  common.completedBlocks = 90;
  REQUIRE(ed2k::rankPieceSelection(rare, 100) <
          ed2k::rankPieceSelection(common, 100));

  auto requested = common;
  requested.requested = true;
  REQUIRE(ed2k::rankPieceSelection(common, 100) <
          ed2k::rankPieceSelection(requested, 100));

  auto preview = common;
  preview.preview = true;
  REQUIRE(ed2k::rankPieceSelection(preview, 100) <
          ed2k::rankPieceSelection(common, 100));

  auto continuing = requested;
  continuing.continuing = true;
  REQUIRE_EQ((uint32_t)0, ed2k::rankPieceSelection(continuing, 100));
}

TEST_CASE("DownloadHelperTest.testEd2kSourcePolicyClassifiesLifecycle")
{
  ed2k::PeerState peer;
  REQUIRE_EQ(ed2k::PeerLifecycle::USEFUL,
             ed2k::classifyPeerLifecycle(peer, 100));
  peer.connecting = true;
  REQUIRE_EQ(ed2k::PeerLifecycle::CONNECTING,
             ed2k::classifyPeerLifecycle(peer, 100));
  peer.connecting = false;
  peer.queued = true;
  REQUIRE_EQ(ed2k::PeerLifecycle::QUEUED,
             ed2k::classifyPeerLifecycle(peer, 100));
  peer.outOfParts = true;
  REQUIRE_EQ(ed2k::PeerLifecycle::NO_NEEDED_PARTS,
             ed2k::classifyPeerLifecycle(peer, 100));
  peer.outOfParts = false;
  peer.dead = true;
  peer.nextRetryTime = 150;
  REQUIRE_EQ(ed2k::PeerLifecycle::DEAD, ed2k::classifyPeerLifecycle(peer, 100));
  REQUIRE_EQ(ed2k::PeerLifecycle::RETRYING,
             ed2k::classifyPeerLifecycle(peer, 160));
  peer.noFile = true;
  REQUIRE_EQ(ed2k::PeerLifecycle::NO_FILE,
             ed2k::classifyPeerLifecycle(peer, 160));
  peer.noFile = false;
  peer.cancelled = true;
  REQUIRE_EQ(ed2k::PeerLifecycle::CANCELLED,
             ed2k::classifyPeerLifecycle(peer, 160));
}

TEST_CASE("DownloadHelperTest.testEd2kSourcePolicyExpiresDeadSources")
{
  Ed2kAttribute attrs;
  ed2k::Endpoint peer;
  peer.host = "203.0.113.10";
  peer.port = 4662;
  addEd2kPeer(&attrs, peer, ed2k::PEER_SOURCE_SERVER);
  REQUIRE(markEd2kPeerDead(&attrs, peer, 100, 30));
  auto state = getEd2kPeerState(&attrs, peer);
  REQUIRE(state);
  REQUIRE(state->dead);
  REQUIRE(state->noFile);

  REQUIRE_EQ((size_t)0, expireEd2kDeadSources(&attrs, 120));
  REQUIRE(state->dead);
  REQUIRE_EQ((size_t)1, expireEd2kDeadSources(&attrs, 131));
  REQUIRE(!state->dead);
  REQUIRE(!state->noFile);
  REQUIRE_EQ((int64_t)0, state->nextRetryTime);
}

TEST_CASE("DownloadHelperTest.testEd2kSourcePolicyAppliesActiveCap")
{
  Ed2kAttribute attrs;
  ed2k::Endpoint first;
  first.host = "203.0.113.10";
  first.port = 4662;
  ed2k::Endpoint second;
  second.host = "203.0.113.11";
  second.port = 4662;
  ed2k::Endpoint queued;
  queued.host = "203.0.113.12";
  queued.port = 4662;
  addEd2kPeer(&attrs, first, ed2k::PEER_SOURCE_SERVER);
  addEd2kPeer(&attrs, second, ed2k::PEER_SOURCE_KAD);
  addEd2kPeer(&attrs, queued, ed2k::PEER_SOURCE_EXCHANGE);
  markEd2kPeerQueued(&attrs, queued, 2, std::vector<bool>{true});

  auto selected = ed2k::selectConnectPeer(attrs.peerStates, 100, 1);
  REQUIRE(selected);
  REQUIRE_EQ(std::string("203.0.113.10"), selected->endpoint.host);
  selected->connecting = true;
  selected = ed2k::selectConnectPeer(attrs.peerStates, 100, 1);
  REQUIRE(!selected);

  auto action = ed2k::selectPeerAction(attrs.peerStates, 100, 1);
  REQUIRE_EQ(ed2k::PeerActionType::REASK, action.type);
  REQUIRE(action.peer);
  REQUIRE_EQ(std::string("203.0.113.12"), action.peer->endpoint.host);

  selected = ed2k::selectConnectPeer(attrs.peerStates, 100, 2);
  REQUIRE(selected);
  REQUIRE_EQ(std::string("203.0.113.11"), selected->endpoint.host);
}

TEST_CASE("DownloadHelperTest.testEd2kSchedulingKeepsInlineSourceLabel")
{
  auto option = std::make_shared<Option>();
  const std::string outdir = A2_TEST_OUT_DIR "/ed2k-inline-source-label";
  const std::string outfile = outdir + "/aria2 next.bin";
  File(outfile).remove();
  File(outdir).mkdirs();

  std::vector<std::string> uris{
      "ed2k://|file|aria2%20next.bin|9728001|"
      "0123456789abcdef0123456789abcdef|sources,203.0.113.20:4662|/"};
  option->put(PREF_DIR, outdir);
  option->put(PREF_MAX_DOWNLOAD_LIMIT, "0");
  option->put(PREF_MAX_UPLOAD_LIMIT, "0");
  option->put(PREF_FILE_ALLOCATION, V_NONE);
  option->put(PREF_DRY_RUN, A2_V_FALSE);

  std::vector<std::shared_ptr<RequestGroup>> result;
  createRequestGroupForUri(result, option, uris);
  auto group = result[0];
  auto attrs = getEd2kAttrs(group->getDownloadContext());

  DownloadEngine engine(make_unique<SelectEventPoll>());
  engine.setOption(option.get());
  engine.setRequestGroupMan(make_unique<RequestGroupMan>(
      std::vector<std::shared_ptr<RequestGroup>>{group}, 1, option.get()));
  std::vector<std::unique_ptr<Command>> commands;
  group->createInitialCommand(commands, &engine);

  REQUIRE_EQ((size_t)1, attrs->peerStates.size());
  auto state = getEd2kPeerState(attrs, attrs->link.sources[0]);
  REQUIRE(state);
  REQUIRE((state->sourceFlags & ed2k::PEER_SOURCE_INLINE) != 0);
}

TEST_CASE("DownloadHelperTest.testEd2kPeerSchedulingSkipsBackoff")
{
  auto option = std::make_shared<Option>();
  std::vector<std::string> uris{"ed2k://|file|aria2%20next.bin|9728001|"
                                "0123456789abcdef0123456789abcdef|/"};
  option->put(PREF_DIR, "/tmp");
  option->put(PREF_ED2K_SERVER, "203.0.113.10:4661");
  option->put(PREF_MAX_DOWNLOAD_LIMIT, "0");
  option->put(PREF_MAX_UPLOAD_LIMIT, "0");
  option->put(PREF_FILE_ALLOCATION, V_NONE);
  option->put(PREF_DRY_RUN, A2_V_TRUE);

  std::vector<std::shared_ptr<RequestGroup>> result;
  createRequestGroupForUri(result, option, uris);
  auto group = result[0];
  auto attrs = getEd2kAttrs(group->getDownloadContext());
  ed2k::Endpoint peer;
  peer.host = "203.0.113.20";
  peer.port = 4662;
  addEd2kPeer(attrs, peer);
  auto state = getEd2kPeerState(attrs, peer);
  state->dead = true;
  state->nextRetryTime = std::numeric_limits<int64_t>::max();

  DownloadEngine engine(make_unique<SelectEventPoll>());
  engine.setOption(option.get());
  engine.setRequestGroupMan(make_unique<RequestGroupMan>(
      std::vector<std::shared_ptr<RequestGroup>>{group}, 1, option.get()));

  schedulePendingEd2kPeers(group.get(), &engine);

  REQUIRE_EQ((int32_t)0, group->getNumCommand());
}

TEST_CASE("DownloadHelperTest.testEd2kPeerSchedulingSkipsConnectingPeer")
{
  auto option = std::make_shared<Option>();
  std::vector<std::string> uris{"ed2k://|file|aria2%20next.bin|9728001|"
                                "0123456789abcdef0123456789abcdef|/"};
  option->put(PREF_DIR, "/tmp");
  option->put(PREF_ED2K_SERVER, "203.0.113.10:4661");
  option->put(PREF_MAX_DOWNLOAD_LIMIT, "0");
  option->put(PREF_MAX_UPLOAD_LIMIT, "0");
  option->put(PREF_FILE_ALLOCATION, V_NONE);
  option->put(PREF_DRY_RUN, A2_V_TRUE);

  std::vector<std::shared_ptr<RequestGroup>> result;
  createRequestGroupForUri(result, option, uris);
  auto group = result[0];
  auto attrs = getEd2kAttrs(group->getDownloadContext());
  ed2k::Endpoint peer;
  peer.host = "203.0.113.20";
  peer.port = 4662;
  addEd2kPeer(attrs, peer);
  auto state = getEd2kPeerState(attrs, peer);
  state->connecting = true;

  DownloadEngine engine(make_unique<SelectEventPoll>());
  engine.setOption(option.get());
  engine.setRequestGroupMan(make_unique<RequestGroupMan>(
      std::vector<std::shared_ptr<RequestGroup>>{group}, 1, option.get()));

  schedulePendingEd2kPeers(group.get(), &engine);

  REQUIRE_EQ((int32_t)0, group->getNumCommand());
}

} // namespace aria2
