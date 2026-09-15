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
#include "ed2k_server.h"
#include <string>
#include <vector>
#include "Ed2kKadCommand.h"
#include "ed2k/KadCommandSupport.h"
#include "DownloadContext.h"
#include "DownloadEngine.h"
#include "Ed2kAttribute.h"
#include "Ed2kSession.h"
#include "Log.h"
#include "RequestGroup.h"
#include "RequestGroupMan.h"
#include "ed2k_packet.h"
#include "fmt.h"

namespace aria2 {

using namespace kad_command;

void Ed2kKadCommand::handleServerSources(const ed2k::Endpoint& endpoint,
                                         const std::string& payload)
{
  const auto session = e_->getRequestGroupMan()->getEd2kSession();
  ed2k::Endpoint server;
  const auto knownServer = findServerByUdpEndpoint(server, endpoint);
  for (auto group : session->downloads()) {
    auto attrs = getEd2kAttrs(group->getDownloadContext());
    std::vector<ed2k::FoundSource> sources;
    if (!attrs || !ed2k::parsePackedFoundSourcesPayloads(sources, payload,
                                                         attrs->link.hash)) {
      continue;
    }
    const auto added =
        mergeEd2kServerSources(attrs, sources, ed2k::PEER_SOURCE_SERVER);
    if (knownServer) {
      updateEd2kServerSourceResponse(attrs, server, sources.size(),
                                     nowSeconds());
    }
    if (added != 0) {
      A2_LOG_DEBUG(fmt("ED2K UDP server %s:%u returned %lu source(s).",
                       endpoint.host.c_str(), endpoint.port,
                       static_cast<unsigned long>(sources.size())));
      schedulePendingEd2kPeers(group, e_);
    }
    return;
  }
}

void Ed2kKadCommand::handleInvalidLowId(const std::string& payload)
{
  const auto session = e_->getRequestGroupMan()->getEd2kSession();
  if (payload.size() >= 4) {
    for (auto group : session->downloads()) {
      markEd2kCallbackFailed(getEd2kAttrs(group->getDownloadContext()),
                             ed2k::readUInt32(payload.data()));
    }
  }
}

void Ed2kKadCommand::handleServerStatus(const ed2k::Endpoint& endpoint,
                                        const std::string& payload)
{
  const auto session = e_->getRequestGroupMan()->getEd2kSession();
  ed2k::Endpoint server;
  if (!findServerByUdpEndpoint(server, endpoint)) {
    return;
  }
  ed2k::ServerStatus status;
  if (!ed2k::parseServerUdpStatusPayload(status, payload) ||
      status.challenge == 0) {
    return;
  }
  bool challengeMatched = false;
  for (auto group : session->downloads()) {
    auto attrs = getEd2kAttrs(group->getDownloadContext());
    auto state = getEd2kServerState(attrs, server);
    if (!state) {
      continue;
    }
    if (status.challenge == state->udpStatusChallenge) {
      challengeMatched = true;
    }
  }
  if (!challengeMatched) {
    return;
  }
  for (auto group : session->downloads()) {
    auto attrs = getEd2kAttrs(group->getDownloadContext());
    if (getEd2kServerState(attrs, server)) {
      updateEd2kServerUdpStatus(attrs, server, status, nowSeconds());
    }
  }
}

} // namespace aria2
