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
#include "Ed2kKadCommand.h"

#include <algorithm>
#include <array>
#include <limits>
#include <memory>

#include "DlAbortEx.h"
#include "DlRetryEx.h"
#include "DownloadContext.h"
#include "DownloadEngine.h"
#include "Ed2kAttribute.h"
#include "Ed2kCommand.h"
#include "Ed2kShareIndex.h"
#include "Ed2kSession.h"
#include "Ed2kUploadQueue.h"
#include "Log.h"
#include "Option.h"
#include "PieceStorage.h"
#include "RequestGroup.h"
#include "RequestGroupMan.h"
#include "SimpleRandomizer.h"
#include "SocketCore.h"
#include "ed2k_constants.h"
#include "ed2k_compression.h"
#include "ed2k_hash.h"
#include "ed2k_crypto.h"
#include "ed2k_endpoint.h"
#include "ed2k_kad.h"
#include "ed2k_kad_search.h"
#include "ed2k_packet.h"
#include "ed2k_peer.h"
#include "ed2k_policy.h"
#include "ed2k_search.h"
#include "ed2k_server.h"
#include "fmt.h"
#include "prefs.h"
#include "util.h"
#include "wallclock.h"

namespace aria2 {

namespace {

ed2k::Endpoint toEndpoint(const ed2k::KadContact& contact)
{
  ed2k::Endpoint endpoint;
  endpoint.host = contact.host;
  endpoint.port = contact.udpPort;
  return endpoint;
}

std::string createKadDatagram(uint8_t opcode, const std::string& payload)
{
  return ed2k::createDatagram(ed2k::KAD_PROTOCOL, opcode, payload);
}

constexpr int64_t SERVER_STATUS_POLL_INTERVAL = 45;
constexpr int64_t FIREWALLED_CHECK_INTERVAL = 3600;
constexpr int64_t SOURCE_PUBLISH_INTERVAL = 1800;

bool isKadProtocolDatagram(const std::string& datagram)
{
  ed2k::PacketHeader header;
  return ed2k::readDatagramHeader(header, datagram.data(), datagram.size()) &&
         (header.protocol == ed2k::KAD_PROTOCOL ||
          header.protocol == ed2k::KAD_PACKED_PROTOCOL) &&
         header.payloadSize() + 2 == datagram.size();
}

int64_t peerRetryWait(const DownloadEngine* e)
{
  return std::max<int64_t>(1, e->getOption()->getAsInt(PREF_RETRY_WAIT));
}

uint32_t createChallenge()
{
  uint32_t challenge = 0;
  SimpleRandomizer::getInstance()->getRandomBytes(
      reinterpret_cast<unsigned char*>(&challenge), sizeof(challenge));
  return challenge == 0 ? 1 : challenge;
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

ed2k::Endpoint serverUdpEndpoint(const ed2k::Endpoint& server)
{
  ed2k::Endpoint endpoint;
  endpoint.host = server.host;
  endpoint.port = server.port + 4;
  return endpoint;
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

bool directKadTcpSourceType(uint8_t sourceType)
{
  return sourceType == 0 || sourceType == 1 || sourceType == 4;
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

} // namespace

Ed2kKadCommand::Ed2kKadCommand(cuid_t cuid, RequestGroup* requestGroup,
                               DownloadEngine* e)
    : Command(cuid),
      requestGroup_(requestGroup),
      e_(e),
      socket_(std::make_shared<SocketCore>(SOCK_DGRAM)),
      initialized_(false),
      lastServerStatusPoll_(0),
      bootstrapCursor_(0)
{
  setStatusRealtime();
  e_->getRequestGroupMan()->getEd2kSession()->registerDownload(requestGroup_);
  e_->setEd2kUdpActive(true);
}

Ed2kKadCommand::~Ed2kKadCommand()
{
  if (initialized_) {
    e_->deleteSocketForReadCheck(socket_, this);
  }
  e_->setEd2kUdpActive(false);
}

uint16_t Ed2kKadCommand::getLocalUdpPort() const
{
  return socket_->getAddrInfo().port;
}

bool Ed2kKadCommand::waitLocalUdpReadable(time_t timeout) const
{
  return socket_->isReadable(timeout);
}

int64_t Ed2kKadCommand::nowSeconds() const
{
  return std::chrono::duration_cast<std::chrono::seconds>(
             global::wallclock().getTime().time_since_epoch())
      .count();
}

RequestGroup*
Ed2kKadCommand::findKadTargetGroup(const std::string& targetId) const
{
  const auto session = e_->getRequestGroupMan()->getEd2kSession();
  for (auto group : session->downloads()) {
    auto attrs = getEd2kAttrs(group->getDownloadContext());
    if (!attrs || group->isHaltRequested()) {
      continue;
    }
    if (!attrs->link.hash.empty() &&
        ed2k::ed2kHashToKadId(attrs->link.hash) == targetId) {
      return group;
    }
    if (attrs->searchActive && !attrs->searchQuery.keyword.empty() &&
        ed2k::createKadKeywordTarget(attrs->searchQuery.keyword) == targetId) {
      return group;
    }
  }
  return nullptr;
}

RequestGroup* Ed2kKadCommand::findPeerGroup(
    const ed2k::Endpoint& endpoint, const std::string& userHash) const
{
  const auto session = e_->getRequestGroupMan()->getEd2kSession();
  for (auto group : session->downloads()) {
    auto attrs = getEd2kAttrs(group->getDownloadContext());
    if (!attrs || group->isHaltRequested()) {
      continue;
    }
    auto state = std::find_if(
        attrs->peerStates.begin(), attrs->peerStates.end(),
        [&](const ed2k::PeerState& peer) {
          const auto endpointMatches =
              peer.endpoint.host == endpoint.host &&
              (peer.endpoint.port == endpoint.port || peer.udpPort == endpoint.port);
          const auto hashMatches =
              !userHash.empty() && peer.endpoint.userHash == userHash;
          return endpointMatches || hashMatches;
        });
    if (state != attrs->peerStates.end()) {
      return group;
    }
  }
  return nullptr;
}

void Ed2kKadCommand::init()
{
  socket_->bind(nullptr, localEd2kUdpPort(e_), AF_INET);
  socket_->setNonBlockingMode();
  e_->addSocketForReadCheck(socket_, this);
  initialized_ = true;
  A2_LOG_DEBUG(fmt("IPv4 ED2K Kad: listening on UDP port %u",
                  socket_->getAddrInfo().port));

  auto attrs = getEd2kAttrs(requestGroup_->getDownloadContext());
  if (attrs->kadUdpVerifyKey == 0) {
    attrs->kadUdpVerifyKey = createEd2kKadUdpVerifyKey();
  }
  if (!attrs->kadRoutingTable) {
    attrs->kadRoutingTable =
        std::make_shared<ed2k::KadRoutingTable>(
            ed2k::ed2kHashToKadId(attrs->clientHash));
  }
  for (const auto& source : attrs->link.sources) {
    ed2k::Endpoint endpoint;
    endpoint.host = source.host;
    endpoint.port = source.port == 0 ? 4672 : source.port;
    attrs->kadRoutingTable->addRouterNode(endpoint);
  }
  queueBootstrap();
}

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
    const ed2k::Endpoint& endpoint,
    const ed2k::KadObfuscatedDatagram& context, uint8_t opcode,
    const std::string& payload)
{
  auto datagram = createKadDatagram(opcode, payload);
  if (context.senderVerifyKey != 0) {
    uint16_t randomKeyPart = 0;
    SimpleRandomizer::getInstance()->getRandomBytes(
        reinterpret_cast<unsigned char*>(&randomKeyPart),
        sizeof(randomKeyPart));
    datagram = ed2k::createKadObfuscatedDatagram(
        datagram, context.senderVerifyKey, context.receiverVerifyKey,
        randomKeyPart);
  }
  outbox_.push_back(std::make_pair(endpoint, datagram));
}

bool Ed2kKadCommand::tryDecodeKadObfuscatedDatagram(
    ed2k::KadObfuscatedDatagram& parsed, const ed2k::Endpoint& endpoint,
    const std::string& raw)
{
  if (ed2k::parseKadObfuscatedDatagram(
          parsed, raw, localKadUdpVerifyKey(
                           getEd2kAttrs(requestGroup_->getDownloadContext()),
                           endpoint)) &&
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
    auto encrypted = ed2k::encryptServerUdpDatagram(
        datagram, server.udpKey, randomKeyPart);
    if (!encrypted.empty()) {
      datagram.swap(encrypted);
      endpoint.port = server.udpObfuscationPort;
    }
  }
  outbox_.push_back(std::make_pair(endpoint, std::move(datagram)));
}

bool Ed2kKadCommand::findServerByUdpEndpoint(
    ed2k::Endpoint& server, const ed2k::Endpoint& endpoint) const
{
  const auto session = e_->getRequestGroupMan()->getEd2kSession();
  const auto group = session->networkDownload();
  const auto attrs = group ? getEd2kAttrs(group->getDownloadContext()) : nullptr;
  if (!attrs) {
    return false;
  }
  for (const auto& state : attrs->serverStates) {
    if (state.endpoint.host != endpoint.host) {
      continue;
    }
    const auto plainPort = state.endpoint.port <= 65531
                               ? static_cast<uint16_t>(state.endpoint.port + 4)
                               : 0;
    if (endpoint.port == plainPort ||
        (state.udpObfuscationPort != 0 &&
         endpoint.port == state.udpObfuscationPort)) {
      server = state.endpoint;
      return true;
    }
  }
  return false;
}

bool Ed2kKadCommand::tryDecodeServerObfuscatedDatagram(
    std::string& datagram, const ed2k::Endpoint& endpoint,
    const std::string& raw) const
{
  const auto session = e_->getRequestGroupMan()->getEd2kSession();
  const auto group = session->networkDownload();
  const auto attrs = group ? getEd2kAttrs(group->getDownloadContext()) : nullptr;
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
  const auto attrs = group ? getEd2kAttrs(group->getDownloadContext()) : nullptr;
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
    auto state = std::find_if(
        attrs->peerStates.begin(), attrs->peerStates.end(),
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

void Ed2kKadCommand::queueServerStatusPoll()
{
  const auto now = nowSeconds();
  if (lastServerStatusPoll_ != 0 &&
      now - lastServerStatusPoll_ < SERVER_STATUS_POLL_INTERVAL) {
    return;
  }
  bool queued = false;
  auto attrs = getEd2kAttrs(requestGroup_->getDownloadContext());
  for (const auto& server : attrs->servers) {
    auto state = getEd2kServerState(attrs, server);
    if (!state || !state->handshakeCompleted || server.port > 65531) {
      continue;
    }
    state->udpStatusChallenge = createChallenge();
    state->lastUdpStatusTime = now;
    queueServerUdpPacket(*state, ed2k::OP_GLOBSERVSTATREQ,
                         ed2k::packUInt32(state->udpStatusChallenge));
    queued = true;
  }
  if (queued) {
    lastServerStatusPoll_ = now;
  }
}

void Ed2kKadCommand::queueServerSourcePoll()
{
  const auto session = e_->getRequestGroupMan()->getEd2kSession();
  const auto networkGroup = session->networkDownload();
  auto networkAttrs =
      networkGroup ? getEd2kAttrs(networkGroup->getDownloadContext()) : nullptr;
  if (!networkAttrs || networkAttrs->servers.empty()) {
    return;
  }
  const auto now = nowSeconds();
  for (const auto& server : networkAttrs->servers) {
    auto networkState = getEd2kServerState(networkAttrs, server);
    if (!networkState || networkState->connected ||
        (server.port > 65531 && networkState->udpObfuscationPort == 0)) {
      continue;
    }
    const bool extGetSources =
        (networkState->udpFlags & ed2k::SRV_UDPFLG_EXT_GETSOURCES) != 0;
    const bool extGetSources2 =
        (networkState->udpFlags & ed2k::SRV_UDPFLG_EXT_GETSOURCES2) != 0;
    const size_t fileLimit = extGetSources || extGetSources2 ? 31 : 1;
    std::string payload;
    std::vector<Ed2kAttribute*> requested;
    for (auto group : session->downloads()) {
      auto attrs = getEd2kAttrs(group->getDownloadContext());
      auto state = attrs ? getEd2kServerState(attrs, server) : nullptr;
      if (!attrs || !state || group->downloadFinished() ||
          attrs->searchActive || attrs->link.hash.empty() ||
          !ed2k::serverUdpSourceRequestDue(*state, attrs->link.size, now)) {
        continue;
      }
      payload += ed2k::createGlobGetSourcesPayload(
          attrs->link.hash, attrs->link.size, extGetSources2);
      requested.push_back(attrs);
      if (requested.size() == fileLimit) {
        break;
      }
    }
    if (requested.empty()) {
      continue;
    }
    queueServerUdpPacket(*networkState,
                         extGetSources2 ? ed2k::OP_GLOBGETSOURCES2
                                        : ed2k::OP_GLOBGETSOURCES,
                         payload);
    for (auto attrs : requested) {
      markEd2kServerUdpSourceRequestSent(attrs, server, now);
    }
    const auto destinationPort =
        networkState->udpKey != 0 && networkState->udpObfuscationPort != 0
            ? networkState->udpObfuscationPort
            : static_cast<uint16_t>(server.port + 4);
    A2_LOG_TRACE(fmt("Queued ED2K UDP source request for %lu file(s) to "
                     "%s:%u.",
                     static_cast<unsigned long>(requested.size()),
                     server.host.c_str(), destinationPort));
  }
}

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
    queueKadContactPacket(
        contact, ed2k::KAD_REQ,
        ed2k::createKadRequestPayload(ed2k::KAD_FIND_NODE, targetId,
                                      contact.id));
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
        ed2k::createKadFirewalledRequestPayload(
            tcpPort, kadClientId, localDirectCallbackOptions()));
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
  auto observed = std::find_if(attrs->kadObservedAddresses.begin(),
                               attrs->kadObservedAddresses.end(),
                               publishableAddress);
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
                            ed2k::createKadRequestPayload(
                                searchType, traversal.targetId(),
                                action.contact.id));
      ed2k::KadTransaction tx;
      tx.endpoint = endpoint;
      tx.contact = action.contact;
      tx.purpose =
          traversal.kind() == ed2k::KadTraversalKind::KEYWORD_LOOKUP
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
      ed2k::KadTraversalKind::SOURCE_LOOKUP, kadFileId,
      attrs->link.size);
  queueTraversalActions(*attrs->kadSourceTraversal,
                        attrs->kadSourceTraversal->start(contacts));
  markEd2kKadSourceSearchStarted(attrs, now);
}

void Ed2kKadCommand::queueKeywordSearch()
{
  auto attrs = getEd2kAttrs(requestGroup_->getDownloadContext());
  if (!attrs->searchActive || !attrs->kadRoutingTable ||
      attrs->kadRoutingTable->liveSize() == 0 ||
      attrs->kadKeywordTraversal) {
    return;
  }
  const auto targetId = ed2k::createKadKeywordTarget(attrs->searchQuery.keyword);
  auto contacts = attrs->kadRoutingTable->findClosest(targetId, 8, true);
  if (contacts.empty()) {
    return;
  }
  attrs->kadKeywordTraversal = make_unique<ed2k::KadTraversal>(
      ed2k::KadTraversalKind::KEYWORD_LOOKUP, targetId, 0);
  queueTraversalActions(*attrs->kadKeywordTraversal,
                        attrs->kadKeywordTraversal->start(contacts));
}

size_t Ed2kKadCommand::queueDuePeerReasks(int64_t now)
{
  auto attrs = getEd2kAttrs(requestGroup_->getDownloadContext());
  size_t queued = 0;
  while (auto peer = selectDueEd2kUdpReaskPeer(attrs, now)) {
    ed2k::Endpoint endpoint = peer->endpoint;
    endpoint.port = peer->udpPort;
    std::string payload;
    if (peer->udpVersion > 3) {
      payload = ed2k::createUdpReaskFilePingPayload(
          attrs->link.hash, localPartStatus(requestGroup_), 0);
    }
    else if (peer->udpVersion > 2) {
      payload = ed2k::createUdpReaskFilePingPayload(attrs->link.hash, 0);
    }
    else {
      payload = attrs->link.hash;
    }
    queueEmuleUdpPacket(endpoint, ed2k::OP_REASKFILEPING, payload);
    markEd2kPeerUdpReaskSent(attrs, peer->endpoint, now);
    ++queued;
  }
  return queued;
}

size_t Ed2kKadCommand::queueDueKadCallbacks(int64_t now)
{
  auto attrs = getEd2kAttrs(requestGroup_->getDownloadContext());
  const auto tcpPort = localEd2kTcpPort(e_);
  if (!attrs || tcpPort == 0 || attrs->link.hash.empty()) {
    return 0;
  }
  constexpr int64_t CALLBACK_TIMEOUT = 45;
  size_t queued = 0;
  for (auto& state : attrs->peerStates) {
    if (!state.lowId || !state.callbackRequested ||
        state.lowIdCallbackState != ed2k::LowIdCallbackState::REQUESTED ||
        state.lastCallbackTime != 0) {
      continue;
    }
    if (state.callbackKind == ed2k::CallbackKind::DIRECT) {
      if (state.endpoint.host.empty() || state.udpPort == 0 ||
          state.endpoint.userHash.size() != ed2k::HASH_LENGTH) {
        continue;
      }
      ed2k::Endpoint endpoint;
      endpoint.host = state.endpoint.host;
      endpoint.port = state.udpPort;
      queueEmuleUdpPacket(endpoint, ed2k::OP_DIRECTCALLBACKREQ,
                          ed2k::createDirectCallbackRequestPayload(
                              tcpPort, attrs->clientHash,
                              localDirectCallbackOptions()));
      state.lastCallbackTime = now;
      state.callbackDeadline = now + CALLBACK_TIMEOUT;
      ++queued;
      A2_LOG_TRACE(fmt("Queued ED2K direct UDP callback request to %s:%u "
                       "for source TCP port %u.",
                       endpoint.host.c_str(), endpoint.port,
                       state.endpoint.port));
      continue;
    }
    if (state.callbackKind != ed2k::CallbackKind::BUDDY ||
        state.callbackBuddy.host.empty() || state.callbackBuddy.port == 0 ||
        state.callbackBuddyId.size() != ed2k::HASH_LENGTH) {
      continue;
    }
    queuePacket(state.callbackBuddy, ed2k::KAD_CALLBACK_REQ,
                ed2k::createKadCallbackRequestPayload(
                    state.callbackBuddyId,
                    ed2k::ed2kHashToKadId(attrs->link.hash), tcpPort));
    state.lastCallbackTime = now;
    state.callbackDeadline = now + CALLBACK_TIMEOUT;
    ++queued;
    A2_LOG_TRACE(fmt("Queued ED2K Kad callback request to buddy %s:%u "
                     "for source %s:%u.",
                     state.callbackBuddy.host.c_str(),
                     state.callbackBuddy.port, state.endpoint.host.c_str(),
                     state.endpoint.port));
  }
  return queued;
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
    std::unique_ptr<ed2k::KadObfuscatedDatagram> obfuscatedContext;
    ed2k::KadObfuscatedDatagram parsed;
    std::string peerDatagram;
    std::string serverDatagram;
    if (tryDecodePeerObfuscatedDatagram(peerDatagram, endpoint, raw)) {
      raw.swap(peerDatagram);
      length = raw.size();
      data.fill(0);
      std::copy(raw.begin(), raw.end(), data.begin());
    }
    else if (tryDecodeServerObfuscatedDatagram(serverDatagram, endpoint, raw)) {
      raw.swap(serverDatagram);
      length = raw.size();
      data.fill(0);
      std::copy(raw.begin(), raw.end(), data.begin());
    }
    else if (tryDecodeKadObfuscatedDatagram(parsed, endpoint, raw)) {
      raw.swap(parsed.datagram);
      obfuscatedContext.reset(new ed2k::KadObfuscatedDatagram(parsed));
      length = raw.size();
      data.fill(0);
      std::copy(raw.begin(), raw.end(), data.begin());
    }
    ed2k::PacketHeader header;
    if (!ed2k::readDatagramHeader(
            header, raw.data(), static_cast<size_t>(length)) ||
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
      handlePacket(endpoint, obfuscatedContext.get(), header.opcode, payload);
    }
  }
}

