/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
#include <cstdint>
#include "download_helper.h"

#include <string>
#include <cstdlib>
#include <vector>

#include "a2doctest.h"

#include "Ed2kAttribute.h"
#include "Ed2kKadCommand.h"
#include "ed2k_endpoint.h"
#include "ed2k_link.h"
#include "ed2k_peer.h"
#include "ed2k_policy.h"
#include "TestUtil.h"

namespace aria2 {

TEST_CASE("DownloadHelperTest.testEd2kPeerUdpReaskStateTransitions")
{
  Ed2kAttribute attrs;
  ed2k::Endpoint peer;
  peer.host = "203.0.113.10";
  peer.port = 4662;
  addEd2kPeer(&attrs, peer, ed2k::PEER_SOURCE_SERVER);
  REQUIRE(markEd2kPeerQueued(&attrs, peer, 7, std::vector<bool>{true, false}));
  auto state = getEd2kPeerState(&attrs, peer);
  state->udpPort = 4672;
  state->udpVersion = 4;

  REQUIRE(markEd2kPeerUdpReaskSent(&attrs, peer, 100));
  REQUIRE(state->udpReaskPending);
  REQUIRE_EQ((int64_t)100, state->lastUdpReaskTime);
  REQUIRE_EQ((int64_t)1400, state->nextUdpReaskTime);

  REQUIRE(markEd2kPeerUdpReaskAck(&attrs, peer, 3,
                                  std::vector<bool>{false, true}, 120));
  REQUIRE(!state->udpReaskPending);
  REQUIRE(!state->remoteQueueFull);
  REQUIRE_EQ((uint16_t)3, state->queueRank);
  REQUIRE_EQ((int64_t)120, state->lastUdpReaskTime);
  REQUIRE_EQ((int64_t)1420, state->nextUdpReaskTime);
  REQUIRE_EQ((size_t)2, state->partStatus.size());
  REQUIRE(!state->partStatus[0]);
  REQUIRE(state->partStatus[1]);

  REQUIRE(markEd2kPeerQueueFull(&attrs, peer, 200, 30));
  REQUIRE(!state->udpReaskPending);
  REQUIRE(state->remoteQueueFull);
  REQUIRE(state->dead);
  REQUIRE(!state->noFile);
  REQUIRE_EQ((int64_t)230, state->nextRetryTime);
}

TEST_CASE("DownloadHelperTest.testEd2kPeerUdpReaskTimeoutFallsBackToTcp")
{
  Ed2kAttribute attrs;
  ed2k::Endpoint peer;
  peer.host = "203.0.113.10";
  peer.port = 4662;
  addEd2kPeer(&attrs, peer, ed2k::PEER_SOURCE_SERVER);
  markEd2kPeerQueued(&attrs, peer, 7, std::vector<bool>{true});
  auto state = getEd2kPeerState(&attrs, peer);
  state->udpPort = 4672;
  state->udpVersion = 4;
  REQUIRE(markEd2kPeerUdpReaskSent(&attrs, peer, 100));

  REQUIRE_EQ((size_t)0, expireEd2kPeerUdpReasks(&attrs, 129, 30));
  REQUIRE(state->udpReaskPending);
  REQUIRE_EQ((size_t)1, expireEd2kPeerUdpReasks(&attrs, 130, 30));
  REQUIRE(!state->udpReaskPending);
  REQUIRE(!state->queued);
  REQUIRE_EQ(state, ed2k::selectConnectPeer(attrs.peerStates, 130));

  markEd2kPeerQueued(&attrs, peer, 4, std::vector<bool>{true});
  state->udpPort = 0;
  state->udpVersion = 0;
  state->nextUdpReaskTime = 200;
  REQUIRE_EQ((size_t)0, promoteEd2kTcpReasks(&attrs, 199));
  REQUIRE_EQ((size_t)1, promoteEd2kTcpReasks(&attrs, 200));
  REQUIRE(!state->queued);
}

TEST_CASE("DownloadHelperTest.testEd2kKadFirewallCheckAckRequiresExpectedHost")
{
  Ed2kAttribute attrs;
  attrs.kadFirewallCheckHosts = {"203.0.113.10", "203.0.113.11"};

  REQUIRE(!consumeEd2kKadFirewallCheckHost(&attrs, "203.0.113.12"));
  REQUIRE_EQ((size_t)2, attrs.kadFirewallCheckHosts.size());
  REQUIRE(consumeEd2kKadFirewallCheckHost(&attrs, "203.0.113.11"));
  REQUIRE(attrs.kadFirewallCheckHosts.empty());
  REQUIRE(!consumeEd2kKadFirewallCheckHost(&attrs, "203.0.113.11"));
}

TEST_CASE("DownloadHelperTest.testEd2kPeerUdpReaskReplyMatchesUdpPort")
{
  Ed2kAttribute attrs;
  ed2k::Endpoint peer;
  peer.host = "203.0.113.10";
  peer.port = 4662;
  addEd2kPeer(&attrs, peer, ed2k::PEER_SOURCE_SERVER);
  markEd2kPeerQueued(&attrs, peer, 7, std::vector<bool>{true});
  auto state = getEd2kPeerState(&attrs, peer);
  state->udpPort = 4672;
  state->udpVersion = 4;
  state->udpReaskPending = true;

  ed2k::Endpoint udpPeer = peer;
  udpPeer.port = 4672;

  REQUIRE(markEd2kPeerUdpReaskAck(&attrs, udpPeer, 5, std::vector<bool>{false},
                                  100));
  REQUIRE_EQ((size_t)1, attrs.peerStates.size());
  REQUIRE(!state->udpReaskPending);
  REQUIRE_EQ((uint16_t)5, state->queueRank);

  REQUIRE(markEd2kPeerQueueFull(&attrs, udpPeer, 200, 30));
  REQUIRE_EQ((size_t)1, attrs.peerStates.size());
  REQUIRE(state->remoteQueueFull);
  REQUIRE(state->dead);
}

TEST_CASE("DownloadHelperTest.testEd2kPeerUdpReaskDueSelection")
{
  Ed2kAttribute attrs;
  ed2k::Endpoint waiting;
  waiting.host = "203.0.113.10";
  waiting.port = 4662;
  addEd2kPeer(&attrs, waiting, ed2k::PEER_SOURCE_SERVER);
  markEd2kPeerQueued(&attrs, waiting, 8, std::vector<bool>{true});
  auto waitingState = getEd2kPeerState(&attrs, waiting);
  waitingState->udpPort = 4672;
  waitingState->udpVersion = 4;
  waitingState->nextUdpReaskTime = 500;

  ed2k::Endpoint due;
  due.host = "203.0.113.11";
  due.port = 4662;
  addEd2kPeer(&attrs, due, ed2k::PEER_SOURCE_KAD);
  markEd2kPeerQueued(&attrs, due, 2, std::vector<bool>{true});
  auto dueState = getEd2kPeerState(&attrs, due);
  dueState->udpPort = 4672;
  dueState->udpVersion = 4;
  dueState->nextUdpReaskTime = 300;

  ed2k::Endpoint pending;
  pending.host = "203.0.113.12";
  pending.port = 4662;
  addEd2kPeer(&attrs, pending, ed2k::PEER_SOURCE_EXCHANGE);
  markEd2kPeerQueued(&attrs, pending, 1, std::vector<bool>{true});
  auto pendingState = getEd2kPeerState(&attrs, pending);
  pendingState->udpPort = 4672;
  pendingState->udpVersion = 4;
  pendingState->udpReaskPending = true;

  auto selected = selectDueEd2kUdpReaskPeer(&attrs, 400);

  REQUIRE(selected);
  REQUIRE_EQ(std::string("203.0.113.11"), selected->endpoint.host);
}

} // namespace aria2
