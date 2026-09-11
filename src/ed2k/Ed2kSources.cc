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
#include "ed2k_kad.h"
#include "ed2k_kad_search.h"
#include "ed2k_link.h"
#include "ed2k_peer.h"
#include "ed2k_server.h"
#include <cstddef>
#include <cstdint>
#include <vector>
#include "Ed2kAttribute.h"
#include "ed2k/Ed2kStateInternal.h"
#include "support/Network.h"
#include "ed2k_hash.h"
#include "ed2k_endpoint.h"
#include <algorithm>

namespace aria2 {

using ed2k_state::sameEndpoint;

bool addUniqueEndpoint(std::vector<ed2k::Endpoint>& endpoints,
                       const ed2k::Endpoint& endpoint)
{
  if (endpoint.host.empty() || endpoint.port == 0) {
    return false;
  }
  auto i = std::find_if(
      endpoints.begin(), endpoints.end(), [&](const ed2k::Endpoint& item) {
        return item.host == endpoint.host && item.port == endpoint.port;
      });
  if (i != endpoints.end()) {
    return false;
  }
  endpoints.push_back(endpoint);
  return true;
}

bool invalidPeerEndpoint(const Ed2kAttribute* attrs, const ed2k::Endpoint& peer)
{
  if (!attrs || peer.host.empty() || peer.port == 0 ||
      (!peer.userHash.empty() && peer.userHash.size() != ed2k::HASH_LENGTH) ||
      (!attrs->clientHash.empty() && peer.userHash == attrs->clientHash)) {
    return true;
  }
  if (!util::isNumericHost(peer.host)) {
    return false;
  }
  return peer.host == "0.0.0.0" || peer.host == "255.255.255.255" ||
         peer.host == "::" || peer.host == "::1" ||
         peer.host.compare(0, 4, "127.") == 0;
}

bool canReceiveEd2kDirectCallback(const Ed2kAttribute* attrs,
                                  const ed2k::Endpoint& peer)
{
  if (!attrs || (peer.cryptOptions & ed2k::SOURCE_CRYPT_DIRECT_CALLBACK) == 0) {
    return false;
  }
  if (!attrs->kadFirewalled) {
    return true;
  }
  return std::any_of(attrs->serverStates.begin(), attrs->serverStates.end(),
                     [](const ed2k::ServerState& state) {
                       return state.connected && state.handshakeCompleted &&
                              state.highId;
                     });
}

bool isFilteredSourceExchangePeer(const ed2k::Endpoint& peer,
                                  const ed2k::Endpoint& remotePeer)
{
  if (peer.host.empty() || peer.port == 0) {
    return true;
  }
  if (sameEndpoint(peer, remotePeer)) {
    return true;
  }
  if (util::isNumericHost(peer.host) &&
      (peer.host == "0.0.0.0" || peer.host == "127.0.0.1" ||
       peer.host == "::" || peer.host == "::1")) {
    return true;
  }
  return false;
}

bool addEd2kPeer(Ed2kAttribute* attrs, const ed2k::Endpoint& peer)
{
  return addEd2kPeer(attrs, peer, 0);
}

bool addEd2kPeer(Ed2kAttribute* attrs, const ed2k::Endpoint& peer,
                 uint32_t sourceFlag)
{
  if (invalidPeerEndpoint(attrs, peer)) {
    return false;
  }
  if (!peer.userHash.empty()) {
    auto identity =
        std::find_if(attrs->peerStates.begin(), attrs->peerStates.end(),
                     [&](const ed2k::PeerState& state) {
                       return state.endpoint.userHash == peer.userHash;
                     });
    if (identity != attrs->peerStates.end()) {
      identity->sourceFlags |= sourceFlag;
      identity->endpoint.cryptOptions |= peer.cryptOptions;
      if (!identity->connecting && !identity->accepted &&
          !sameEndpoint(identity->endpoint, peer)) {
        auto endpoint =
            std::find_if(attrs->peers.begin(), attrs->peers.end(),
                         [&](const ed2k::Endpoint& item) {
                           return sameEndpoint(item, identity->endpoint);
                         });
        if (endpoint != attrs->peers.end()) {
          *endpoint = peer;
        }
        identity->endpoint = peer;
      }
      return false;
    }
  }
  if (!addUniqueEndpoint(attrs->peers, peer)) {
    auto state = getEd2kPeerState(attrs, peer);
    if (state) {
      if (!peer.userHash.empty() && state->endpoint.userHash.empty()) {
        state->endpoint.userHash = peer.userHash;
      }
      if (peer.cryptOptions != 0 && state->endpoint.cryptOptions == 0) {
        state->endpoint.cryptOptions = peer.cryptOptions;
      }
      state->sourceFlags |= sourceFlag;
    }
    return false;
  }
  auto state = getEd2kPeerState(attrs, peer);
  if (state) {
    state->sourceFlags |= sourceFlag;
  }
  return true;
}

bool addEd2kKadSourcePeer(Ed2kAttribute* attrs,
                          const ed2k::KadSourceEndpoint& source,
                          uint32_t sourceFlag)
{
  if (invalidPeerEndpoint(attrs, source.endpoint)) {
    return false;
  }
  if (source.sourceType != 0 && source.sourceType != 1 &&
      source.sourceType != 4 && source.sourceType != 3 &&
      source.sourceType != 5 && source.sourceType != 6) {
    return false;
  }
  const bool buddyCallback = source.sourceType == 3 || source.sourceType == 5;
  const bool directCallback = source.sourceType == 6;
  if (directCallback && !canReceiveEd2kDirectCallback(attrs, source.endpoint)) {
    return false;
  }
  const bool callbackOnly = buddyCallback || directCallback;
  const uint32_t lowIdClientId = callbackOnly ? 1 : 0;
  const auto added = addEd2kPeer(attrs, source.endpoint, sourceFlag);
  auto state = getEd2kPeerState(attrs, source.endpoint);
  if (state) {
    state->sourceFlags |= sourceFlag;
    if (source.udpPort != 0) {
      state->udpPort = source.udpPort;
      if (state->udpVersion == 0) {
        state->udpVersion = 4;
      }
    }
    if (callbackOnly) {
      state->clientId = lowIdClientId;
      state->lowId = true;
      state->callbackRequested = true;
      state->callbackImpossible = false;
      state->lowIdCallbackState = ed2k::LowIdCallbackState::REQUESTED;
      state->callbackDeadline = 0;
      state->callbackKind = directCallback ? ed2k::CallbackKind::DIRECT
                                           : ed2k::CallbackKind::BUDDY;
      if (buddyCallback) {
        state->callbackBuddy.host = ed2k::ipv4FromEndpoint(source.buddyIp);
        state->callbackBuddy.port = source.buddyPort;
        state->callbackBuddyId = source.buddyId.empty()
                                     ? ed2k::ed2kHashToKadId(source.buddyHash)
                                     : source.buddyId;
      }
      else {
        state->callbackBuddy = ed2k::Endpoint();
        state->callbackBuddyId.clear();
      }
    }
  }
  return added;
}

bool addEd2kFoundSource(Ed2kAttribute* attrs, const ed2k::FoundSource& source,
                        uint32_t sourceFlag, bool callbackRequested)
{
  if (invalidPeerEndpoint(attrs, source.endpoint)) {
    return false;
  }
  if (!source.lowId) {
    return addEd2kPeer(attrs, source.endpoint, sourceFlag);
  }
  auto state = getEd2kPeerState(attrs, source.endpoint);
  if (!state) {
    return false;
  }
  if (!source.endpoint.userHash.empty() && state->endpoint.userHash.empty()) {
    state->endpoint.userHash = source.endpoint.userHash;
  }
  if (source.endpoint.cryptOptions != 0 && state->endpoint.cryptOptions == 0) {
    state->endpoint.cryptOptions = source.endpoint.cryptOptions;
  }
  state->sourceFlags |= sourceFlag;
  state->clientId = source.clientId;
  state->lowId = true;
  if (callbackRequested) {
    state->callbackRequested = true;
    state->callbackImpossible = false;
    state->lowIdCallbackState = ed2k::LowIdCallbackState::REQUESTED;
    state->callbackKind = ed2k::CallbackKind::BUDDY;
    state->lastCallbackTime = 0;
    state->callbackDeadline = 0;
  }
  else if (!state->callbackRequested) {
    state->callbackImpossible = true;
    state->lowIdCallbackState = ed2k::LowIdCallbackState::IMPOSSIBLE;
  }
  state->connecting = false;
  state->accepted = false;
  state->queued = false;
  releaseEd2kRequestedRanges(attrs, state->requestedParts);
  state->requestedParts.clear();
  return false;
}

size_t mergeEd2kServerSources(Ed2kAttribute* attrs,
                              const std::vector<ed2k::FoundSource>& sources,
                              uint32_t sourceFlag)
{
  if (!attrs) {
    return 0;
  }
  size_t added = 0;
  for (const auto& source : sources) {
    if (source.lowId) {
      addEd2kFoundSource(attrs, source, sourceFlag, false);
      continue;
    }
    if (addEd2kFoundSource(attrs, source, sourceFlag, false)) {
      ++added;
    }
  }
  return added;
}

size_t mergeEd2kSourceExchangePeers(
    Ed2kAttribute* attrs, const std::vector<ed2k::SourceExchangeEntry>& entries,
    const ed2k::Endpoint& remotePeer)
{
  if (!attrs) {
    return 0;
  }
  size_t added = 0;
  for (const auto& entry : entries) {
    auto peer = entry.endpoint;
    if (!entry.userHash.empty()) {
      peer.userHash = entry.userHash;
    }
    if (entry.cryptOptions != 0) {
      peer.cryptOptions = entry.cryptOptions;
    }
    if (isFilteredSourceExchangePeer(peer, remotePeer)) {
      continue;
    }
    if (addEd2kPeer(attrs, peer, ed2k::PEER_SOURCE_EXCHANGE)) {
      ++added;
    }
  }
  return added;
}

ed2k::PeerState* getEd2kPeerState(Ed2kAttribute* attrs,
                                  const ed2k::Endpoint& peer)
{
  if (!attrs || peer.host.empty() || peer.port == 0) {
    return nullptr;
  }
  auto i = std::find_if(attrs->peerStates.begin(), attrs->peerStates.end(),
                        [&](const ed2k::PeerState& state) {
                          return state.endpoint.host == peer.host &&
                                 state.endpoint.port == peer.port;
                        });
  if (i != attrs->peerStates.end()) {
    return &*i;
  }
  ed2k::PeerState state;
  state.endpoint = peer;
  attrs->peerStates.push_back(state);
  return &attrs->peerStates.back();
}

} // namespace aria2
