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
#include "Ed2kAttribute.h"
#include "ed2k_policy.h"
#include <algorithm>
#include <limits>

namespace aria2 {

ed2k::ServerState* getEd2kServerState(Ed2kAttribute* attrs,
                                      const ed2k::Endpoint& server)
{
  if (!attrs || server.host.empty() || server.port == 0) {
    return nullptr;
  }
  auto i = std::find_if(attrs->serverStates.begin(), attrs->serverStates.end(),
                        [&](const ed2k::ServerState& item) {
                          return item.endpoint.host == server.host &&
                                 item.endpoint.port == server.port;
                        });
  if (i != attrs->serverStates.end()) {
    return &*i;
  }
  ed2k::ServerState state;
  state.endpoint = server;
  attrs->serverStates.push_back(state);
  return &attrs->serverStates.back();
}

ed2k::ServerState* updateEd2kServerConnecting(Ed2kAttribute* attrs,
                                              const ed2k::Endpoint& server)
{
  auto state = getEd2kServerState(attrs, server);
  if (!state) {
    return nullptr;
  }
  state->connecting = true;
  return state;
}

ed2k::ServerState* updateEd2kServerConnected(Ed2kAttribute* attrs,
                                             const ed2k::Endpoint& server)
{
  auto state = getEd2kServerState(attrs, server);
  if (!state) {
    return nullptr;
  }
  state->connecting = false;
  state->connected = true;
  return state;
}

void updateEd2kServerIdChange(Ed2kAttribute* attrs,
                              const ed2k::Endpoint& server,
                              const ed2k::ServerIdChange& idChange)
{
  auto state = updateEd2kServerConnected(attrs, server);
  if (!state) {
    return;
  }
  state->handshakeCompleted = true;
  state->clientId = idChange.clientId;
  state->highId = idChange.highId;
  state->ipAddress = idChange.ipAddress;
  state->tcpFlags = idChange.tcpFlags;
  if (idChange.tcpObfuscationPort != 0) {
    state->tcpObfuscationPort = idChange.tcpObfuscationPort;
  }
  state->failCount = 0;
  state->lastFailureTime = 0;
  state->nextRetryTime = 0;
}

void updateEd2kServerStatus(Ed2kAttribute* attrs, const ed2k::Endpoint& server,
                            const ed2k::ServerStatus& status)
{
  auto state = getEd2kServerState(attrs, server);
  if (!state) {
    return;
  }
  state->users = status.users;
  state->files = status.files;
}

void updateEd2kServerUdpStatus(Ed2kAttribute* attrs,
                               const ed2k::Endpoint& server,
                               const ed2k::ServerStatus& status, int64_t now)
{
  auto state = getEd2kServerState(attrs, server);
  if (!state) {
    return;
  }
  state->users = status.users;
  state->files = status.files;
  state->maxUsers = status.maxUsers;
  state->softFiles = status.softFiles;
  state->hardFiles = status.hardFiles;
  state->udpFlags = status.udpFlags;
  state->lowIdUsers = status.lowIdUsers;
  state->udpObfuscationPort = status.udpObfuscationPort;
  state->tcpObfuscationPort = status.tcpObfuscationPort;
  state->udpKey = status.udpKey;
  state->udpStatusChallenge = 0;
  state->lastUdpStatusTime = now;
}

void updateEd2kServerMessage(Ed2kAttribute* attrs, const ed2k::Endpoint& server,
                             const std::string& message)
{
  auto state = getEd2kServerState(attrs, server);
  if (!state) {
    return;
  }
  state->lastMessage = message;
}

void updateEd2kServerIdent(Ed2kAttribute* attrs, const ed2k::Endpoint& server,
                           const ed2k::ServerIdent& ident)
{
  auto state = getEd2kServerState(attrs, server);
  if (!state) {
    return;
  }
  if (!ident.name.empty()) {
    state->name = ident.name;
  }
  if (!ident.description.empty()) {
    state->description = ident.description;
  }
}

void updateEd2kServerSourceRequestTime(Ed2kAttribute* attrs,
                                       const ed2k::Endpoint& server,
                                       int64_t nextTime)
{
  auto state = getEd2kServerState(attrs, server);
  if (!state) {
    return;
  }
  state->nextSourceRequestTime = nextTime;
}

void markEd2kServerTcpSourceRequestSent(Ed2kAttribute* attrs,
                                        const ed2k::Endpoint& server,
                                        int64_t now)
{
  auto state = getEd2kServerState(attrs, server);
  if (!state) {
    return;
  }
  state->nextSourceRequestTime = now + ed2k::SERVER_TCP_SOURCE_REASK_INTERVAL;
}

void markEd2kServerUdpSourceRequestSent(Ed2kAttribute* attrs,
                                        const ed2k::Endpoint& server,
                                        int64_t now)
{
  auto state = getEd2kServerState(attrs, server);
  if (!state) {
    return;
  }
  state->lastUdpSourceRequestTime = now;
}

void updateEd2kServerSourceResponse(Ed2kAttribute* attrs,
                                    const ed2k::Endpoint& server,
                                    size_t sourceCount, int64_t now)
{
  auto state = getEd2kServerState(attrs, server);
  if (!state) {
    return;
  }
  state->lastSourceResponseTime = now;
  state->lastSourceCount = static_cast<uint32_t>(
      std::min<size_t>(sourceCount, std::numeric_limits<uint32_t>::max()));
}

void markEd2kServerSourceRequestFinished(Ed2kAttribute* attrs,
                                         const ed2k::Endpoint& server)
{
  auto state = getEd2kServerState(attrs, server);
  if (!state) {
    return;
  }
  state->connecting = false;
}

void updateEd2kServerFailure(Ed2kAttribute* attrs, const ed2k::Endpoint& server,
                             int64_t now, int64_t baseRetrySeconds)
{
  auto state = getEd2kServerState(attrs, server);
  if (!state) {
    return;
  }
  state->connecting = false;
  state->connected = false;
  state->handshakeCompleted = false;
  ++state->failCount;
  state->lastFailureTime = now;
  const auto multiplier = std::min<uint32_t>(state->failCount, 6);
  state->nextRetryTime = now + baseRetrySeconds * multiplier;
}

} // namespace aria2
