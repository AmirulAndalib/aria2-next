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
#include "ed2k_link.h"
#include "ed2k_peer.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include "Ed2kKadCommand.h"
#include "ed2k_constants.h"
#include "ed2k/KadCommandSupport.h"
#include "DownloadContext.h"
#include "Ed2kAttribute.h"
#include "Log.h"
#include "RequestGroup.h"
#include "ed2k_hash.h"
#include "fmt.h"

namespace aria2 {

using namespace kad_command;

size_t Ed2kKadCommand::queueDuePeerReasks(int64_t now)
{
  auto attrs = getEd2kAttrs(requestGroup_->getDownloadContext());
  size_t queued = 0;
  while (auto peer = selectDueEd2kUdpReaskPeer(attrs, now)) {
    ed2k::Endpoint endpoint = peer->endpoint;
    endpoint.port = peer->udpPort;
    std::string payload;
    if (peer->udpVersion > 3) {
      payload = ed2k::createUdpReaskFilePingPayload(
          attrs->link.hash, localPartStatus(requestGroup_), 0);
    }
    else if (peer->udpVersion > 2) {
      payload = ed2k::createUdpReaskFilePingPayload(attrs->link.hash, 0);
    }
    else {
      payload = attrs->link.hash;
    }
    queueEmuleUdpPacket(endpoint, ed2k::OP_REASKFILEPING, payload);
    markEd2kPeerUdpReaskSent(attrs, peer->endpoint, now);
    ++queued;
  }
  return queued;
}

size_t Ed2kKadCommand::queueDueKadCallbacks(int64_t now)
{
  auto attrs = getEd2kAttrs(requestGroup_->getDownloadContext());
  const auto tcpPort = localEd2kTcpPort(e_);
  if (!attrs || tcpPort == 0 || attrs->link.hash.empty()) {
    return 0;
  }
  constexpr int64_t CALLBACK_TIMEOUT = 45;
  size_t queued = 0;
  for (auto& state : attrs->peerStates) {
    if (!state.lowId || !state.callbackRequested ||
        state.lowIdCallbackState != ed2k::LowIdCallbackState::REQUESTED ||
        state.lastCallbackTime != 0) {
      continue;
    }
    if (state.callbackKind == ed2k::CallbackKind::DIRECT) {
      if (state.endpoint.host.empty() || state.udpPort == 0 ||
          state.endpoint.userHash.size() != ed2k::HASH_LENGTH) {
        continue;
      }
      ed2k::Endpoint endpoint;
      endpoint.host = state.endpoint.host;
      endpoint.port = state.udpPort;
      queueEmuleUdpPacket(
          endpoint, ed2k::OP_DIRECTCALLBACKREQ,
          ed2k::createDirectCallbackRequestPayload(
              tcpPort, attrs->clientHash, localDirectCallbackOptions()));
      state.lastCallbackTime = now;
      state.callbackDeadline = now + CALLBACK_TIMEOUT;
      ++queued;
      A2_LOG_TRACE(fmt("Queued ED2K direct UDP callback request to %s:%u "
                       "for source TCP port %u.",
                       endpoint.host.c_str(), endpoint.port,
                       state.endpoint.port));
      continue;
    }
    if (state.callbackKind != ed2k::CallbackKind::BUDDY ||
        state.callbackBuddy.host.empty() || state.callbackBuddy.port == 0 ||
        state.callbackBuddyId.size() != ed2k::HASH_LENGTH) {
      continue;
    }
    queuePacket(state.callbackBuddy, ed2k::KAD_CALLBACK_REQ,
                ed2k::createKadCallbackRequestPayload(
                    state.callbackBuddyId,
                    ed2k::ed2kHashToKadId(attrs->link.hash), tcpPort));
    state.lastCallbackTime = now;
    state.callbackDeadline = now + CALLBACK_TIMEOUT;
    ++queued;
    A2_LOG_TRACE(fmt("Queued ED2K Kad callback request to buddy %s:%u "
                     "for source %s:%u.",
                     state.callbackBuddy.host.c_str(), state.callbackBuddy.port,
                     state.endpoint.host.c_str(), state.endpoint.port));
  }
  return queued;
}

} // namespace aria2
