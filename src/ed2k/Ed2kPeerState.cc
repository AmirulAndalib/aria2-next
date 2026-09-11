/* <!-- copyright */
/*
 * aria2 - The high speed download utility
 *
 * Copyright (C) 2026 aria2-next contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
/* copyright --> */
#include "ed2k_link.h"
#include "ed2k_peer.h"
#include <cstddef>
#include <cstdint>
#include <vector>
#include "Ed2kAttribute.h"
#include "ed2k_policy.h"
#include <algorithm>

namespace aria2 {

ed2k::PeerState* findEd2kPeerStateByEndpointOrUdp(Ed2kAttribute* attrs,
                                                  const ed2k::Endpoint& peer)
{
  if (!attrs || peer.host.empty() || peer.port == 0) {
    return nullptr;
  }
  auto i = std::find_if(attrs->peerStates.begin(), attrs->peerStates.end(),
                        [&](const ed2k::PeerState& state) {
                          return state.endpoint.host == peer.host &&
                                 (state.endpoint.port == peer.port ||
                                  state.udpPort == peer.port);
                        });
  return i == attrs->peerStates.end() ? nullptr : &*i;
}

bool markEd2kPeerQueued(Ed2kAttribute* attrs, const ed2k::Endpoint& peer,
                        uint16_t rank, const std::vector<bool>& partStatus)
{
  auto state = getEd2kPeerState(attrs, peer);
  if (!state) {
    return false;
  }
  state->queued = true;
  state->dead = false;
  state->cancelled = false;
  state->noFile = false;
  state->outOfParts = false;
  state->nextPartStatusRecheckTime = 0;
  state->queueRank = rank;
  state->partStatus = partStatus;
  return true;
}

bool markEd2kPeerUdpReaskSent(Ed2kAttribute* attrs, const ed2k::Endpoint& peer,
                              int64_t now)
{
  auto state = getEd2kPeerState(attrs, peer);
  if (!state) {
    return false;
  }
  state->udpReaskPending = true;
  state->lastUdpReaskTime = now;
  state->nextUdpReaskTime = now + ed2k::PEER_UDP_REASK_INTERVAL;
  return true;
}

bool markEd2kPeerUdpReaskAck(Ed2kAttribute* attrs, const ed2k::Endpoint& peer,
                             uint16_t rank, const std::vector<bool>& partStatus,
                             int64_t now)
{
  auto state = findEd2kPeerStateByEndpointOrUdp(attrs, peer);
  if (!state) {
    return false;
  }
  state->queued = true;
  state->dead = false;
  state->noFile = false;
  state->cancelled = false;
  state->remoteQueueFull = false;
  state->outOfParts = false;
  state->nextPartStatusRecheckTime = 0;
  state->udpReaskPending = false;
  state->queueRank = rank;
  if (!partStatus.empty()) {
    state->partStatus = partStatus;
  }
  state->lastUdpReaskTime = now;
  state->nextUdpReaskTime = now + ed2k::PEER_UDP_REASK_INTERVAL;
  return true;
}

bool markEd2kPeerQueueFull(Ed2kAttribute* attrs, const ed2k::Endpoint& peer,
                           int64_t now, int64_t baseRetrySeconds)
{
  auto state = findEd2kPeerStateByEndpointOrUdp(attrs, peer);
  if (!state) {
    return false;
  }
  const auto endpoint = state->endpoint;
  if (!markEd2kPeerFailure(attrs, endpoint, now, baseRetrySeconds)) {
    return false;
  }
  state = getEd2kPeerState(attrs, endpoint);
  state->queued = true;
  state->dead = true;
  state->noFile = false;
  state->remoteQueueFull = true;
  state->udpReaskPending = false;
  state->queueRank = 0;
  state->nextUdpReaskTime = state->nextRetryTime;
  return true;
}

bool markEd2kPeerConnecting(Ed2kAttribute* attrs, const ed2k::Endpoint& peer)
{
  auto state = getEd2kPeerState(attrs, peer);
  if (!state) {
    return false;
  }
  state->connecting = true;
  return true;
}

bool markEd2kPeerDisconnected(Ed2kAttribute* attrs, const ed2k::Endpoint& peer)
{
  auto state = getEd2kPeerState(attrs, peer);
  if (!state) {
    return false;
  }
  state->connecting = false;
  state->accepted = false;
  releaseEd2kRequestedRanges(attrs, state->requestedParts);
  state->requestedParts.clear();
  return true;
}

bool updateEd2kPeerPartStatus(Ed2kAttribute* attrs, const ed2k::Endpoint& peer,
                              const std::vector<bool>& partStatus)
{
  auto state = getEd2kPeerState(attrs, peer);
  if (!state) {
    return false;
  }
  state->partStatus = partStatus;
  return true;
}

bool markEd2kPeerAccepted(Ed2kAttribute* attrs, const ed2k::Endpoint& peer)
{
  auto state = getEd2kPeerState(attrs, peer);
  if (!state) {
    return false;
  }
  state->connecting = false;
  state->accepted = true;
  state->queued = false;
  state->dead = false;
  if (state->lowIdCallbackState == ed2k::LowIdCallbackState::ACCEPTED) {
    state->lowIdCallbackState = ed2k::LowIdCallbackState::COMPLETED;
  }
  state->udpReaskPending = false;
  state->remoteQueueFull = false;
  state->outOfParts = false;
  state->cancelTransferSent = false;
  return true;
}

bool markEd2kPeerOutOfParts(Ed2kAttribute* attrs, const ed2k::Endpoint& peer,
                            int64_t now)
{
  auto state = getEd2kPeerState(attrs, peer);
  if (!state) {
    return false;
  }
  state->outOfParts = true;
  state->nextPartStatusRecheckTime = now + ed2k::PEER_UDP_REASK_INTERVAL;
  state->nextUdpReaskTime = state->nextPartStatusRecheckTime;
  state->connecting = false;
  state->accepted = false;
  state->queued = true;
  releaseEd2kRequestedRanges(attrs, state->requestedParts);
  state->requestedParts.clear();
  return true;
}

bool markEd2kPeerCancelled(Ed2kAttribute* attrs, const ed2k::Endpoint& peer)
{
  auto state = getEd2kPeerState(attrs, peer);
  if (!state) {
    return false;
  }
  state->cancelled = true;
  state->connecting = false;
  state->accepted = false;
  state->queued = false;
  releaseEd2kRequestedRanges(attrs, state->requestedParts);
  state->requestedParts.clear();
  return true;
}

bool markEd2kPeerFailure(Ed2kAttribute* attrs, const ed2k::Endpoint& peer,
                         int64_t now, int64_t baseRetrySeconds)
{
  auto state = getEd2kPeerState(attrs, peer);
  if (!state) {
    return false;
  }
  state->queued = false;
  state->connecting = false;
  state->dead = true;
  state->accepted = false;
  state->udpReaskPending = false;
  releaseEd2kRequestedRanges(attrs, state->requestedParts);
  state->requestedParts.clear();
  ++state->failCount;
  state->lastFailureTime = now;
  const auto multiplier = std::min<uint32_t>(state->failCount, 6);
  state->nextRetryTime = now + baseRetrySeconds * multiplier;
  return true;
}

bool markEd2kPeerDead(Ed2kAttribute* attrs, const ed2k::Endpoint& peer,
                      int64_t now, int64_t baseRetrySeconds)
{
  if (!markEd2kPeerFailure(attrs, peer, now, baseRetrySeconds)) {
    return false;
  }
  auto state = getEd2kPeerState(attrs, peer);
  state->noFile = true;
  return true;
}

size_t expireEd2kDeadSources(Ed2kAttribute* attrs, int64_t now)
{
  if (!attrs) {
    return 0;
  }
  size_t expired = 0;
  for (auto& state : attrs->peerStates) {
    if (state.outOfParts && state.nextPartStatusRecheckTime != 0 &&
        state.nextPartStatusRecheckTime <= now) {
      state.outOfParts = false;
      state.nextPartStatusRecheckTime = 0;
      ++expired;
    }
    if (!state.dead || state.nextRetryTime == 0 || state.nextRetryTime > now) {
      continue;
    }
    state.dead = false;
    state.noFile = false;
    state.cancelled = false;
    state.remoteQueueFull = false;
    state.udpReaskPending = false;
    state.nextRetryTime = 0;
    releaseEd2kRequestedRanges(attrs, state.requestedParts);
    state.requestedParts.clear();
    ++expired;
  }
  return expired;
}

size_t expireEd2kPeerUdpReasks(Ed2kAttribute* attrs, int64_t now,
                               int64_t timeoutSeconds)
{
  if (!attrs || timeoutSeconds <= 0) {
    return 0;
  }
  size_t expired = 0;
  for (auto& state : attrs->peerStates) {
    if (!state.udpReaskPending || state.lastUdpReaskTime == 0 ||
        now - state.lastUdpReaskTime < timeoutSeconds) {
      continue;
    }
    state.udpReaskPending = false;
    state.queued = false;
    state.queueRank = 0;
    state.nextUdpReaskTime = 0;
    ++expired;
  }
  return expired;
}

size_t promoteEd2kTcpReasks(Ed2kAttribute* attrs, int64_t now)
{
  if (!attrs) {
    return 0;
  }
  size_t promoted = 0;
  for (auto& state : attrs->peerStates) {
    if (!state.queued || state.connecting || state.accepted || state.dead ||
        state.noFile || state.cancelled || state.outOfParts ||
        state.udpReaskPending ||
        (state.nextUdpReaskTime != 0 && state.nextUdpReaskTime > now) ||
        (state.udpPort != 0 && state.udpVersion != 0)) {
      continue;
    }
    state.queued = false;
    state.queueRank = 0;
    ++promoted;
  }
  return promoted;
}

ed2k::PeerState* selectDueEd2kUdpReaskPeer(Ed2kAttribute* attrs, int64_t now)
{
  if (!attrs) {
    return nullptr;
  }
  ed2k::PeerState* selected = nullptr;
  for (auto& state : attrs->peerStates) {
    if (!state.queued || state.dead || state.noFile || state.cancelled ||
        state.outOfParts || state.accepted || state.connecting ||
        state.udpReaskPending || state.udpPort == 0 || state.udpVersion == 0) {
      continue;
    }
    if (state.nextUdpReaskTime != 0 && state.nextUdpReaskTime > now) {
      continue;
    }
    if (!selected ||
        ed2k::sourcePriority(state.sourceFlags) >
            ed2k::sourcePriority(selected->sourceFlags) ||
        (ed2k::sourcePriority(state.sourceFlags) ==
             ed2k::sourcePriority(selected->sourceFlags) &&
         state.queueRank != 0 &&
         (selected->queueRank == 0 || state.queueRank < selected->queueRank))) {
      selected = &state;
    }
  }
  return selected;
}

} // namespace aria2
