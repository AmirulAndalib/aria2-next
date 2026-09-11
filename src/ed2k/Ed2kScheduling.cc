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
#include "ed2k_server.h"
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>
#include "Ed2kAttribute.h"
#include "DownloadEngine.h"
#include "RequestGroup.h"
#include "Ed2kCommand.h"
#include "Command.h"
#include "ed2k_policy.h"
#include "wallclock.h"
#include "a2functional.h"
#include <algorithm>
#include <chrono>

namespace aria2 {

namespace {
class Ed2kPeerScheduleCommand : public Command {
public:
  Ed2kPeerScheduleCommand(cuid_t cuid, RequestGroup* requestGroup,
                          DownloadEngine* e)
      : Command(cuid), requestGroup_(requestGroup), e_(e)
  {
    setStatus(Command::STATUS_ONESHOT_REALTIME);
  }

  bool execute() override
  {
    if (!requestGroup_->downloadFinished() &&
        !requestGroup_->isHaltRequested()) {
      schedulePendingEd2kPeers(requestGroup_, e_);
    }
    return true;
  }

private:
  RequestGroup* requestGroup_;
  DownloadEngine* e_;
};
} // namespace

void schedulePendingEd2kServers(RequestGroup* requestGroup, DownloadEngine* e)
{
  std::vector<std::unique_ptr<Command>> commands;
  schedulePendingEd2kServers(commands, requestGroup, e);
  e->addCommand(std::move(commands));
}

void schedulePendingEd2kServers(std::vector<std::unique_ptr<Command>>& commands,
                                RequestGroup* requestGroup, DownloadEngine* e)
{
  auto attrs = getEd2kAttrs(requestGroup->getDownloadContext());
  if (attrs->servers.empty()) {
    return;
  }
  constexpr size_t MAX_SERVER_CONNECTION_ATTEMPTS = 2;
  if (e->getEd2kServerConnectionCount() >= MAX_SERVER_CONNECTION_ATTEMPTS) {
    return;
  }
  const auto connected = std::find_if(
      attrs->serverStates.begin(), attrs->serverStates.end(),
      [](const ed2k::ServerState& state) { return state.connected; });
  if (connected != attrs->serverStates.end()) {
    return;
  }
  if (attrs->nextServerIndex >= attrs->servers.size()) {
    attrs->nextServerIndex = 0;
  }
  const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                       global::wallclock().getTime().time_since_epoch())
                       .count();
  const auto limit = attrs->servers.size();
  size_t scanned = 0;
  while (scanned < limit &&
         e->getEd2kServerConnectionCount() < MAX_SERVER_CONNECTION_ATTEMPTS) {
    auto server = attrs->servers[attrs->nextServerIndex];
    attrs->nextServerIndex =
        (attrs->nextServerIndex + 1) % attrs->servers.size();
    ++scanned;
    auto state = getEd2kServerState(attrs, server);
    if (!state) {
      continue;
    }
    if (state->connecting) {
      continue;
    }
    if (state->connected) {
      continue;
    }
    if (!ed2k::serverConnectionDue(*state, now)) {
      continue;
    }
    updateEd2kServerConnecting(attrs, server);
    commands.push_back(make_unique<Ed2kCommand>(e->newCUID(), requestGroup, e,
                                                server, true, false));
  }
}

void schedulePendingEd2kPeers(RequestGroup* requestGroup, DownloadEngine* e)
{
  auto attrs = getEd2kAttrs(requestGroup->getDownloadContext());
  const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                       global::wallclock().getTime().time_since_epoch())
                       .count();
  expireEd2kDeadSources(attrs, now);
  expireEd2kCallbackWaits(attrs, now);
  expireEd2kPeerUdpReasks(attrs, now, 30);
  promoteEd2kTcpReasks(attrs, now);
  while (requestGroup->getNumStreamCommand() <
         requestGroup->getNumConcurrentCommand()) {
    auto action = ed2k::selectPeerAction(attrs->peerStates, now);
    if (action.type != ed2k::PeerActionType::CONNECT &&
        action.type != ed2k::PeerActionType::RETRY) {
      return;
    }
    auto state = action.peer;
    if (!state) {
      return;
    }
    auto peer = state->endpoint;
    state->dead = false;
    state->connecting = true;
    e->addCommand(
        make_unique<Ed2kCommand>(e->newCUID(), requestGroup, e, peer, false));
  }
}

void scheduleEd2kPeerCheck(RequestGroup* requestGroup, DownloadEngine* e)
{
  e->addCommand(
      make_unique<Ed2kPeerScheduleCommand>(e->newCUID(), requestGroup, e));
  e->setNoWait(true);
}

} // namespace aria2
