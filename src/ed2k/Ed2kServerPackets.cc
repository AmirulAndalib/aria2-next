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
#include "DlRetryEx.h"
#include "Ed2kAttribute.h"
#include "Ed2kCommand.h"
#include "GroupId.h"
#include "Log.h"
#include "ed2k_constants.h"
#include "ed2k_link.h"
#include "ed2k_packet.h"
#include "ed2k_peer.h"
#include "ed2k_search.h"
#include "ed2k_server.h"
#include "fmt.h"
#include <algorithm>
#include <cinttypes>
#include <vector>
#include "support/Encoding.h"
#include "ed2k/Ed2kCommandInternal.h"

namespace aria2 {
using namespace ed2k_command;

void Ed2kCommand::handleServerPacket()
{
  auto attrs = getEd2kAttrs(getDownloadContext());
  if (currentHeader_.opcode == ed2k::OP_IDCHANGE) {
    ed2k::ServerIdChange idChange;
    if (!ed2k::parseServerIdChangePayload(idChange, body_)) {
      throw DL_RETRY_EX("Bad ED2K server ID change.");
    }
    auto session = getDownloadEngine()->getRequestGroupMan()->getEd2kSession();
    for (auto group : session->downloads()) {
      updateEd2kServerIdChange(getEd2kAttrs(group->getDownloadContext()),
                               endpoint_, idChange);
    }
    A2_LOG_INFO(fmt("component=ed2k event=server_connected gid=%s cuid=%" PRId64
                    " endpoint=%s:%u id_type=%s client_id=0x%08x",
                    GroupId::toHex(getRequestGroup()->getGID()).c_str(),
                    getCuid(), logging::sanitizeText(endpoint_.host).c_str(),
                    endpoint_.port, idChange.highId ? "high" : "low",
                    idChange.clientId));
    if (attrs->searchActive) {
      queueServerOfferFiles();
      queueSearchRequest();
    }
    else {
      queueServerOfferFiles();
      A2_LOG_DEBUG(fmt("CUID#%" PRId64
                       " - ED2K server %s:%u requesting sources for %s.",
                       getCuid(), endpoint_.host.c_str(), endpoint_.port,
                       util::toHex(attrs->link.hash).c_str()));
      if (!queueAllServerSourceRequests()) {
        markEd2kServerSourceRequestFinished(attrs, endpoint_);
        state_ = State::READ_HEADER;
        return;
      }
    }
    state_ = State::WRITE;
    return;
  }
  if (currentHeader_.opcode == ed2k::OP_FOUNDSOURCES ||
      currentHeader_.opcode == ed2k::OP_FOUNDSOURCES_OBFU) {
    auto session = getDownloadEngine()->getRequestGroupMan()->getEd2kSession();
    RequestGroup* sourceGroup = nullptr;
    std::vector<ed2k::FoundSource> sources;
    for (auto group : session->downloads()) {
      auto candidateAttrs = getEd2kAttrs(group->getDownloadContext());
      if (candidateAttrs &&
          ed2k::parseFoundSourcesPayload(
              sources, body_, candidateAttrs->link.hash,
              currentHeader_.opcode == ed2k::OP_FOUNDSOURCES_OBFU)) {
        sourceGroup = group;
        attrs = candidateAttrs;
        break;
      }
      sources.clear();
    }
    if (!sourceGroup) {
      A2_LOG_DEBUG(fmt("CUID#%" PRId64
                       " - ED2K server %s:%u returned unusable sources.",
                       getCuid(), endpoint_.host.c_str(), endpoint_.port));
      updateEd2kServerSourceResponse(attrs, endpoint_, 0, nowSeconds());
      markEd2kServerSourceRequestFinished(attrs, endpoint_);
      state_ = State::READ_HEADER;
      return;
    }
    auto serverState = getEd2kServerState(attrs, endpoint_);
    const bool canRequestCallback =
        serverState && serverState->handshakeCompleted && serverState->highId;
    for (const auto& source : sources) {
      if (source.lowId) {
        if (canRequestCallback) {
          addEd2kFoundSource(attrs, source, ed2k::PEER_SOURCE_SERVER, true);
          queueCallbackRequest(sourceGroup, source.clientId);
        }
        else {
          addEd2kFoundSource(attrs, source, ed2k::PEER_SOURCE_SERVER, false);
        }
      }
    }
    mergeEd2kServerSources(attrs, sources, ed2k::PEER_SOURCE_SERVER);
    A2_LOG_DEBUG(fmt("CUID#%" PRId64
                     " - ED2K server %s:%u returned %lu source(s).",
                     getCuid(), endpoint_.host.c_str(), endpoint_.port,
                     static_cast<unsigned long>(sources.size())));
    updateEd2kServerSourceResponse(attrs, endpoint_, sources.size(),
                                   nowSeconds());
    schedulePendingEd2kPeers(sourceGroup, getDownloadEngine());
    markEd2kServerSourceRequestFinished(attrs, endpoint_);
    state_ = outbox_.empty() ? State::READ_HEADER : State::WRITE;
    return;
  }
  if (currentHeader_.opcode == ed2k::OP_CALLBACKREQUESTED) {
    ed2k::Endpoint peer;
    if (!ed2k::parseCallbackRequestIncomingPayload(peer, body_)) {
      throw DL_RETRY_EX("Bad ED2K callback request.");
    }
    RequestGroup* callbackGroup = getRequestGroup();
    if (!pendingCallbacks_.empty()) {
      callbackGroup = pendingCallbacks_.front().group;
      attrs = getEd2kAttrs(callbackGroup->getDownloadContext());
    }
    addEd2kPeer(attrs, peer, ed2k::PEER_SOURCE_SERVER);
    if (!pendingCallbacks_.empty()) {
      markEd2kCallbackAccepted(attrs, pendingCallbacks_.front().clientId, peer,
                               nowSeconds());
      pendingCallbacks_.pop_front();
    }
    schedulePendingEd2kPeers(callbackGroup, getDownloadEngine());
    state_ = State::READ_HEADER;
    return;
  }
  if (currentHeader_.opcode == ed2k::OP_CALLBACK_FAIL) {
    if (body_.size() >= 4) {
      const auto clientId = ed2k::readUInt32(body_.data());
      auto i = std::find_if(pendingCallbacks_.begin(), pendingCallbacks_.end(),
                            [&](const PendingCallback& callback) {
                              return callback.clientId == clientId;
                            });
      if (i != pendingCallbacks_.end()) {
        markEd2kCallbackFailed(getEd2kAttrs(i->group->getDownloadContext()),
                               clientId);
        pendingCallbacks_.erase(i);
      }
    }
    A2_LOG_DEBUG(fmt("CUID#%" PRId64
                     " - ED2K server %s:%u reported callback failure.",
                     getCuid(), endpoint_.host.c_str(), endpoint_.port));
    state_ = State::READ_HEADER;
    return;
  }
  if (currentHeader_.opcode == ed2k::OP_REJECT) {
    A2_LOG_DEBUG(fmt("CUID#%" PRId64
                     " - ED2K server %s:%u rejected the last command.",
                     getCuid(), endpoint_.host.c_str(), endpoint_.port));
    state_ = State::READ_HEADER;
    return;
  }
  if (currentHeader_.opcode == ed2k::OP_SEARCHRESULT) {
    ed2k::SearchResult result;
    if (!ed2k::parseSearchResultPayload(result, body_, "server")) {
      A2_LOG_DEBUG(
          fmt("CUID#%" PRId64
              " - ED2K server %s:%u returned an unusable search result.",
              getCuid(), endpoint_.host.c_str(), endpoint_.port));
      state_ = State::READ_HEADER;
      return;
    }
    addEd2kSearchResults(getEd2kAttrs(getDownloadContext()), result.entries,
                         result.moreResults);
    state_ = State::READ_HEADER;
  }
  if (currentHeader_.opcode == ed2k::OP_SERVERSTATUS) {
    ed2k::ServerStatus status;
    if (!ed2k::parseServerStatusPayload(status, body_)) {
      throw DL_RETRY_EX("Bad ED2K server status.");
    }
    updateEd2kServerStatus(attrs, endpoint_, status);
  }
  if (currentHeader_.opcode == ed2k::OP_SERVERMESSAGE) {
    std::string message;
    if (!ed2k::parseServerMessagePayload(message, body_)) {
      throw DL_RETRY_EX("Bad ED2K server message.");
    }
    A2_LOG_DEBUG(fmt("CUID#%" PRId64 " - ED2K server %s:%u message: %s",
                     getCuid(), endpoint_.host.c_str(), endpoint_.port,
                     message.c_str()));
    updateEd2kServerMessage(attrs, endpoint_, message);
  }
  if (currentHeader_.opcode == ed2k::OP_SERVERIDENT) {
    ed2k::ServerIdent ident;
    if (!ed2k::parseServerIdentPayload(ident, body_)) {
      throw DL_RETRY_EX("Bad ED2K server ident.");
    }
    updateEd2kServerIdent(attrs, endpoint_, ident);
  }
  if (currentHeader_.opcode == ed2k::OP_SERVERLIST) {
    std::vector<ed2k::Endpoint> servers;
    if (!ed2k::parseServerListPayload(servers, body_)) {
      throw DL_RETRY_EX("Bad ED2K server list.");
    }
    for (const auto& server : servers) {
      auto before = attrs->servers.size();
      auto i = std::find_if(attrs->servers.begin(), attrs->servers.end(),
                            [&](const ed2k::Endpoint& item) {
                              return item.host == server.host &&
                                     item.port == server.port;
                            });
      if (i == attrs->servers.end()) {
        attrs->servers.push_back(server);
      }
      if (attrs->servers.size() != before) {
        getEd2kServerState(attrs, server);
      }
    }
  }
}

} // namespace aria2