void Ed2kKadCommand::handleEd2kUdpPacket(const ed2k::Endpoint& endpoint,
                                         uint8_t opcode,
                                         const std::string& payload)
{
  auto session = e_->getRequestGroupMan()->getEd2kSession();
  if (opcode == ed2k::OP_REASKACK) {
    ed2k::UdpReaskAck ack;
    auto group = findPeerGroup(endpoint);
    if (group && ed2k::parseUdpReaskAckPayload(ack, payload)) {
      markEd2kPeerUdpReaskAck(
          getEd2kAttrs(group->getDownloadContext()), endpoint, ack.rank,
          ack.bitfield, nowSeconds());
    }
    return;
  }
  if (opcode == ed2k::OP_QUEUEFULL) {
    auto group = findPeerGroup(endpoint);
    if (group) {
      markEd2kPeerQueueFull(getEd2kAttrs(group->getDownloadContext()), endpoint,
                            nowSeconds(), peerRetryWait(e_));
    }
    return;
  }
  if (opcode == ed2k::OP_FILENOTFOUND) {
    auto group = findPeerGroup(endpoint);
    if (group) {
      markEd2kPeerDead(getEd2kAttrs(group->getDownloadContext()), endpoint,
                       nowSeconds(), peerRetryWait(e_));
    }
    return;
  }
  if (opcode == ed2k::OP_REASKFILEPING) {
    ed2k::UdpReask reask;
    if (!ed2k::parseUdpReaskFilePingPayload(reask, payload)) {
      return;
    }
    auto group = std::find_if(
        session->downloads().begin(), session->downloads().end(),
        [&](RequestGroup* candidate) {
          auto attrs = getEd2kAttrs(candidate->getDownloadContext());
          return attrs && attrs->link.hash == reask.fileHash;
        });
    if (group == session->downloads().end()) {
      queueEmuleUdpPacket(endpoint, ed2k::OP_FILENOTFOUND, std::string());
      return;
    }
    uint16_t rank = 0;
    auto rgman = e_->getRequestGroupMan().get();
    auto uploadQueue = rgman ? rgman->getEd2kUploadQueue() : nullptr;
    if (uploadQueue) {
      rank = uploadQueue->queueRank(endpoint);
    }
    if (rank == 0 && (!uploadQueue || !uploadQueue->isUploading(endpoint))) {
      queueEmuleUdpPacket(endpoint, ed2k::OP_QUEUEFULL, std::string());
      return;
    }
    const auto ackPayload =
        reask.partStatus.empty()
            ? ed2k::createUdpReaskAckPayload(rank)
            : ed2k::createUdpReaskAckPayload(localPartStatus(*group), rank);
    queueEmuleUdpPacket(endpoint, ed2k::OP_REASKACK, ackPayload);
    return;
  }
  if (opcode == ed2k::OP_DIRECTCALLBACKREQ) {
    ed2k::DirectCallbackRequest request;
    if (!ed2k::parseDirectCallbackRequestPayload(request, payload) ||
        request.tcpPort == 0 || request.userHash.empty()) {
      return;
    }
    ed2k::Endpoint peer;
    peer.host = endpoint.host;
    peer.port = request.tcpPort;
    peer.userHash = request.userHash;
    peer.cryptOptions = request.connectOptions;
    auto group = findPeerGroup(endpoint, request.userHash);
    if (!group) {
      return;
    }
    auto attrs = getEd2kAttrs(group->getDownloadContext());
    addEd2kPeer(attrs, peer, ed2k::PEER_SOURCE_INCOMING);
    e_->addCommand(
        make_unique<Ed2kCommand>(e_->newCUID(), group, e_, peer, false));
    A2_LOG_TRACE(fmt("Accepted ED2K direct UDP callback request from %s:%u "
                     "tcp=%u.",
                     endpoint.host.c_str(), endpoint.port, request.tcpPort));
    return;
  }
  if (opcode == ed2k::OP_GLOBFOUNDSOURCES) {
    ed2k::Endpoint server;
    const auto knownServer = findServerByUdpEndpoint(server, endpoint);
    for (auto group : session->downloads()) {
      auto attrs = getEd2kAttrs(group->getDownloadContext());
      std::vector<ed2k::FoundSource> sources;
      if (!attrs || !ed2k::parsePackedFoundSourcesPayloads(
                        sources, payload, attrs->link.hash)) {
        continue;
      }
      const auto added =
          mergeEd2kServerSources(attrs, sources, ed2k::PEER_SOURCE_SERVER);
      if (knownServer) {
        updateEd2kServerSourceResponse(attrs, server, sources.size(),
                                       nowSeconds());
      }
      if (added != 0) {
        A2_LOG_DEBUG(fmt("ED2K UDP server %s:%u returned %lu source(s).",
                        endpoint.host.c_str(), endpoint.port,
                        static_cast<unsigned long>(sources.size())));
        schedulePendingEd2kPeers(group, e_);
      }
      return;
    }
    return;
  }
  if (opcode == ed2k::OP_INVALID_LOWID) {
    if (payload.size() >= 4) {
      for (auto group : session->downloads()) {
        markEd2kCallbackFailed(getEd2kAttrs(group->getDownloadContext()),
                               ed2k::readUInt32(payload.data()));
      }
    }
    return;
  }
  if (opcode == ed2k::OP_GLOBCALLBACKREQ) {
    return;
  }
  if (opcode != ed2k::OP_GLOBSERVSTATRES) {
    return;
  }
  ed2k::Endpoint server;
  if (!findServerByUdpEndpoint(server, endpoint)) {
    return;
  }
  ed2k::ServerStatus status;
  if (!ed2k::parseServerUdpStatusPayload(status, payload) ||
      status.challenge == 0) {
    return;
  }
  bool challengeMatched = false;
  for (auto group : session->downloads()) {
    auto attrs = getEd2kAttrs(group->getDownloadContext());
    auto state = getEd2kServerState(attrs, server);
    if (!state) {
      continue;
    }
    if (status.challenge == state->udpStatusChallenge) {
      challengeMatched = true;
    }
  }
  if (!challengeMatched) {
    return;
  }
  for (auto group : session->downloads()) {
    auto attrs = getEd2kAttrs(group->getDownloadContext());
    if (getEd2kServerState(attrs, server)) {
      updateEd2kServerUdpStatus(attrs, server, status, nowSeconds());
    }
  }
}

