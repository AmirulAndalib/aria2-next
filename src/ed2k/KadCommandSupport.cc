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
#include "PieceStorage.h"
#include "ed2k/KadCommandSupport.h"
#include "Ed2kKadState.h"
#include "ed2k_kad.h"
#include "ed2k_link.h"
#include <cstddef>
#include <cstdint>
#include <vector>
#include "ed2k_constants.h"
#include "DlAbortEx.h"
#include "DownloadContext.h"
#include "DownloadEngine.h"
#include "Ed2kAttribute.h"
#include "Option.h"
#include "RequestGroup.h"
#include "ed2k_endpoint.h"
#include "support/Network.h"
#include "prefs.h"
#include <limits>

namespace aria2::kad_command {

ed2k::KadTraversal* pendingTraversal(Ed2kAttribute& attrs,
                                     ed2k::KadTransactionPurpose purpose)
{
  switch (purpose) {
  case ed2k::KadTransactionPurpose::KEYWORD_LOOKUP:
    return attrs.kadKeywordTraversal.get();
  case ed2k::KadTransactionPurpose::SOURCE_LOOKUP:
    return attrs.kadSourceTraversal.get();
  default:
    return nullptr;
  }
}

ed2k::Endpoint toEndpoint(const ed2k::KadContact& contact)
{
  ed2k::Endpoint endpoint;
  endpoint.host = contact.host;
  endpoint.port = contact.udpPort;
  return endpoint;
}

uint16_t localEd2kTcpPort(const DownloadEngine* e)
{
  const auto port = e->getEd2kTcpPort();
  if (port != 0) {
    return port;
  }
  const auto configured = e->getOption()->getAsInt(PREF_ED2K_LISTEN_PORT);
  if (configured > 0 &&
      configured <= static_cast<int>(std::numeric_limits<uint16_t>::max())) {
    return static_cast<uint16_t>(configured);
  }
  return 0;
}

uint16_t localEd2kUdpPort(const DownloadEngine* e)
{
  const auto configured = e->getOption()->getAsInt(PREF_ED2K_UDP_LISTEN_PORT);
  if (configured > 0 &&
      configured <= static_cast<int>(std::numeric_limits<uint16_t>::max())) {
    return static_cast<uint16_t>(configured);
  }
  return 0;
}

uint32_t localKadUdpVerifyKey(const Ed2kAttribute* attrs,
                              const ed2k::Endpoint& endpoint)
{
  return attrs ? ed2k::createKadUdpVerifyKey(attrs->kadUdpVerifyKey,
                                             endpoint.host)
               : 0;
}

bool publishableAddress(const std::string& host)
{
  return !host.empty() && host != "0.0.0.0" && host != "127.0.0.1" &&
         host.compare(0, 4, "127.") != 0 && !util::inPrivateAddress(host);
}

uint32_t publicIpv4Value(const Ed2kAttribute* attrs)
{
  if (!attrs) {
    return 0;
  }
  for (const auto& host : attrs->kadObservedAddresses) {
    if (!publishableAddress(host)) {
      continue;
    }
    try {
      return ed2k::ipv4ToEndpointValue(host);
    }
    catch (DlAbortEx&) {
    }
  }
  for (const auto& server : attrs->serverStates) {
    if (!publishableAddress(server.ipAddress)) {
      continue;
    }
    try {
      return ed2k::ipv4ToEndpointValue(server.ipAddress);
    }
    catch (DlAbortEx&) {
    }
  }
  return 0;
}

uint8_t localDirectCallbackOptions()
{
  return ed2k::SOURCE_CRYPT_SUPPORT | ed2k::SOURCE_CRYPT_REQUEST;
}

std::vector<bool> localPartStatus(RequestGroup* group)
{
  std::vector<bool> status;
  if (!group || !group->getDownloadContext() || !group->getPieceStorage()) {
    return status;
  }
  status.resize(group->getDownloadContext()->getNumPieces());
  for (size_t i = 0; i < status.size(); ++i) {
    status[i] = group->getPieceStorage()->hasPiece(i);
  }
  return status;
}

} // namespace aria2::kad_command
