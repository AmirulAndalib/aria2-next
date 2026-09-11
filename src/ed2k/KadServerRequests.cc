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
#include "ed2k_server.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include "Ed2kKadCommand.h"
#include "ed2k_constants.h"
#include "ed2k/KadCommandSupport.h"
#include "DownloadContext.h"
#include "DownloadEngine.h"
#include "Ed2kAttribute.h"
#include "Ed2kSession.h"
#include "Log.h"
#include "RequestGroup.h"
#include "RequestGroupMan.h"
#include "SimpleRandomizer.h"
#include "ed2k_packet.h"
#include "ed2k_policy.h"
#include "fmt.h"

namespace aria2 {

using namespace kad_command;

constexpr int64_t SERVER_STATUS_POLL_INTERVAL = 45;
namespace {
uint32_t createChallenge()
{
  uint32_t challenge = 0;
  SimpleRandomizer::getInstance()->getRandomBytes(
      reinterpret_cast<unsigned char*>(&challenge), sizeof(challenge));
  return challenge == 0 ? 1 : challenge;
}

} // namespace

bool Ed2kKadCommand::findServerByUdpEndpoint(
    ed2k::Endpoint& server, const ed2k::Endpoint& endpoint) const
{
  const auto session = e_->getRequestGroupMan()->getEd2kSession();
  const auto group = session->networkDownload();
  const auto attrs =
      group ? getEd2kAttrs(group->getDownloadContext()) : nullptr;
  if (!attrs) {
    return false;
  }
  for (const auto& state : attrs->serverStates) {
    if (state.endpoint.host != endpoint.host) {
      continue;
    }
    const auto plainPort = state.endpoint.port <= 65531
                               ? static_cast<uint16_t>(state.endpoint.port + 4)
                               : 0;
    if (endpoint.port == plainPort ||
        (state.udpObfuscationPort != 0 &&
         endpoint.port == state.udpObfuscationPort)) {
      server = state.endpoint;
      return true;
    }
  }
  return false;
}

void Ed2kKadCommand::queueServerStatusPoll()
{
  const auto now = nowSeconds();
  if (lastServerStatusPoll_ != 0 &&
      now - lastServerStatusPoll_ < SERVER_STATUS_POLL_INTERVAL) {
    return;
  }
  bool queued = false;
  auto attrs = getEd2kAttrs(requestGroup_->getDownloadContext());
  for (const auto& server : attrs->servers) {
    auto state = getEd2kServerState(attrs, server);
    if (!state || !state->handshakeCompleted || server.port > 65531) {
      continue;
    }
    state->udpStatusChallenge = createChallenge();
    state->lastUdpStatusTime = now;
    queueServerUdpPacket(*state, ed2k::OP_GLOBSERVSTATREQ,
                         ed2k::packUInt32(state->udpStatusChallenge));
    queued = true;
  }
  if (queued) {
    lastServerStatusPoll_ = now;
  }
}

void Ed2kKadCommand::queueServerSourcePoll()
{
  const auto session = e_->getRequestGroupMan()->getEd2kSession();
  const auto networkGroup = session->networkDownload();
  auto networkAttrs =
      networkGroup ? getEd2kAttrs(networkGroup->getDownloadContext()) : nullptr;
  if (!networkAttrs || networkAttrs->servers.empty()) {
    return;
  }
  const auto now = nowSeconds();
  for (const auto& server : networkAttrs->servers) {
    auto networkState = getEd2kServerState(networkAttrs, server);
    if (!networkState || networkState->connected ||
        (server.port > 65531 && networkState->udpObfuscationPort == 0)) {
      continue;
    }
    const bool extGetSources =
        (networkState->udpFlags & ed2k::SRV_UDPFLG_EXT_GETSOURCES) != 0;
    const bool extGetSources2 =
        (networkState->udpFlags & ed2k::SRV_UDPFLG_EXT_GETSOURCES2) != 0;
    const size_t fileLimit = extGetSources || extGetSources2 ? 31 : 1;
    std::string payload;
    std::vector<Ed2kAttribute*> requested;
    for (auto group : session->downloads()) {
      auto attrs = getEd2kAttrs(group->getDownloadContext());
      auto state = attrs ? getEd2kServerState(attrs, server) : nullptr;
      if (!attrs || !state || group->downloadFinished() ||
          attrs->searchActive || attrs->link.hash.empty() ||
          !ed2k::serverUdpSourceRequestDue(*state, attrs->link.size, now)) {
        continue;
      }
      payload += ed2k::createGlobGetSourcesPayload(
          attrs->link.hash, attrs->link.size, extGetSources2);
      requested.push_back(attrs);
      if (requested.size() == fileLimit) {
        break;
      }
    }
    if (requested.empty()) {
      continue;
    }
    queueServerUdpPacket(*networkState,
                         extGetSources2 ? ed2k::OP_GLOBGETSOURCES2
                                        : ed2k::OP_GLOBGETSOURCES,
                         payload);
    for (auto attrs : requested) {
      markEd2kServerUdpSourceRequestSent(attrs, server, now);
    }
    const auto destinationPort =
        networkState->udpKey != 0 && networkState->udpObfuscationPort != 0
            ? networkState->udpObfuscationPort
            : static_cast<uint16_t>(server.port + 4);
    A2_LOG_TRACE(fmt("Queued ED2K UDP source request for %lu file(s) to "
                     "%s:%u.",
                     static_cast<unsigned long>(requested.size()),
                     server.host.c_str(), destinationPort));
  }
}

} // namespace aria2
