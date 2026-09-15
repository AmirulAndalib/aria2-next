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
#include <cstdint>
#include <utility>
#include "Ed2kKadCommand.h"
#include "ed2k_constants.h"
#include "ed2k/KadCommandSupport.h"
#include "DlAbortEx.h"
#include "DownloadContext.h"
#include "DownloadEngine.h"
#include "Ed2kAttribute.h"
#include "Ed2kSession.h"
#include "RequestGroup.h"
#include "RequestGroupMan.h"
#include "SimpleRandomizer.h"
#include "ed2k_crypto.h"
#include "ed2k_hash.h"
#include "ed2k_packet.h"
#include "ed2k_endpoint.h"
#include <algorithm>

namespace aria2 {

using namespace kad_command;

namespace {
ed2k::Endpoint serverUdpEndpoint(const ed2k::Endpoint& server)
{
  ed2k::Endpoint endpoint;
  endpoint.host = server.host;
  endpoint.port = server.port + 4;
  return endpoint;
}

} // namespace

void Ed2kKadCommand::queueServerUdpPacket(const ed2k::ServerState& server,
                                          uint8_t opcode,
                                          const std::string& payload)
{
  auto endpoint = serverUdpEndpoint(server.endpoint);
  auto datagram = ed2k::createDatagram(ed2k::PROTO_EDONKEY, opcode, payload);
  if (server.udpKey != 0 && server.udpObfuscationPort != 0) {
    uint16_t randomKeyPart = 0;
    SimpleRandomizer::getInstance()->getRandomBytes(
        reinterpret_cast<unsigned char*>(&randomKeyPart),
        sizeof(randomKeyPart));
    auto encrypted =
        ed2k::encryptServerUdpDatagram(datagram, server.udpKey, randomKeyPart);
    if (!encrypted.empty()) {
      datagram.swap(encrypted);
      endpoint.port = server.udpObfuscationPort;
    }
  }
  outbox_.push_back(std::make_pair(endpoint, std::move(datagram)));
}

bool Ed2kKadCommand::tryDecodeServerObfuscatedDatagram(
    std::string& datagram, const ed2k::Endpoint& endpoint,
    const std::string& raw) const
{
  const auto session = e_->getRequestGroupMan()->getEd2kSession();
  const auto group = session->networkDownload();
  const auto attrs =
      group ? getEd2kAttrs(group->getDownloadContext()) : nullptr;
  if (!attrs) {
    return false;
  }
  for (const auto& state : attrs->serverStates) {
    if (state.endpoint.host != endpoint.host || state.udpKey == 0 ||
        (state.udpObfuscationPort != 0 &&
         endpoint.port != state.udpObfuscationPort)) {
      continue;
    }
    if (ed2k::decryptServerUdpDatagram(datagram, raw, state.udpKey)) {
      return true;
    }
  }
  return false;
}

bool Ed2kKadCommand::tryDecodePeerObfuscatedDatagram(
    std::string& datagram, const ed2k::Endpoint& endpoint,
    const std::string& raw) const
{
  if (raw.empty() || (static_cast<uint8_t>(raw[0]) & 0x01) == 0) {
    return false;
  }
  const auto session = e_->getRequestGroupMan()->getEd2kSession();
  const auto group = session->networkDownload();
  const auto attrs =
      group ? getEd2kAttrs(group->getDownloadContext()) : nullptr;
  if (!attrs) {
    return false;
  }
  try {
    return ed2k::decryptPeerUdpDatagram(
        datagram, raw, attrs->clientHash,
        ed2k::ipv4ToEndpointValue(endpoint.host));
  }
  catch (DlAbortEx&) {
    return false;
  }
}

void Ed2kKadCommand::queueEmuleUdpPacket(const ed2k::Endpoint& endpoint,
                                         uint8_t opcode,
                                         const std::string& payload)
{
  auto datagram = ed2k::createDatagram(ed2k::PROTO_EMULE, opcode, payload);
  auto group = findPeerGroup(endpoint);
  auto attrs = group ? getEd2kAttrs(group->getDownloadContext()) : nullptr;
  const ed2k::PeerState* peerState = nullptr;
  if (attrs) {
    auto state =
        std::find_if(attrs->peerStates.begin(), attrs->peerStates.end(),
                     [&](const ed2k::PeerState& item) {
                       return item.endpoint.host == endpoint.host &&
                              (item.endpoint.port == endpoint.port ||
                               item.udpPort == endpoint.port);
                     });
    if (state != attrs->peerStates.end()) {
      peerState = &*state;
    }
  }
  const auto publicIp = publicIpv4Value(attrs);
  if (peerState && publicIp != 0 &&
      peerState->endpoint.userHash.size() == ed2k::HASH_LENGTH &&
      (peerState->endpoint.cryptOptions &
       (ed2k::SOURCE_CRYPT_SUPPORT | ed2k::SOURCE_CRYPT_REQUIRE)) != 0) {
    uint16_t randomKeyPart = 0;
    SimpleRandomizer::getInstance()->getRandomBytes(
        reinterpret_cast<unsigned char*>(&randomKeyPart),
        sizeof(randomKeyPart));
    auto encrypted = ed2k::encryptPeerUdpDatagram(
        datagram, peerState->endpoint.userHash, publicIp, randomKeyPart);
    if (!encrypted.empty()) {
      datagram.swap(encrypted);
    }
  }
  outbox_.push_back(std::make_pair(endpoint, std::move(datagram)));
}

} // namespace aria2
