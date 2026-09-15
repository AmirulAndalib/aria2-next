/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
#include "ed2k_kad_search.h"
#include <cstdint>
#include "download_helper.h"

#include <string>
#include <cstdlib>
#include <vector>

#include "a2doctest.h"

#include "Ed2kAttribute.h"
#include "Ed2kKadCommand.h"
#include "ed2k_endpoint.h"
#include "ed2k_hash.h"
#include "ed2k_link.h"
#include "ed2k_peer.h"
#include "ed2k_policy.h"
#include "TestUtil.h"

namespace aria2 {

TEST_CASE("DownloadHelperTest.testEd2kLowIdCallbackStateTransitions")
{
  Ed2kAttribute attrs;
  ed2k::FoundSource source;
  source.endpoint.host = "120.0.0.0";
  source.endpoint.port = 4662;
  source.clientId = 0x00000120;
  source.lowId = true;

  REQUIRE(!addEd2kFoundSource(&attrs, source, ed2k::PEER_SOURCE_SERVER, true));
  auto state = getEd2kPeerState(&attrs, source.endpoint);
  REQUIRE(state);
  REQUIRE_EQ(ed2k::LowIdCallbackState::REQUESTED, state->lowIdCallbackState);
  REQUIRE(state->callbackRequested);
  REQUIRE(!state->callbackImpossible);
  REQUIRE_EQ(ed2k::PeerLifecycle::CALLBACK_WAITING,
             ed2k::classifyPeerLifecycle(*state, 100));
  REQUIRE(markEd2kCallbackRequestSent(&attrs, source.clientId, 105, 45));
  REQUIRE_EQ((int64_t)105, state->lastCallbackTime);
  REQUIRE_EQ((int64_t)150, state->callbackDeadline);

  ed2k::Endpoint callbackPeer;
  callbackPeer.host = "203.0.113.44";
  callbackPeer.port = 4662;
  REQUIRE(markEd2kCallbackAccepted(&attrs, source.clientId, callbackPeer, 120));
  state = getEd2kPeerState(&attrs, callbackPeer);
  REQUIRE(state);
  REQUIRE_EQ(ed2k::LowIdCallbackState::ACCEPTED, state->lowIdCallbackState);
  REQUIRE(!state->lowId);
  REQUIRE(!state->callbackRequested);
  REQUIRE(!state->callbackImpossible);
  REQUIRE_EQ(source.clientId, state->clientId);
  REQUIRE_EQ((int64_t)120, state->lastCallbackTime);
  REQUIRE_EQ(ed2k::PeerLifecycle::USEFUL,
             ed2k::classifyPeerLifecycle(*state, 121));

  REQUIRE(markEd2kCallbackCompleted(&attrs, callbackPeer));
  REQUIRE_EQ(ed2k::LowIdCallbackState::COMPLETED, state->lowIdCallbackState);

  Ed2kAttribute directAttrs;
  directAttrs.kadFirewalled = false;
  ed2k::KadSourceEndpoint direct;
  direct.endpoint.host = "203.0.113.44";
  direct.endpoint.port = 4662;
  direct.endpoint.userHash = std::string(ed2k::HASH_LENGTH, '\x44');
  direct.endpoint.cryptOptions = ed2k::SOURCE_CRYPT_DIRECT_CALLBACK;
  direct.udpPort = 4672;
  direct.sourceType = 6;
  REQUIRE(addEd2kKadSourcePeer(&directAttrs, direct, ed2k::PEER_SOURCE_KAD));
  auto directState = getEd2kPeerState(&directAttrs, direct.endpoint);
  REQUIRE(directState);
  ed2k::Endpoint directPeer = direct.endpoint;
  directPeer.cryptOptions = ed2k::SOURCE_CRYPT_SUPPORT;
  REQUIRE(markEd2kDirectCallbackAccepted(&directAttrs, directPeer, 130));
  REQUIRE_EQ(ed2k::LowIdCallbackState::ACCEPTED,
             directState->lowIdCallbackState);
  REQUIRE(!directState->lowId);
  REQUIRE(!directState->callbackRequested);
  REQUIRE_EQ(ed2k::CallbackKind::NONE, directState->callbackKind);
  REQUIRE_EQ(ed2k::PeerLifecycle::USEFUL,
             ed2k::classifyPeerLifecycle(*directState, 131));

  Ed2kAttribute failAttrs;
  REQUIRE(
      !addEd2kFoundSource(&failAttrs, source, ed2k::PEER_SOURCE_SERVER, true));
  state = getEd2kPeerState(&failAttrs, source.endpoint);
  REQUIRE(markEd2kCallbackFailed(&failAttrs, source.clientId, 180, 30));
  REQUIRE_EQ(ed2k::LowIdCallbackState::FAILED, state->lowIdCallbackState);
  REQUIRE(state->callbackImpossible);
  REQUIRE(state->dead);
  REQUIRE_EQ((int64_t)210, state->nextRetryTime);

  REQUIRE(expireEd2kCallbackWaits(&failAttrs, 211) == 1);
  REQUIRE_EQ(ed2k::LowIdCallbackState::IMPOSSIBLE, state->lowIdCallbackState);
  REQUIRE(!state->dead);
  REQUIRE(state->callbackImpossible);

  Ed2kAttribute timeoutAttrs;
  REQUIRE(!addEd2kFoundSource(&timeoutAttrs, source, ed2k::PEER_SOURCE_SERVER,
                              true));
  state = getEd2kPeerState(&timeoutAttrs, source.endpoint);
  REQUIRE(markEd2kCallbackRequestSent(&timeoutAttrs, source.clientId, 300, 45));
  REQUIRE(expireEd2kCallbackWaits(&timeoutAttrs, 345) == 1);
  REQUIRE_EQ(ed2k::LowIdCallbackState::TIMED_OUT, state->lowIdCallbackState);
  REQUIRE(state->callbackImpossible);
}

TEST_CASE("DownloadHelperTest.testEd2kPeerActionPolicySelectsConnect")
{
  std::vector<ed2k::PeerState> peers(2);
  peers[0].endpoint.host = "203.0.113.10";
  peers[0].endpoint.port = 4662;
  peers[0].sourceFlags = ed2k::PEER_SOURCE_EXCHANGE;
  peers[0].failCount = 1;
  peers[1].endpoint.host = "203.0.113.11";
  peers[1].endpoint.port = 4662;
  peers[1].sourceFlags = ed2k::PEER_SOURCE_SERVER;

  auto action = ed2k::selectPeerAction(peers, 100, 1);

  REQUIRE_EQ(ed2k::PeerActionType::CONNECT, action.type);
  REQUIRE(action.peer);
  REQUIRE_EQ(std::string("203.0.113.11"), action.peer->endpoint.host);
  action.peer->connecting = true;

  action = ed2k::selectPeerAction(peers, 100, 1);
  REQUIRE_EQ(ed2k::PeerActionType::WAIT, action.type);
  REQUIRE(!action.peer);
}

TEST_CASE("DownloadHelperTest.testEd2kPeerActionPolicyReportsQueuedReask")
{
  std::vector<ed2k::PeerState> peers(1);
  peers[0].endpoint.host = "203.0.113.10";
  peers[0].endpoint.port = 4662;
  peers[0].queued = true;
  peers[0].queueRank = 12;

  auto action = ed2k::selectPeerAction(peers, 100, 1);

  REQUIRE_EQ(ed2k::PeerActionType::REASK, action.type);
  REQUIRE(action.peer);
  REQUIRE_EQ((uint16_t)12, action.peer->queueRank);
}

TEST_CASE("DownloadHelperTest.testEd2kPeerActionPolicyHandlesCallbackAndExpiry")
{
  std::vector<ed2k::PeerState> peers(3);
  peers[0].endpoint.host = "203.0.113.10";
  peers[0].endpoint.port = 4662;
  peers[0].lowId = true;
  peers[0].callbackRequested = true;
  peers[0].lowIdCallbackState = ed2k::LowIdCallbackState::REQUESTED;
  peers[0].clientId = 42;
  peers[1].endpoint.host = "203.0.113.11";
  peers[1].endpoint.port = 4662;
  peers[1].dead = true;
  peers[1].nextRetryTime = 120;
  peers[2].endpoint.host = "203.0.113.12";
  peers[2].endpoint.port = 4662;
  peers[2].noFile = true;

  auto action = ed2k::selectPeerAction(peers, 100, 1);
  REQUIRE_EQ(ed2k::PeerActionType::REQUEST_CALLBACK, action.type);
  REQUIRE(action.peer);
  REQUIRE_EQ((uint32_t)42, action.peer->clientId);

  peers[0].callbackRequested = false;
  peers[0].callbackImpossible = true;
  peers[0].lowIdCallbackState = ed2k::LowIdCallbackState::IMPOSSIBLE;
  action = ed2k::selectPeerAction(peers, 130, 1);
  REQUIRE_EQ(ed2k::PeerActionType::RETRY, action.type);
  REQUIRE(action.peer);
  REQUIRE_EQ(std::string("203.0.113.11"), action.peer->endpoint.host);
}

TEST_CASE("DownloadHelperTest.testEd2kPeerActionPolicyIsolatesUnreachableLowId")
{
  std::vector<ed2k::PeerState> peers(2);
  peers[0].endpoint.host = "120.0.0.0";
  peers[0].endpoint.port = 4662;
  peers[0].lowId = true;
  peers[0].clientId = 0x00000120;
  peers[0].callbackImpossible = true;
  peers[0].lowIdCallbackState = ed2k::LowIdCallbackState::IMPOSSIBLE;
  peers[1].endpoint.host = "203.0.113.12";
  peers[1].endpoint.port = 4662;

  auto action = ed2k::selectPeerAction(peers, 100, 1);

  REQUIRE_EQ(ed2k::PeerActionType::CONNECT, action.type);
  REQUIRE(action.peer);
  REQUIRE_EQ(std::string("203.0.113.12"), action.peer->endpoint.host);
  REQUIRE(!ed2k::selectConnectPeer(peers, 100)->lowId);

  peers[1].connecting = true;
  action = ed2k::selectPeerAction(peers, 100, 1);
  REQUIRE_EQ(ed2k::PeerActionType::WAIT, action.type);
  REQUIRE(!action.peer);
}

TEST_CASE("DownloadHelperTest.testEd2kConnectPolicyIgnoresNonConnectActions")
{
  std::vector<ed2k::PeerState> peers(3);
  peers[0].endpoint.host = "203.0.113.10";
  peers[0].endpoint.port = 4662;
  peers[0].queued = true;
  peers[0].queueRank = 1;
  peers[1].endpoint.host = "203.0.113.11";
  peers[1].endpoint.port = 4662;
  peers[1].lowId = true;
  peers[1].callbackRequested = true;
  peers[2].endpoint.host = "203.0.113.12";
  peers[2].endpoint.port = 4662;
  peers[2].dead = true;
  peers[2].nextRetryTime = 100;

  auto selected = ed2k::selectConnectPeer(peers, 130, 1);

  REQUIRE(selected);
  REQUIRE_EQ(std::string("203.0.113.12"), selected->endpoint.host);
}

} // namespace aria2