void Ed2kKadCommand::handlePacket(const ed2k::Endpoint& endpoint,
                                  uint8_t opcode,
                                  const std::string& payload)
{
  handlePacket(endpoint, nullptr, opcode, payload);
}

void Ed2kKadCommand::handlePacket(
    const ed2k::Endpoint& endpoint,
    const ed2k::KadObfuscatedDatagram* context, uint8_t opcode,
    const std::string& payload)
{
  auto attrs = getEd2kAttrs(requestGroup_->getDownloadContext());
  if (!attrs->kadRoutingTable) {
    return;
  }
  if (opcode == ed2k::KAD_BOOTSTRAP_REQ) {
    std::string requesterId;
    if (payload.size() >= ed2k::HASH_LENGTH + 3) {
      requesterId.assign(payload.begin(), payload.begin() + ed2k::HASH_LENGTH);
    }
    const auto kadClientId = ed2k::ed2kHashToKadId(attrs->clientHash);
    const auto contacts =
        requesterId.empty()
            ? attrs->kadRoutingTable->findClosest(kadClientId, 20, false)
            : attrs->kadRoutingTable->findClosestExcluding(
                  kadClientId, requesterId, 20, false);
    auto response = ed2k::createKadBootstrapResponsePayload(
        kadClientId, localEd2kTcpPort(e_), 8, contacts);
    if (context) {
      queueKadResponsePacket(endpoint, *context, ed2k::KAD_BOOTSTRAP_RES,
                             response);
    }
    else {
      queuePacket(endpoint, ed2k::KAD_BOOTSTRAP_RES, response);
    }
    return;
  }
  if (opcode == ed2k::KAD_BOOTSTRAP_RES) {
    ed2k::KadBootstrapResponse response;
    if (!ed2k::parseKadBootstrapResponsePayload(response, payload)) {
      return;
    }
    ed2k::KadTransaction tx;
    attrs->kadTransactions.complete(endpoint, opcode, tx);
    ed2k::KadContact sender;
    sender.id = response.id;
    sender.host = endpoint.host;
    sender.udpPort = endpoint.port;
    sender.tcpPort = response.tcpPort;
    sender.version = response.version;
    sender.udpKey = context ? context->senderVerifyKey : 0;
    attrs->kadRoutingTable->nodeSeen(sender, nowSeconds());
    for (const auto& contact : response.contacts) {
      attrs->kadRoutingTable->heardAbout(contact, nowSeconds());
      if (contact.host == endpoint.host && contact.udpPort == endpoint.port) {
        continue;
      }
      queueKadContactPacket(contact, ed2k::KAD_HELLO_REQ,
                            ed2k::createKadHelloPayload(
                                ed2k::ed2kHashToKadId(attrs->clientHash),
                                localEd2kTcpPort(e_), 8));
    }
    return;
  }
  if (opcode == ed2k::KAD_HELLO_RES_ACK) {
    std::string contactId;
    ed2k::KadContact contact;
    const auto validReceiverKey =
        context && context->receiverVerifyKey ==
                       localKadUdpVerifyKey(attrs, endpoint);
    if (validReceiverKey &&
        ed2k::parseKadHelloAckPayload(contactId, payload) &&
        attrs->kadRoutingTable->findByEndpoint(contact, endpoint) &&
        contact.id == contactId) {
      attrs->kadRoutingTable->nodeSeen(contact, nowSeconds());
    }
    return;
  }
  if (opcode == ed2k::KAD_HELLO_REQ || opcode == ed2k::KAD_HELLO_RES) {
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
        context && context->receiverVerifyKey ==
                       localKadUdpVerifyKey(attrs, endpoint);
    if (validReceiverKey) {
      attrs->kadRoutingTable->nodeSeen(contact, nowSeconds());
    }
    else {
      attrs->kadRoutingTable->heardAbout(contact, nowSeconds());
    }
    if (opcode == ed2k::KAD_HELLO_REQ) {
      auto response = ed2k::createKadHelloPayload(
          ed2k::ed2kHashToKadId(attrs->clientHash), localEd2kTcpPort(e_), 8,
          hello.version >= 8 && !validReceiverKey, attrs->kadFirewalled, true);
      if (context) {
        queueKadResponsePacket(endpoint, *context, ed2k::KAD_HELLO_RES,
                               response);
      }
      else {
        queuePacket(endpoint, ed2k::KAD_HELLO_RES, response);
      }
    }
    else if (hello.requestsAck && context && context->senderVerifyKey != 0) {
      queueKadResponsePacket(
          endpoint, *context, ed2k::KAD_HELLO_RES_ACK,
          ed2k::createKadHelloAckPayload(
              ed2k::ed2kHashToKadId(attrs->clientHash)));
    }
    return;
  }
  if (opcode == ed2k::KAD_REQ) {
    ed2k::KadRequest request;
    if (!ed2k::parseKadRequestPayload(request, payload) ||
        request.receiverId != ed2k::ed2kHashToKadId(attrs->clientHash)) {
      return;
    }
    const auto searchType = request.searchType & 0x1f;
    if (searchType == 0) {
      return;
    }
    auto response = ed2k::createKadResponsePayload(
        request.targetId,
        attrs->kadRoutingTable->findClosest(request.targetId, 32, false));
    if (context) {
      queueKadResponsePacket(endpoint, *context, ed2k::KAD_RES, response);
    }
    else {
      queuePacket(endpoint, ed2k::KAD_RES, response);
    }
    return;
  }
  if (opcode == ed2k::KAD_RES) {
    ed2k::KadResponse response;
    if (!ed2k::parseKadResponsePayload(response, payload)) {
      return;
    }
    ed2k::KadTransaction tx;
    const auto knownResponse =
        attrs->kadTransactions.complete(endpoint, opcode, response.targetId,
                                        tx);
    if (!knownResponse) {
      return;
    }
    attrs->kadRoutingTable->nodeSeen(tx.contact, nowSeconds());
    for (const auto& contact : response.contacts) {
      attrs->kadRoutingTable->heardAbout(contact, nowSeconds());
    }
    if (tx.purpose == ed2k::KadTransactionPurpose::KEYWORD_LOOKUP &&
        attrs->kadKeywordTraversal) {
      queueTraversalActions(*attrs->kadKeywordTraversal,
                            attrs->kadKeywordTraversal->onResponse(
                                tx.contact, response.contacts));
    }
    else if (tx.purpose == ed2k::KadTransactionPurpose::SOURCE_LOOKUP &&
             attrs->kadSourceTraversal) {
      queueTraversalActions(*attrs->kadSourceTraversal,
                            attrs->kadSourceTraversal->onResponse(
                                tx.contact, response.contacts));
    }
    return;
  }
  if (opcode == ed2k::KAD_SEARCH_RES) {
    ed2k::KadSearchResult result;
    if (ed2k::parseKadSearchResultPayload(result, payload)) {
      auto attrs = getEd2kAttrs(requestGroup_->getDownloadContext());
      ed2k::KadTransaction tx;
      if (!attrs->kadTransactions.complete(endpoint, opcode, result.targetId,
                                           tx)) {
        return;
      }
      if (tx.purpose == ed2k::KadTransactionPurpose::KEYWORD_LOOKUP &&
          attrs->kadKeywordTraversal) {
        attrs->kadKeywordTraversal->onSearchResponse(tx.contact);
      }
      else if (tx.purpose == ed2k::KadTransactionPurpose::SOURCE_LOOKUP &&
               attrs->kadSourceTraversal) {
        attrs->kadSourceTraversal->onSearchResponse(tx.contact);
      }
      if (attrs->searchActive) {
        auto entries = ed2k::kadSearchEntriesToSearchResults(result.entries,
                                                            "kad");
        addEd2kSearchResults(attrs, entries, false);
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
        addEd2kKadSourcePeer(attrs, source, ed2k::PEER_SOURCE_KAD);
      }
      schedulePendingEd2kPeers(requestGroup_, e_);
    }
    return;
  }
  if (opcode == ed2k::KAD_FIREWALLED_RES) {
    ed2k::KadFirewalledResponse response;
    if (!ed2k::parseKadFirewalledResponsePayload(response, payload)) {
      return;
    }
    ed2k::KadTransaction tx;
    if (attrs->kadTransactions.complete(endpoint, opcode, tx)) {
      attrs->kadRoutingTable->nodeSeen(tx.contact, nowSeconds());
    }
    if (std::find(attrs->kadObservedAddresses.begin(),
                  attrs->kadObservedAddresses.end(),
                  response.ipAddress) == attrs->kadObservedAddresses.end()) {
      attrs->kadObservedAddresses.push_back(response.ipAddress);
    }
    return;
  }
  if (opcode == ed2k::KAD_PUBLISH_SOURCE_REQ) {
    ed2k::KadPublishSourceRequest request;
    if (!ed2k::parseKadPublishSourceRequestPayload(request, payload)) {
      return;
    }
    attrs->kadSourceIndex.store(request.fileId, request.source);
    auto response = ed2k::createKadPublishResultPayload(request.fileId, 1);
    if (context) {
      queueKadResponsePacket(endpoint, *context, ed2k::KAD_PUBLISH_RES,
                             response);
    }
    else {
      queuePacket(endpoint, ed2k::KAD_PUBLISH_RES, response);
    }
    return;
  }
  if (opcode == ed2k::KAD_SEARCH_SOURCES_REQ) {
    ed2k::KadSearchSourcesRequest request;
    if (!ed2k::parseKadSearchSourcesRequestPayload(request, payload)) {
      return;
    }
    auto entries = attrs->kadSourceIndex.find(request.targetId,
                                             request.startPosition, 50);
    if (!entries.empty()) {
      auto response = ed2k::createKadSearchResultPayload(
          ed2k::ed2kHashToKadId(attrs->clientHash), request.targetId,
          entries);
      if (context) {
        queueKadResponsePacket(endpoint, *context, ed2k::KAD_SEARCH_RES,
                               response);
      }
      else {
        queuePacket(endpoint, ed2k::KAD_SEARCH_RES, response);
      }
    }
    return;
  }
  if (opcode == ed2k::KAD_FIREWALLED_REQ) {
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
    e_->addCommand(make_unique<Ed2kCommand>(
        e_->newCUID(), requestGroup_, e_, peer, false, false, true));
    auto response = ed2k::createKadFirewalledResponsePayload(endpoint.host);
    if (context) {
      queueKadResponsePacket(endpoint, *context, ed2k::KAD_FIREWALLED_RES,
                             response);
    }
    else {
      queuePacket(endpoint, ed2k::KAD_FIREWALLED_RES, response);
    }
    return;
  }
  if (opcode == ed2k::KAD_PING) {
    if (context) {
      queueKadResponsePacket(endpoint, *context, ed2k::KAD_PONG,
                             std::string());
    }
    else {
      queuePacket(endpoint, ed2k::KAD_PONG, std::string());
    }
    return;
  }
}

