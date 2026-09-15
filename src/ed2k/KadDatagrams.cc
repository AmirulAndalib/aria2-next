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
#include "SocketCore.h"
#include "a2functional.h"
#include "a2netcompat.h"
#include "ed2k_kad.h"
#include "ed2k_kad_search.h"
#include "ed2k_link.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include "Ed2kKadCommand.h"
#include "ed2k/KadCommandSupport.h"
#include "DlRetryEx.h"
#include "DownloadContext.h"
#include "DownloadEngine.h"
#include "Ed2kAttribute.h"
#include "Ed2kSession.h"
#include "Log.h"
#include "RequestGroup.h"
#include "RequestGroupMan.h"
#include "SimpleRandomizer.h"
#include "ed2k_constants.h"
#include "ed2k_packet.h"
#include "ed2k_compression.h"
#include "ed2k_hash.h"
#include <array>

namespace aria2 {

using namespace kad_command;

namespace {
bool isKadProtocolDatagram(const std::string& datagram)
{
  ed2k::PacketHeader header;
  return ed2k::readDatagramHeader(header, datagram.data(), datagram.size()) &&
         (header.protocol == ed2k::KAD_PROTOCOL ||
          header.protocol == ed2k::KAD_PACKED_PROTOCOL) &&
         header.payloadSize() + 2 == datagram.size();
}

} // namespace

namespace {
std::string createKadDatagram(uint8_t opcode, const std::string& payload)
{
  return ed2k::createDatagram(ed2k::KAD_PROTOCOL, opcode, payload);
}

} // namespace

void Ed2kKadCommand::queuePacket(const ed2k::Endpoint& endpoint, uint8_t opcode,
                                 const std::string& payload)
{
  outbox_.push_back(std::make_pair(
      endpoint, ed2k::createDatagram(ed2k::KAD_PROTOCOL, opcode, payload)));
}

void Ed2kKadCommand::queueKadContactPacket(const ed2k::KadContact& contact,
                                           uint8_t opcode,
                                           const std::string& payload)
{
  auto datagram = createKadDatagram(opcode, payload);
  if (contact.version >= 6 && contact.id.size() == ed2k::HASH_LENGTH) {
    uint16_t randomKeyPart = 0;
    SimpleRandomizer::getInstance()->getRandomBytes(
        reinterpret_cast<unsigned char*>(&randomKeyPart),
        sizeof(randomKeyPart));
    datagram = ed2k::createKadObfuscatedDatagram(
        datagram, contact.id, randomKeyPart, contact.udpKey,
        localKadUdpVerifyKey(getEd2kAttrs(requestGroup_->getDownloadContext()),
                             toEndpoint(contact)));
  }
  outbox_.push_back(std::make_pair(toEndpoint(contact), datagram));
}

void Ed2kKadCommand::queueKadResponsePacket(
    const ed2k::Endpoint& endpoint, const ed2k::KadObfuscatedDatagram* context,
    uint8_t opcode, const std::string& payload)
{
  auto datagram = createKadDatagram(opcode, payload);
  if (context && context->senderVerifyKey != 0) {
    uint16_t randomKeyPart = 0;
    SimpleRandomizer::getInstance()->getRandomBytes(
        reinterpret_cast<unsigned char*>(&randomKeyPart),
        sizeof(randomKeyPart));
    datagram = ed2k::createKadObfuscatedDatagram(
        datagram, context->senderVerifyKey, context->receiverVerifyKey,
        randomKeyPart);
  }
  outbox_.push_back(std::make_pair(endpoint, datagram));
}

bool Ed2kKadCommand::tryDecodeKadObfuscatedDatagram(
    ed2k::KadObfuscatedDatagram& parsed, const ed2k::Endpoint& endpoint,
    const std::string& raw)
{
  if (ed2k::parseKadObfuscatedDatagram(
          parsed, raw,
          localKadUdpVerifyKey(
              getEd2kAttrs(requestGroup_->getDownloadContext()), endpoint)) &&
      isKadProtocolDatagram(parsed.datagram)) {
    return true;
  }

  auto attrs = getEd2kAttrs(requestGroup_->getDownloadContext());
  if (!attrs->kadRoutingTable) {
    return false;
  }
  if (ed2k::parseKadObfuscatedDatagram(
          parsed, raw, ed2k::ed2kHashToKadId(attrs->clientHash)) &&
      isKadProtocolDatagram(parsed.datagram)) {
    return true;
  }
  ed2k::KadContact contact;
  if (!attrs->kadRoutingTable->findByEndpoint(contact, endpoint) ||
      contact.id.size() != ed2k::HASH_LENGTH) {
    return false;
  }
  if (ed2k::parseKadObfuscatedDatagram(parsed, raw, contact.id) &&
      isKadProtocolDatagram(parsed.datagram)) {
    return true;
  }
  return contact.udpKey != 0 &&
         ed2k::parseKadObfuscatedDatagram(parsed, raw, contact.udpKey) &&
         isKadProtocolDatagram(parsed.datagram);
}

void Ed2kKadCommand::sendQueuedPackets()
{
  while (!outbox_.empty()) {
    auto item = outbox_.front();
    socket_->writeData(item.second.data(), item.second.size(), item.first.host,
                       item.first.port);
    outbox_.pop_front();
  }
}

void Ed2kKadCommand::receivePackets()
{
  std::array<unsigned char, 64_k> data;
  while (true) {
    Endpoint sender;
    ssize_t length = 0;
    try {
      length = socket_->readDataFrom(data.data(), data.size(), sender);
    }
    catch (DlRetryEx& e) {
      A2_LOG_DEBUG_EX("ED2K Kad UDP receive failed.", e);
      break;
    }
    if (length <= 0) {
      break;
    }
    if (length < 2) {
      continue;
    }
    ed2k::Endpoint endpoint;
    endpoint.host = sender.addr;
    endpoint.port = sender.port;
    std::string raw(reinterpret_cast<const char*>(data.data()),
                    reinterpret_cast<const char*>(data.data()) + length);
    const ed2k::KadObfuscatedDatagram* obfuscatedContext = nullptr;
    ed2k::KadObfuscatedDatagram parsed;
    std::string peerDatagram;
    std::string serverDatagram;
    if (tryDecodePeerObfuscatedDatagram(peerDatagram, endpoint, raw)) {
      raw.swap(peerDatagram);
      length = raw.size();
    }
    else if (tryDecodeServerObfuscatedDatagram(serverDatagram, endpoint, raw)) {
      raw.swap(serverDatagram);
      length = raw.size();
    }
    else if (tryDecodeKadObfuscatedDatagram(parsed, endpoint, raw)) {
      raw.swap(parsed.datagram);
      obfuscatedContext = &parsed;
      length = raw.size();
    }
    ed2k::PacketHeader header;
    if (!ed2k::readDatagramHeader(header, raw.data(),
                                  static_cast<size_t>(length)) ||
        (header.protocol != ed2k::KAD_PROTOCOL &&
         header.protocol != ed2k::KAD_PACKED_PROTOCOL &&
         header.protocol != ed2k::PROTO_EDONKEY &&
         header.protocol != ed2k::PROTO_EMULE &&
         header.protocol != ed2k::PROTO_PACKED) ||
        header.payloadSize() + 2 != static_cast<size_t>(length)) {
      continue;
    }
    std::string payload(raw.data() + 2, raw.data() + length);
    if (header.protocol == ed2k::PROTO_PACKED ||
        header.protocol == ed2k::KAD_PACKED_PROTOCOL) {
      std::string inflated;
      if (!ed2k::inflatePackedPacketPayload(inflated, payload, 64_k)) {
        continue;
      }
      header.protocol = header.protocol == ed2k::KAD_PACKED_PROTOCOL
                            ? ed2k::KAD_PROTOCOL
                            : ed2k::PROTO_EMULE;
      payload.swap(inflated);
    }
    if (header.protocol == ed2k::PROTO_EDONKEY ||
        header.protocol == ed2k::PROTO_EMULE) {
      handleEd2kUdpPacket(endpoint, header.opcode, payload);
    }
    else {
      RequestGroup* targetGroup = nullptr;
      if (header.opcode == ed2k::KAD_RES) {
        ed2k::KadResponse response;
        if (ed2k::parseKadResponsePayload(response, payload)) {
          targetGroup = findKadTargetGroup(response.targetId);
        }
      }
      else if (header.opcode == ed2k::KAD_SEARCH_RES) {
        ed2k::KadSearchResult result;
        if (ed2k::parseKadSearchResultPayload(result, payload)) {
          targetGroup = findKadTargetGroup(result.targetId);
        }
      }
      if (!targetGroup) {
        targetGroup =
            e_->getRequestGroupMan()->getEd2kSession()->networkDownload();
      }
      if (!targetGroup) {
        continue;
      }
      requestGroup_ = targetGroup;
      handlePacket(endpoint, obfuscatedContext, header.opcode, payload);
    }
  }
}

} // namespace aria2
