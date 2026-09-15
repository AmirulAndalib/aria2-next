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
#include "ed2k_search.h"
#include <vector>
#include "Ed2kKadCommand.h"
#include "ed2k_constants.h"
#include "ed2k/KadCommandSupport.h"
#include "DownloadContext.h"
#include "Ed2kAttribute.h"
#include "RequestGroup.h"
#include "a2functional.h"

namespace aria2 {

using namespace kad_command;

void Ed2kKadCommand::queueTraversalActions(
    ed2k::KadTraversal& traversal,
    const std::vector<ed2k::KadTraversalAction>& actions)
{
  auto attrs = getEd2kAttrs(requestGroup_->getDownloadContext());
  for (const auto& action : actions) {
    const auto endpoint = toEndpoint(action.contact);
    if (action.type == ed2k::KadTraversalActionType::FIND_NODE) {
      const auto searchType =
          traversal.kind() == ed2k::KadTraversalKind::KEYWORD_LOOKUP ||
                  traversal.kind() == ed2k::KadTraversalKind::SOURCE_LOOKUP
              ? ed2k::KAD_FIND_VALUE
              : ed2k::KAD_FIND_NODE;
      queueKadContactPacket(action.contact, ed2k::KAD_REQ,
                            ed2k::createKadRequestPayload(searchType,
                                                          traversal.targetId(),
                                                          action.contact.id));
      ed2k::KadTransaction tx;
      tx.endpoint = endpoint;
      tx.contact = action.contact;
      tx.purpose = traversal.kind() == ed2k::KadTraversalKind::KEYWORD_LOOKUP
                       ? ed2k::KadTransactionPurpose::KEYWORD_LOOKUP
                       : ed2k::KadTransactionPurpose::SOURCE_LOOKUP;
      tx.expectedOpcode = ed2k::KAD_RES;
      tx.targetId = traversal.targetId();
      tx.sentTime = nowSeconds();
      attrs->kadTransactions.add(tx);
      continue;
    }

    if (traversal.kind() == ed2k::KadTraversalKind::KEYWORD_LOOKUP) {
      queueKadContactPacket(
          action.contact, ed2k::KAD_SEARCH_KEYS_REQ,
          ed2k::createKadSearchKeysRequestPayload(traversal.targetId(), 0));
    }
    else {
      queueKadContactPacket(action.contact, ed2k::KAD_SEARCH_SOURCES_REQ,
                            ed2k::createKadSearchSourcesRequestPayload(
                                traversal.targetId(), 0, traversal.size()));
    }
    ed2k::KadTransaction tx;
    tx.endpoint = endpoint;
    tx.contact = action.contact;
    tx.purpose = traversal.kind() == ed2k::KadTraversalKind::KEYWORD_LOOKUP
                     ? ed2k::KadTransactionPurpose::KEYWORD_LOOKUP
                     : ed2k::KadTransactionPurpose::SOURCE_LOOKUP;
    tx.expectedOpcode = ed2k::KAD_SEARCH_RES;
    tx.targetId = traversal.targetId();
    tx.sentTime = nowSeconds();
    attrs->kadTransactions.add(tx);
  }
}

void Ed2kKadCommand::queueSourceSearch()
{
  auto attrs = getEd2kAttrs(requestGroup_->getDownloadContext());
  const auto now = nowSeconds();
  if (!shouldStartEd2kKadSourceSearch(attrs, now)) {
    return;
  }
  const auto kadFileId = ed2k::ed2kHashToKadId(attrs->link.hash);
  auto contacts = attrs->kadRoutingTable->findClosest(kadFileId, 8, true);
  if (contacts.empty()) {
    return;
  }
  attrs->kadSourceTraversal = make_unique<ed2k::KadTraversal>(
      ed2k::KadTraversalKind::SOURCE_LOOKUP, kadFileId, attrs->link.size);
  queueTraversalActions(*attrs->kadSourceTraversal,
                        attrs->kadSourceTraversal->start(contacts));
  markEd2kKadSourceSearchStarted(attrs, now);
}

void Ed2kKadCommand::queueKeywordSearch()
{
  auto attrs = getEd2kAttrs(requestGroup_->getDownloadContext());
  if (!attrs->searchActive || !attrs->kadRoutingTable ||
      attrs->kadRoutingTable->liveSize() == 0 || attrs->kadKeywordTraversal) {
    return;
  }
  const auto targetId =
      ed2k::createKadKeywordTarget(attrs->searchQuery.keyword);
  auto contacts = attrs->kadRoutingTable->findClosest(targetId, 8, true);
  if (contacts.empty()) {
    return;
  }
  attrs->kadKeywordTraversal = make_unique<ed2k::KadTraversal>(
      ed2k::KadTraversalKind::KEYWORD_LOOKUP, targetId, 0);
  queueTraversalActions(*attrs->kadKeywordTraversal,
                        attrs->kadKeywordTraversal->start(contacts));
}

void Ed2kKadCommand::expireTransactions(Ed2kAttribute& attrs)
{
  const auto expired = attrs.kadTransactions.expire(nowSeconds(), 12);
  if (!attrs.kadRoutingTable) {
    return;
  }
  for (const auto& transaction : expired) {
    attrs.kadRoutingTable->nodeFailed(transaction.contact);
    auto traversal = pendingTraversal(attrs, transaction.purpose);
    if (!traversal) {
      continue;
    }
    if (transaction.expectedOpcode == ed2k::KAD_SEARCH_RES) {
      traversal->onSearchFailure(transaction.contact);
    }
    else {
      queueTraversalActions(*traversal,
                            traversal->onFailure(transaction.contact));
    }
  }
}

} // namespace aria2
