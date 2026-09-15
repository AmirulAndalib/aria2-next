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
#include "ed2k_kad_search.h"
#include <cstdint>
#include "Ed2kKadCommand.h"
#include "ed2k_constants.h"
#include "ed2k/KadCommandSupport.h"
#include "DownloadContext.h"
#include "DownloadEngine.h"
#include "Ed2kAttribute.h"
#include "Ed2kShareIndex.h"
#include "Log.h"
#include "RequestGroup.h"
#include "RequestGroupMan.h"
#include "fmt.h"
#include <algorithm>

namespace aria2 {

using namespace kad_command;

constexpr int64_t FIREWALLED_CHECK_INTERVAL = 3600;
constexpr int64_t SOURCE_PUBLISH_INTERVAL = 1800;
void Ed2kKadCommand::queueBootstrap()
{
  auto attrs = getEd2kAttrs(requestGroup_->getDownloadContext());
  if (!attrs->kadRoutingTable ||
      !attrs->kadRoutingTable->needBootstrap(nowSeconds())) {
    return;
  }
  const auto routerContacts = attrs->kadRoutingTable->getRouterContacts();
  if (!routerContacts.empty()) {
    const auto& contact =
        routerContacts[bootstrapCursor_++ % routerContacts.size()];
    const auto endpoint = toEndpoint(contact);
    queueKadContactPacket(contact, ed2k::KAD_BOOTSTRAP_REQ, std::string());
    ed2k::KadTransaction tx;
    tx.endpoint = endpoint;
    tx.contact = contact;
    tx.purpose = ed2k::KadTransactionPurpose::BOOTSTRAP;
    tx.expectedOpcode = ed2k::KAD_BOOTSTRAP_RES;
    tx.sentTime = nowSeconds();
    attrs->kadTransactions.add(tx);
    A2_LOG_DEBUG(fmt("Queued ED2K Kad bootstrap to %s:%u.",
                     endpoint.host.c_str(), endpoint.port));
    return;
  }

  const auto routerNodes = attrs->kadRoutingTable->getRouterNodes();
  if (!routerNodes.empty()) {
    const auto& endpoint = routerNodes[bootstrapCursor_++ % routerNodes.size()];
    queuePacket(endpoint, ed2k::KAD_BOOTSTRAP_REQ, std::string());
    ed2k::KadTransaction tx;
    tx.endpoint = endpoint;
    tx.purpose = ed2k::KadTransactionPurpose::BOOTSTRAP;
    tx.expectedOpcode = ed2k::KAD_BOOTSTRAP_RES;
    tx.sentTime = nowSeconds();
    attrs->kadTransactions.add(tx);
    A2_LOG_DEBUG(fmt("Queued ED2K Kad bootstrap to %s:%u.",
                     endpoint.host.c_str(), endpoint.port));
  }
}

void Ed2kKadCommand::queueRefresh()
{
  auto attrs = getEd2kAttrs(requestGroup_->getDownloadContext());
  if (!attrs->kadRoutingTable || attrs->kadRoutingTable->liveSize() == 0) {
    return;
  }
  std::string targetId;
  if (!attrs->kadRoutingTable->needRefresh(targetId, nowSeconds())) {
    return;
  }
  auto contacts = attrs->kadRoutingTable->findClosest(targetId, 8, true);
  for (const auto& contact : contacts) {
    const auto endpoint = toEndpoint(contact);
    queueKadContactPacket(contact, ed2k::KAD_REQ,
                          ed2k::createKadRequestPayload(ed2k::KAD_FIND_NODE,
                                                        targetId, contact.id));
    ed2k::KadTransaction tx;
    tx.endpoint = endpoint;
    tx.contact = contact;
    tx.purpose = ed2k::KadTransactionPurpose::REFRESH;
    tx.expectedOpcode = ed2k::KAD_RES;
    tx.targetId = targetId;
    tx.sentTime = nowSeconds();
    attrs->kadTransactions.add(tx);
  }
}

void Ed2kKadCommand::queueFirewalledCheck()
{
  auto attrs = getEd2kAttrs(requestGroup_->getDownloadContext());
  if (!attrs->kadRoutingTable || attrs->kadRoutingTable->liveSize() == 0) {
    return;
  }
  const auto tcpPort = localEd2kTcpPort(e_);
  if (tcpPort == 0) {
    return;
  }
  const auto now = nowSeconds();
  if (attrs->lastKadFirewalledCheck != 0 &&
      now - attrs->lastKadFirewalledCheck < FIREWALLED_CHECK_INTERVAL) {
    return;
  }
  const auto kadClientId = ed2k::ed2kHashToKadId(attrs->clientHash);
  auto contacts = attrs->kadRoutingTable->findClosest(kadClientId, 8, true);
  if (contacts.empty()) {
    return;
  }
  attrs->lastKadFirewalledCheck = now;
  attrs->kadFirewalled = true;
  attrs->kadFirewallCheckHosts.clear();
  for (const auto& contact : contacts) {
    const auto endpoint = toEndpoint(contact);
    attrs->kadFirewallCheckHosts.push_back(endpoint.host);
    queueKadContactPacket(
        contact, ed2k::KAD_FIREWALLED_REQ,
        ed2k::createKadFirewalledRequestPayload(tcpPort, kadClientId,
                                                localDirectCallbackOptions()));
    ed2k::KadTransaction tx;
    tx.endpoint = endpoint;
    tx.contact = contact;
    tx.purpose = ed2k::KadTransactionPurpose::FIREWALLED_CHECK;
    tx.expectedOpcode = ed2k::KAD_FIREWALLED_RES;
    tx.sentTime = now;
    attrs->kadTransactions.add(tx);
  }
}

void Ed2kKadCommand::queueSourcePublish()
{
  auto attrs = getEd2kAttrs(requestGroup_->getDownloadContext());
  if (!attrs->kadRoutingTable || attrs->kadRoutingTable->liveSize() == 0) {
    return;
  }
  if (attrs->kadFirewalled) {
    return;
  }
  const auto tcpPort = localEd2kTcpPort(e_);
  if (tcpPort == 0) {
    return;
  }
  const auto now = nowSeconds();
  if (attrs->lastKadSourcePublish != 0 &&
      now - attrs->lastKadSourcePublish < SOURCE_PUBLISH_INTERVAL) {
    return;
  }
  auto observed =
      std::find_if(attrs->kadObservedAddresses.begin(),
                   attrs->kadObservedAddresses.end(), publishableAddress);
  if (observed == attrs->kadObservedAddresses.end()) {
    return;
  }
  ed2k::Endpoint source;
  source.host = *observed;
  source.port = tcpPort;
  const auto udpPort = localEd2kUdpPort(e_);
  const auto sourceId = ed2k::ed2kHashToKadId(attrs->clientHash);

  bool queued = false;
  auto sharedSources = ed2k::listSharedSources(e_->getRequestGroupMan().get());
  for (const auto& shared : sharedSources) {
    if (!shared || shared->hash().empty()) {
      continue;
    }
    const auto kadFileId = ed2k::ed2kHashToKadId(shared->hash());
    auto contacts = attrs->kadRoutingTable->findClosest(kadFileId, 8, true);
    if (contacts.empty()) {
      continue;
    }
    const auto payload = ed2k::createKadPublishSourceRequestPayload(
        kadFileId, source, sourceId, shared->size(), udpPort,
        localDirectCallbackOptions());
    ed2k::KadPublishSourceRequest request;
    if (!ed2k::parseKadPublishSourceRequestPayload(request, payload)) {
      continue;
    }
    attrs->kadSourceIndex.store(kadFileId, request.source);
    for (const auto& contact : contacts) {
      queueKadContactPacket(contact, ed2k::KAD_PUBLISH_SOURCE_REQ, payload);
      queued = true;
    }
  }
  if (queued) {
    attrs->lastKadSourcePublish = now;
  }
}

} // namespace aria2
