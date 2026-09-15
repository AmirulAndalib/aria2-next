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
#include "ed2k_hash.h"
#include "ed2k_link.h"
#include "ed2k_peer.h"
#include <cstddef>
#include <cstdint>
#include "Ed2kAttribute.h"
#include <algorithm>

namespace aria2 {

bool markEd2kCallbackRequestSent(Ed2kAttribute* attrs, uint32_t clientId,
                                 int64_t now, int64_t timeoutSeconds)
{
  if (!attrs || clientId == 0 || timeoutSeconds <= 0) {
    return false;
  }
  auto i = std::find_if(attrs->peerStates.begin(), attrs->peerStates.end(),
                        [&](const ed2k::PeerState& state) {
                          return state.lowId && state.clientId == clientId;
                        });
  if (i == attrs->peerStates.end()) {
    return false;
  }
  i->callbackRequested = true;
  i->callbackImpossible = false;
  i->lowIdCallbackState = ed2k::LowIdCallbackState::REQUESTED;
  i->lastCallbackTime = now;
  i->callbackDeadline = now + timeoutSeconds;
  i->connecting = false;
  i->accepted = false;
  i->queued = false;
  releaseEd2kRequestedRanges(attrs, i->requestedParts);
  i->requestedParts.clear();
  return true;
}

bool markEd2kCallbackAccepted(Ed2kAttribute* attrs, uint32_t clientId,
                              const ed2k::Endpoint& peer, int64_t now)
{
  if (!attrs || clientId == 0 || peer.host.empty() || peer.port == 0) {
    return false;
  }
  auto state = getEd2kPeerState(attrs, peer);
  if (!state) {
    return false;
  }
  state->clientId = clientId;
  state->lowId = false;
  state->callbackRequested = false;
  state->callbackImpossible = false;
  state->callbackKind = ed2k::CallbackKind::NONE;
  state->lowIdCallbackState = ed2k::LowIdCallbackState::ACCEPTED;
  state->lastCallbackTime = now;
  state->callbackDeadline = 0;
  state->connecting = false;
  state->accepted = false;
  state->queued = false;
  state->dead = false;
  state->cancelled = false;
  state->noFile = false;
  releaseEd2kRequestedRanges(attrs, state->requestedParts);
  state->requestedParts.clear();
  for (auto& existing : attrs->peerStates) {
    if (&existing == state || existing.clientId != clientId ||
        !existing.lowId) {
      continue;
    }
    existing.callbackRequested = false;
    existing.callbackImpossible = false;
    existing.callbackKind = ed2k::CallbackKind::NONE;
    existing.lowIdCallbackState = ed2k::LowIdCallbackState::ACCEPTED;
    existing.lastCallbackTime = now;
    existing.callbackDeadline = 0;
    existing.connecting = false;
    existing.accepted = false;
    existing.queued = false;
    releaseEd2kRequestedRanges(attrs, existing.requestedParts);
    existing.requestedParts.clear();
  }
  return true;
}

bool markEd2kDirectCallbackAccepted(Ed2kAttribute* attrs,
                                    const ed2k::Endpoint& peer, int64_t now)
{
  if (!attrs || peer.host.empty() || peer.port == 0 ||
      peer.userHash.size() != ed2k::HASH_LENGTH) {
    return false;
  }
  auto i =
      std::find_if(attrs->peerStates.begin(), attrs->peerStates.end(),
                   [&](const ed2k::PeerState& state) {
                     return state.callbackKind == ed2k::CallbackKind::DIRECT &&
                            state.endpoint.host == peer.host &&
                            state.endpoint.port == peer.port &&
                            state.endpoint.userHash == peer.userHash;
                   });
  if (i == attrs->peerStates.end()) {
    return false;
  }
  i->lowId = false;
  i->callbackRequested = false;
  i->callbackImpossible = false;
  i->callbackKind = ed2k::CallbackKind::NONE;
  i->lowIdCallbackState = ed2k::LowIdCallbackState::ACCEPTED;
  i->lastCallbackTime = now;
  i->callbackDeadline = 0;
  i->connecting = false;
  i->accepted = false;
  i->queued = false;
  i->dead = false;
  i->cancelled = false;
  i->noFile = false;
  if (peer.cryptOptions != 0 && i->endpoint.cryptOptions == 0) {
    i->endpoint.cryptOptions = peer.cryptOptions;
  }
  releaseEd2kRequestedRanges(attrs, i->requestedParts);
  i->requestedParts.clear();
  return true;
}

bool markEd2kCallbackCompleted(Ed2kAttribute* attrs, const ed2k::Endpoint& peer)
{
  auto state = getEd2kPeerState(attrs, peer);
  if (!state) {
    return false;
  }
  state->lowIdCallbackState = ed2k::LowIdCallbackState::COMPLETED;
  state->callbackRequested = false;
  state->callbackImpossible = false;
  state->callbackKind = ed2k::CallbackKind::NONE;
  state->callbackDeadline = 0;
  return true;
}

bool markEd2kCallbackFailed(Ed2kAttribute* attrs, uint32_t clientId)
{
  return markEd2kCallbackFailed(attrs, clientId, 0, 0);
}

bool markEd2kCallbackFailed(Ed2kAttribute* attrs, uint32_t clientId,
                            int64_t now, int64_t baseRetrySeconds)
{
  if (!attrs || clientId == 0) {
    return false;
  }
  auto i = std::find_if(attrs->peerStates.begin(), attrs->peerStates.end(),
                        [&](const ed2k::PeerState& state) {
                          return state.lowId && state.clientId == clientId;
                        });
  if (i == attrs->peerStates.end()) {
    return false;
  }
  i->callbackRequested = false;
  i->callbackImpossible = true;
  i->callbackKind = ed2k::CallbackKind::NONE;
  i->lowIdCallbackState = ed2k::LowIdCallbackState::FAILED;
  i->lastCallbackTime = now;
  i->callbackDeadline = 0;
  i->connecting = false;
  i->accepted = false;
  i->queued = false;
  releaseEd2kRequestedRanges(attrs, i->requestedParts);
  i->requestedParts.clear();
  if (baseRetrySeconds > 0) {
    i->dead = true;
    ++i->failCount;
    i->lastFailureTime = now;
    const auto multiplier = std::min<uint32_t>(i->failCount, 6);
    i->nextRetryTime = now + baseRetrySeconds * multiplier;
  }
  return true;
}

size_t expireEd2kCallbackWaits(Ed2kAttribute* attrs, int64_t now)
{
  if (!attrs) {
    return 0;
  }
  size_t expired = 0;
  for (auto& state : attrs->peerStates) {
    const bool retryExpired =
        state.dead && state.nextRetryTime != 0 && state.nextRetryTime <= now &&
        (state.lowIdCallbackState == ed2k::LowIdCallbackState::FAILED ||
         state.lowIdCallbackState == ed2k::LowIdCallbackState::TIMED_OUT);
    const bool deadlineExpired =
        state.lowIdCallbackState == ed2k::LowIdCallbackState::REQUESTED &&
        state.callbackDeadline != 0 && state.callbackDeadline <= now;
    if (!retryExpired && !deadlineExpired) {
      continue;
    }
    state.callbackRequested = false;
    state.callbackImpossible = true;
    state.callbackKind = ed2k::CallbackKind::NONE;
    state.lowIdCallbackState = deadlineExpired
                                   ? ed2k::LowIdCallbackState::TIMED_OUT
                                   : ed2k::LowIdCallbackState::IMPOSSIBLE;
    state.dead = false;
    state.connecting = false;
    state.accepted = false;
    state.queued = false;
    releaseEd2kRequestedRanges(attrs, state.requestedParts);
    state.requestedParts.clear();
    state.nextRetryTime = 0;
    ++expired;
  }
  return expired;
}

} // namespace aria2
