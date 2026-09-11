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
#include "ed2k_link.h"
#include "ed2k_peer.h"
#include "ed2k_search.h"
#include <string>
#include "Ed2kKadCommand.h"
#include "ed2k_constants.h"
#include "ed2k/KadCommandSupport.h"
#include "Ed2kAttribute.h"
#include "Log.h"
#include "support/Encoding.h"
#include "fmt.h"

namespace aria2 {

using namespace kad_command;

void Ed2kKadCommand::handleNodeRequest(
    Ed2kAttribute& attrs, const ed2k::Endpoint& endpoint,
    const ed2k::KadObfuscatedDatagram* context, const std::string& payload)
{
  ed2k::KadRequest request;
  if (!ed2k::parseKadRequestPayload(request, payload) ||
      request.receiverId != ed2k::ed2kHashToKadId(attrs.clientHash)) {
    return;
  }
  const auto searchType = request.searchType & 0x1f;
  if (searchType == 0) {
    return;
  }
  auto response = ed2k::createKadResponsePayload(
      request.targetId,
      attrs.kadRoutingTable->findClosest(request.targetId, 32, false));
  queueKadResponsePacket(endpoint, context, ed2k::KAD_RES, response);
}

void Ed2kKadCommand::handleNodeResponse(Ed2kAttribute& attrs,
                                        const ed2k::Endpoint& endpoint,
                                        const std::string& payload)
{
  ed2k::KadResponse response;
  if (!ed2k::parseKadResponsePayload(response, payload)) {
    return;
  }
  ed2k::KadTransaction tx;
  const auto knownResponse = attrs.kadTransactions.complete(
      endpoint, ed2k::KAD_RES, response.targetId, tx);
  if (!knownResponse) {
    return;
  }
  attrs.kadRoutingTable->nodeSeen(tx.contact, nowSeconds());
  for (const auto& contact : response.contacts) {
    attrs.kadRoutingTable->heardAbout(contact, nowSeconds());
  }
  if (auto traversal = pendingTraversal(attrs, tx.purpose)) {
    queueTraversalActions(*traversal,
                          traversal->onResponse(tx.contact, response.contacts));
  }
}

void Ed2kKadCommand::handleSearchResponse(Ed2kAttribute& attrs,
                                          const ed2k::Endpoint& endpoint,
                                          const std::string& payload)
{
  ed2k::KadSearchResult result;
  if (ed2k::parseKadSearchResultPayload(result, payload)) {
    ed2k::KadTransaction tx;
    if (!attrs.kadTransactions.complete(endpoint, ed2k::KAD_SEARCH_RES,
                                        result.targetId, tx)) {
      return;
    }
    if (auto traversal = pendingTraversal(attrs, tx.purpose)) {
      traversal->onSearchResponse(tx.contact);
    }
    if (attrs.searchActive) {
      auto entries =
          ed2k::kadSearchEntriesToSearchResults(result.entries, "kad");
      addEd2kSearchResults(&attrs, entries, false);
      return;
    }
    auto sources = ed2k::extractKadSourceEndpointDetails(result);
    A2_LOG_TRACE(fmt("ED2K Kad search response from %s:%u target=%s "
                     "entries=%lu sources=%lu.",
                     endpoint.host.c_str(), endpoint.port,
                     util::toHex(result.targetId).c_str(),
                     static_cast<unsigned long>(result.entries.size()),
                     static_cast<unsigned long>(sources.size())));
    for (const auto& source : sources) {
      addEd2kKadSourcePeer(&attrs, source, ed2k::PEER_SOURCE_KAD);
    }
    schedulePendingEd2kPeers(requestGroup_, e_);
  }
}

void Ed2kKadCommand::handlePublishRequest(
    Ed2kAttribute& attrs, const ed2k::Endpoint& endpoint,
    const ed2k::KadObfuscatedDatagram* context, const std::string& payload)
{
  ed2k::KadPublishSourceRequest request;
  if (!ed2k::parseKadPublishSourceRequestPayload(request, payload)) {
    return;
  }
  attrs.kadSourceIndex.store(request.fileId, request.source);
  auto response = ed2k::createKadPublishResultPayload(request.fileId, 1);
  queueKadResponsePacket(endpoint, context, ed2k::KAD_PUBLISH_RES, response);
}

void Ed2kKadCommand::handleSourceRequest(
    Ed2kAttribute& attrs, const ed2k::Endpoint& endpoint,
    const ed2k::KadObfuscatedDatagram* context, const std::string& payload)
{
  ed2k::KadSearchSourcesRequest request;
  if (!ed2k::parseKadSearchSourcesRequestPayload(request, payload)) {
    return;
  }
  auto entries =
      attrs.kadSourceIndex.find(request.targetId, request.startPosition, 50);
  if (!entries.empty()) {
    auto response = ed2k::createKadSearchResultPayload(
        ed2k::ed2kHashToKadId(attrs.clientHash), request.targetId, entries);
    queueKadResponsePacket(endpoint, context, ed2k::KAD_SEARCH_RES, response);
  }
}

} // namespace aria2
