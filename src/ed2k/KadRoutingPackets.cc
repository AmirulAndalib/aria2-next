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
#include <cstdint>
#include <string>
#include "Ed2kKadCommand.h"
#include "ed2k_constants.h"
#include "ed2k/KadCommandSupport.h"
#include "Ed2kAttribute.h"
#include "ed2k_hash.h"

namespace aria2 {

using namespace kad_command;

void Ed2kKadCommand::handleBootstrapRequest(
    Ed2kAttribute& attrs, const ed2k::Endpoint& endpoint,
    const ed2k::KadObfuscatedDatagram* context, const std::string& payload)
{
  std::string requesterId;
  if (payload.size() >= ed2k::HASH_LENGTH + 3) {
    requesterId.assign(payload.begin(), payload.begin() + ed2k::HASH_LENGTH);
  }
  const auto kadClientId = ed2k::ed2kHashToKadId(attrs.clientHash);
  const auto contacts =
      requesterId.empty()
          ? attrs.kadRoutingTable->findClosest(kadClientId, 20, false)
          : attrs.kadRoutingTable->findClosestExcluding(kadClientId,
                                                        requesterId, 20, false);
  auto response = ed2k::createKadBootstrapResponsePayload(
      kadClientId, localEd2kTcpPort(e_), 8, contacts);
  queueKadResponsePacket(endpoint, context, ed2k::KAD_BOOTSTRAP_RES, response);
}

void Ed2kKadCommand::handleBootstrapResponse(
    Ed2kAttribute& attrs, const ed2k::Endpoint& endpoint,
    const ed2k::KadObfuscatedDatagram* context, const std::string& payload)
{
  ed2k::KadBootstrapResponse response;
  if (!ed2k::parseKadBootstrapResponsePayload(response, payload)) {
    return;
  }
  ed2k::KadTransaction tx;
  attrs.kadTransactions.complete(endpoint, ed2k::KAD_BOOTSTRAP_RES, tx);
  ed2k::KadContact sender;
  sender.id = response.id;
  sender.host = endpoint.host;
  sender.udpPort = endpoint.port;
  sender.tcpPort = response.tcpPort;
  sender.version = response.version;
  sender.udpKey = context ? context->senderVerifyKey : 0;
  attrs.kadRoutingTable->nodeSeen(sender, nowSeconds());
  for (const auto& contact : response.contacts) {
    attrs.kadRoutingTable->heardAbout(contact, nowSeconds());
    if (contact.host == endpoint.host && contact.udpPort == endpoint.port) {
      continue;
    }
    queueKadContactPacket(
        contact, ed2k::KAD_HELLO_REQ,
        ed2k::createKadHelloPayload(ed2k::ed2kHashToKadId(attrs.clientHash),
                                    localEd2kTcpPort(e_), 8));
  }
}

void Ed2kKadCommand::handleHelloAck(Ed2kAttribute& attrs,
                                    const ed2k::Endpoint& endpoint,
                                    const ed2k::KadObfuscatedDatagram* context,
                                    const std::string& payload)
{
  std::string contactId;
  ed2k::KadContact contact;
  const auto validReceiverKey =
      context &&
      context->receiverVerifyKey == localKadUdpVerifyKey(&attrs, endpoint);
  if (validReceiverKey && ed2k::parseKadHelloAckPayload(contactId, payload) &&
      attrs.kadRoutingTable->findByEndpoint(contact, endpoint) &&
      contact.id == contactId) {
    attrs.kadRoutingTable->nodeSeen(contact, nowSeconds());
  }
}

void Ed2kKadCommand::handleHello(Ed2kAttribute& attrs,
                                 const ed2k::Endpoint& endpoint,
                                 const ed2k::KadObfuscatedDatagram* context,
                                 uint8_t opcode, const std::string& payload)
{
  ed2k::KadHello hello;
  if (!ed2k::parseKadHelloPayload(hello, payload)) {
    return;
  }
  ed2k::KadContact contact;
  contact.id = hello.id;
  contact.host = endpoint.host;
  contact.udpPort = endpoint.port;
  contact.tcpPort = hello.tcpPort;
  contact.version = hello.version;
  contact.udpKey = context ? context->senderVerifyKey : 0;
  const auto validReceiverKey =
      context &&
      context->receiverVerifyKey == localKadUdpVerifyKey(&attrs, endpoint);
  if (validReceiverKey) {
    attrs.kadRoutingTable->nodeSeen(contact, nowSeconds());
  }
  else {
    attrs.kadRoutingTable->heardAbout(contact, nowSeconds());
  }
  if (opcode == ed2k::KAD_HELLO_REQ) {
    auto response = ed2k::createKadHelloPayload(
        ed2k::ed2kHashToKadId(attrs.clientHash), localEd2kTcpPort(e_), 8,
        hello.version >= 8 && !validReceiverKey, attrs.kadFirewalled, true);
    queueKadResponsePacket(endpoint, context, ed2k::KAD_HELLO_RES, response);
  }
  else if (hello.requestsAck && context && context->senderVerifyKey != 0) {
    queueKadResponsePacket(endpoint, context, ed2k::KAD_HELLO_RES_ACK,
                           ed2k::createKadHelloAckPayload(
                               ed2k::ed2kHashToKadId(attrs.clientHash)));
  }
}

} // namespace aria2
