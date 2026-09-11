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
#include "Ed2kKadState.h"
#include "ed2k_kad.h"
#include "ed2k_link.h"
#include <string>
#include "Ed2kKadCommand.h"
#include "ed2k_constants.h"
#include "ed2k/KadCommandSupport.h"
#include "DownloadEngine.h"
#include "Ed2kAttribute.h"
#include "Ed2kCommand.h"
#include "ed2k_hash.h"
#include "a2functional.h"
#include <algorithm>

namespace aria2 {

using namespace kad_command;

void Ed2kKadCommand::handleFirewallResponse(Ed2kAttribute& attrs,
                                            const ed2k::Endpoint& endpoint,
                                            const std::string& payload)
{
  ed2k::KadFirewalledResponse response;
  if (!ed2k::parseKadFirewalledResponsePayload(response, payload)) {
    return;
  }
  ed2k::KadTransaction tx;
  if (attrs.kadTransactions.complete(endpoint, ed2k::KAD_FIREWALLED_RES, tx)) {
    attrs.kadRoutingTable->nodeSeen(tx.contact, nowSeconds());
  }
  if (std::find(attrs.kadObservedAddresses.begin(),
                attrs.kadObservedAddresses.end(),
                response.ipAddress) == attrs.kadObservedAddresses.end()) {
    attrs.kadObservedAddresses.push_back(response.ipAddress);
  }
}

void Ed2kKadCommand::handleFirewallRequest(
    const ed2k::Endpoint& endpoint, const ed2k::KadObfuscatedDatagram* context,
    const std::string& payload)
{
  ed2k::KadFirewalledRequest request;
  if (!ed2k::parseKadFirewalledRequestPayload(request, payload)) {
    return;
  }
  if (request.tcpPort == 0 || request.id.size() != ed2k::HASH_LENGTH) {
    return;
  }
  ed2k::Endpoint peer;
  peer.host = endpoint.host;
  peer.port = request.tcpPort;
  peer.userHash = ed2k::kadIdToEd2kHash(request.id);
  peer.cryptOptions = request.options;
  e_->addCommand(make_unique<Ed2kCommand>(e_->newCUID(), requestGroup_, e_,
                                          peer, false, false, true));
  auto response = ed2k::createKadFirewalledResponsePayload(endpoint.host);
  queueKadResponsePacket(endpoint, context, ed2k::KAD_FIREWALLED_RES, response);
}

void Ed2kKadCommand::handlePing(const ed2k::Endpoint& endpoint,
                                const ed2k::KadObfuscatedDatagram* context)
{
  queueKadResponsePacket(endpoint, context, ed2k::KAD_PONG, std::string());
}

} // namespace aria2