bool Ed2kKadCommand::execute()
{
  auto session = e_->getRequestGroupMan()->getEd2kSession();
  session->detachStoppedDownloads();
  if (e_->isHaltRequested()) {
    session->detachAllDownloads();
    return true;
  }
  if (session->empty()) {
    return true;
  }
  requestGroup_ = session->networkDownload();
  try {
    if (!initialized_) {
      init();
    }
    receivePackets();
    const auto downloads = session->downloads();
    for (auto group : downloads) {
      if (!group || group->isHaltRequested()) {
        continue;
      }
      requestGroup_ = group;
      auto attrs = getEd2kAttrs(group->getDownloadContext());
      const auto expired = attrs->kadTransactions.expire(nowSeconds(), 12);
      if (attrs->kadRoutingTable) {
        for (const auto& tx : expired) {
          attrs->kadRoutingTable->nodeFailed(tx.contact);
          if (tx.expectedOpcode == ed2k::KAD_SEARCH_RES &&
              tx.purpose == ed2k::KadTransactionPurpose::KEYWORD_LOOKUP &&
              attrs->kadKeywordTraversal) {
            attrs->kadKeywordTraversal->onSearchFailure(tx.contact);
          }
          else if (tx.expectedOpcode == ed2k::KAD_SEARCH_RES &&
                   tx.purpose == ed2k::KadTransactionPurpose::SOURCE_LOOKUP &&
                   attrs->kadSourceTraversal) {
            attrs->kadSourceTraversal->onSearchFailure(tx.contact);
          }
          else if (tx.purpose == ed2k::KadTransactionPurpose::KEYWORD_LOOKUP &&
              attrs->kadKeywordTraversal) {
            queueTraversalActions(*attrs->kadKeywordTraversal,
                                  attrs->kadKeywordTraversal->onFailure(
                                      tx.contact));
          }
          else if (tx.purpose == ed2k::KadTransactionPurpose::SOURCE_LOOKUP &&
                   attrs->kadSourceTraversal) {
            queueTraversalActions(*attrs->kadSourceTraversal,
                                  attrs->kadSourceTraversal->onFailure(
                                      tx.contact));
          }
        }
      }
      schedulePendingEd2kServers(group, e_);
      queueDuePeerReasks(nowSeconds());
      queueDueKadCallbacks(nowSeconds());
      if (!group->downloadFinished()) {
        if (attrs->searchActive) {
          queueKeywordSearch();
        }
        else {
          queueSourceSearch();
        }
      }
    }

    requestGroup_ = session->networkDownload();
    if (auto uploadQueue = e_->getRequestGroupMan()->getEd2kUploadQueue()) {
      uploadQueue->maintain(nowSeconds(), e_->getRequestGroupMan().get());
    }
    queueServerStatusPoll();
    queueServerSourcePoll();
    queueBootstrap();
    queueRefresh();
    queueFirewalledCheck();
    queueSourcePublish();
    sendQueuedPackets();
    session->synchronizeNetworkState();
  }
  catch (DlAbortEx& e) {
    A2_LOG_DEBUG_EX("Exception thrown while handling ED2K Kad.", e);
  }
  e_->addRoutineCommand(std::unique_ptr<Command>(this));
  return false;
}

} // namespace aria2
