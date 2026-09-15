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
#include "Ed2kAttribute.h"
#include "Ed2kCommand.h"
#include "Ed2kShareIndex.h"
#include "Log.h"
#include "RequestGroup.h"
#include "ed2k_constants.h"
#include "ed2k_policy.h"
#include "ed2k_search.h"
#include "ed2k_server.h"
#include "fmt.h"
#include <algorithm>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <limits>
#include "support/Encoding.h"
#include "ed2k/Ed2kCommandInternal.h"

namespace aria2 {
using namespace ed2k_command;

bool Ed2kCommand::queueDueServerRequest()
{
  if (mode_ != Mode::SERVER || state_ != State::READ_HEADER ||
      !outbox_.empty()) {
    return false;
  }
  auto attrs = getEd2kAttrs(getDownloadContext());
  auto state = getEd2kServerState(attrs, endpoint_);
  if (!state || !state->connected || attrs->searchActive ||
      !ed2k::serverTcpSourceRequestDue(*state, attrs->link.size,
                                       nowSeconds())) {
    return false;
  }
  if (!queueAllServerSourceRequests()) {
    markEd2kServerTcpSourceRequestSent(attrs, endpoint_, nowSeconds());
    return false;
  }
  state_ = State::WRITE;
  return true;
}

void Ed2kCommand::queueServerLogin()
{
  auto attrs = getEd2kAttrs(getDownloadContext());
  queuePacket(ed2k::PROTO_EDONKEY, ed2k::OP_LOGINREQUEST,
              ed2k::createLoginRequestPayload(
                  attrs->clientHash, 0, localEd2kTcpPort(getDownloadEngine()),
                  "aria2-next"));
}

void Ed2kCommand::queueServerOfferFiles()
{
  auto rgman = getDownloadEngine()->getRequestGroupMan().get();
  auto state =
      getEd2kServerState(getEd2kAttrs(getDownloadContext()), endpoint_);
  if (!rgman || !state || !state->handshakeCompleted) {
    return;
  }
  auto sources = ed2k::listSharedSources(rgman);
  std::string payload;
  const auto limit = state->softFiles == 0
                         ? static_cast<size_t>(200)
                         : std::min<size_t>(state->softFiles, 200);
  const bool supportsLarge =
      (state->tcpFlags & ed2k::SRV_TCPFLG_LARGEFILES) != 0;
  if (!ed2k::createOfferFilesPayload(
          payload, sources, supportsLarge, limit,
          state->highId ? state->clientId : 0,
          state->highId ? localEd2kTcpPort(getDownloadEngine()) : 0)) {
    return;
  }
  queuePacket(ed2k::PROTO_EDONKEY, ed2k::OP_OFFERFILES, payload);
}

bool Ed2kCommand::queueGetSources(RequestGroup* group)
{
  group = group ? group : getRequestGroup();
  const auto attrs = getEd2kAttrs(group->getDownloadContext());
  const auto state = getEd2kServerState(attrs, endpoint_);
  if (attrs->link.size > std::numeric_limits<uint32_t>::max() && state &&
      (state->tcpFlags & ed2k::SRV_TCPFLG_LARGEFILES) == 0) {
    A2_LOG_DEBUG(fmt("CUID#%" PRId64
                     " - ED2K server %s:%u does not advertise large-file "
                     "source requests for %s.",
                     getCuid(), endpoint_.host.c_str(), endpoint_.port,
                     util::toHex(attrs->link.hash).c_str()));
    return false;
  }
  const bool requestObfuSources =
      state && (state->tcpFlags & ed2k::SRV_TCPFLG_TCPOBFUSCATION) != 0;
  queuePacket(
      ed2k::PROTO_EDONKEY,
      requestObfuSources ? ed2k::OP_GETSOURCES_OBFU : ed2k::OP_GETSOURCES,
      ed2k::createGetSourcesPayload(attrs->link.hash, attrs->link.size));
  return true;
}

bool Ed2kCommand::queueAllServerSourceRequests()
{
  bool queued = false;
  auto rgman = getDownloadEngine()->getRequestGroupMan().get();
  if (!rgman) {
    return queueGetSources();
  }
  auto session = rgman->getEd2kSession();
  auto primaryState =
      getEd2kServerState(getEd2kAttrs(getDownloadContext()), endpoint_);
  for (auto group : session->downloads()) {
    auto attrs = getEd2kAttrs(group->getDownloadContext());
    if (!attrs || attrs->searchActive || group->downloadFinished()) {
      continue;
    }
    auto state = getEd2kServerState(attrs, endpoint_);
    if (primaryState && state != primaryState) {
      state->connected = primaryState->connected;
      state->connecting = primaryState->connecting;
      state->handshakeCompleted = primaryState->handshakeCompleted;
      state->clientId = primaryState->clientId;
      state->highId = primaryState->highId;
      state->ipAddress = primaryState->ipAddress;
      state->tcpFlags = primaryState->tcpFlags;
      state->tcpObfuscationPort = primaryState->tcpObfuscationPort;
    }
    if (queueGetSources(group)) {
      queued = true;
    }
    markEd2kServerTcpSourceRequestSent(attrs, endpoint_, nowSeconds());
  }
  return queued;
}

void Ed2kCommand::queueSearchRequest()
{
  const auto attrs = getEd2kAttrs(getDownloadContext());
  if (!attrs->searchActive) {
    return;
  }
  queuePacket(
      ed2k::PROTO_EDONKEY, ed2k::OP_SEARCHREQUEST,
      ed2k::createSearchRequestPayload(
          attrs->searchQuery,
          attrs->link.size >
              static_cast<int64_t>(std::numeric_limits<uint32_t>::max())));
}

void Ed2kCommand::queueCallbackRequest(RequestGroup* group, uint32_t clientId)
{
  pendingCallbacks_.push_back({group, clientId});
  constexpr int64_t CALLBACK_TIMEOUT = 45;
  markEd2kCallbackRequestSent(getEd2kAttrs(group->getDownloadContext()),
                              clientId, nowSeconds(), CALLBACK_TIMEOUT);
  queuePacket(ed2k::PROTO_EDONKEY, ed2k::OP_CALLBACKREQUEST,
              ed2k::createCallbackRequestPayload(clientId));
}

uint32_t Ed2kCommand::localEd2kClientId() const
{
  auto attrs = getEd2kAttrs(getDownloadContext());
  if (!attrs) {
    return 0;
  }
  for (const auto& state : attrs->serverStates) {
    if (state.handshakeCompleted && state.clientId != 0) {
      return state.clientId;
    }
  }
  return 0;
}

ed2k::Endpoint Ed2kCommand::localEd2kServerEndpoint() const
{
  auto attrs = getEd2kAttrs(getDownloadContext());
  if (!attrs) {
    return ed2k::Endpoint();
  }
  for (const auto& state : attrs->serverStates) {
    if (state.handshakeCompleted && state.clientId != 0) {
      return state.endpoint;
    }
  }
  return ed2k::Endpoint();
}

} // namespace aria2
