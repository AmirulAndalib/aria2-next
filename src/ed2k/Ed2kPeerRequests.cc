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
#include "Ed2kSharedResponder.h"
#include "Log.h"
#include "ed2k_aich.h"
#include "ed2k_constants.h"
#include "ed2k_hash.h"
#include "ed2k_packet.h"
#include "ed2k_peer.h"
#include "fmt.h"
#include <algorithm>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>
#include "support/Encoding.h"
#include "ed2k/Ed2kCommandInternal.h"

namespace aria2 {
using namespace ed2k_command;

void Ed2kCommand::queuePeerHello()
{
  auto attrs = getEd2kAttrs(getDownloadContext());
  auto payload = ed2k::createPeerHelloPayload(
      attrs->clientHash, localEd2kClientId(),
      localEd2kTcpPort(getDownloadEngine()), localEd2kServerEndpoint(),
      "aria2-next", localPeerInfo_, true);
  queuePacket(ed2k::PROTO_EDONKEY, ed2k::OP_HELLO, payload);
}

void Ed2kCommand::queuePeerHelloAnswer()
{
  auto attrs = getEd2kAttrs(getDownloadContext());
  auto payload = ed2k::createPeerHelloPayload(
      attrs->clientHash, localEd2kClientId(),
      localEd2kTcpPort(getDownloadEngine()), localEd2kServerEndpoint(),
      "aria2-next", localPeerInfo_, false);
  queuePacket(ed2k::PROTO_EDONKEY, ed2k::OP_HELLOANSWER, payload);
}

void Ed2kCommand::queueEmuleInfo(bool answer)
{
  queuePacket(ed2k::PROTO_EMULE,
              answer ? ed2k::OP_EMULEINFOANSWER : ed2k::OP_EMULEINFO,
              ed2k::createEmuleInfoPayload(localPeerInfo_));
}

void Ed2kCommand::queuePeerFileRequest()
{
  if (getRequestGroup()->downloadFinished()) {
    return;
  }
  if (peerFileRequestSent_) {
    return;
  }
  peerFileRequestSent_ = true;
  auto attrs = getEd2kAttrs(getDownloadContext());
  const auto localPartStatus = createLocalPartStatus(getDownloadContext().get(),
                                                     getPieceStorage().get());

  if (remotePeerInfo_.miscOptions.multiPacket) {
    const bool extendedMultipacket =
        remotePeerInfo_.miscOptions2.supportsExtendedMultipacket;
    queuePacket(ed2k::PROTO_EMULE,
                extendedMultipacket ? ed2k::OP_MULTIPACKET_EXT
                                    : ed2k::OP_MULTIPACKET,
                ed2k::createMultipacketFileRequestPayload(
                    attrs->link.hash, getDownloadContext()->getTotalLength(),
                    localPartStatus, remotePeerInfo_, extendedMultipacket));
    if (localPartStatus.size() > 1) {
      peerFileStatusRequested_ = true;
    }
    if (remotePeerInfo_.miscOptions2.supportsSourceExchange2 ||
        remotePeerInfo_.miscOptions.sourceExchange1Version > 1) {
      sourceExchangeRequested_ = true;
    }
    if (remotePeerInfo_.miscOptions.aichVersion > 0 &&
        attrs->aichRootHash.empty()) {
      aichFileHashRequested_ = true;
    }
    return;
  }

  queuePacket(ed2k::PROTO_EDONKEY, ed2k::OP_REQUESTFILENAME,
              createPeerFileRequestPayload(
                  getDownloadContext().get(), getPieceStorage().get(),
                  attrs->link.hash,
                  remotePeerInfo_.miscOptions.extendedRequestsVersion));
  if (localPartStatus.size() > 1) {
    queuePeerFileStatusRequest();
  }
  queueSourceExchangeRequest();
  queueAichFileHashRequest();
}

void Ed2kCommand::queuePeerFileStatusRequest()
{
  if (peerFileStatusRequested_) {
    return;
  }
  peerFileStatusRequested_ = true;
  queuePacket(ed2k::PROTO_EDONKEY, ed2k::OP_SETREQFILEID,
              getEd2kAttrs(getDownloadContext())->link.hash);
}

void Ed2kCommand::queuePeerHashSetRequest()
{
  queuePacket(ed2k::PROTO_EDONKEY, ed2k::OP_HASHSETREQUEST,
              getEd2kAttrs(getDownloadContext())->link.hash);
}

void Ed2kCommand::queuePeerPostFileStatusRequests()
{
  auto attrs = getEd2kAttrs(getDownloadContext());
  if (ed2k::hashSetPartCount(getDownloadContext()->getTotalLength()) > 0 &&
      attrs->pieceHashes.empty()) {
    queuePeerHashSetRequest();
  }
  else {
    queuePeerStartUpload();
  }
}

void Ed2kCommand::queueAichFileHashRequest()
{
  auto attrs = getEd2kAttrs(getDownloadContext());
  if (aichFileHashRequested_ || remotePeerInfo_.miscOptions.aichVersion == 0 ||
      !attrs->aichRootHash.empty()) {
    return;
  }
  aichFileHashRequested_ = true;
  queuePacket(ed2k::PROTO_EMULE, ed2k::OP_AICHFILEHASHREQ,
              ed2k::createAichFileHashRequestPayload(attrs->link.hash));
}

void Ed2kCommand::queueAichRecoveryRequest(size_t pieceIndex)
{
  auto attrs = getEd2kAttrs(getDownloadContext());
  if (remotePeerInfo_.miscOptions.aichVersion == 0 ||
      attrs->aichRootHash.empty() || !attrs->aichRootTrusted ||
      pieceIndex > std::numeric_limits<uint16_t>::max()) {
    return;
  }
  queuePacket(ed2k::PROTO_EMULE, ed2k::OP_AICHREQUEST,
              ed2k::createAichRequestPayload(attrs->link.hash,
                                             static_cast<uint16_t>(pieceIndex),
                                             attrs->aichRootHash));
}

void Ed2kCommand::queueSourceExchangeRequest()
{
  if (sourceExchangeRequested_ ||
      (remotePeerInfo_.miscOptions.sourceExchange1Version == 0 &&
       !remotePeerInfo_.miscOptions2.supportsSourceExchange2)) {
    return;
  }
  sourceExchangeRequested_ = true;
  const auto request = ed2k::createRequestSourcesPayload(
      getEd2kAttrs(getDownloadContext())->link.hash, remotePeerInfo_);
  if (request.opcode != 0) {
    queuePacket(ed2k::PROTO_EMULE, request.opcode, request.payload);
  }
}

void Ed2kCommand::queueSourceExchangeAnswer(uint8_t version)
{
  auto attrs = getEd2kAttrs(getDownloadContext());
  std::vector<ed2k::SourceExchangeEntry> entries;
  entries.reserve(std::min<size_t>(attrs->peers.size(), 500));
  for (const auto& peer : attrs->peers) {
    if (entries.size() >= 500) {
      break;
    }
    if (peer.host.empty() || peer.port == 0 ||
        (peer.host == endpoint_.host && peer.port == endpoint_.port)) {
      continue;
    }
    ed2k::SourceExchangeEntry entry;
    entry.endpoint = peer;
    entry.server.host = "0.0.0.0";
    entry.server.port = 0;
    entry.userHash = peer.userHash.empty()
                         ? std::string(ed2k::HASH_LENGTH, '\0')
                         : peer.userHash;
    entry.cryptOptions = static_cast<uint8_t>(peer.cryptOptions & 0xff);
    entries.push_back(std::move(entry));
  }
  if (entries.empty()) {
    return;
  }
  if (version >= 2) {
    queuePacket(
        ed2k::PROTO_EMULE, ed2k::OP_ANSWERSOURCES2,
        ed2k::createAnswerSources2Payload(attrs->link.hash, version, entries));
  }
  else {
    queuePacket(
        ed2k::PROTO_EMULE, ed2k::OP_ANSWERSOURCES,
        ed2k::createAnswerSourcesPayload(attrs->link.hash, version, entries));
  }
}

void Ed2kCommand::queuePeerStartUpload()
{
  queuePacket(ed2k::PROTO_EDONKEY, ed2k::OP_STARTUPLOADREQ,
              getEd2kAttrs(getDownloadContext())->link.hash);
}

ed2k::SharedResponder Ed2kCommand::createSharedResponder()
{
  auto rgman = getDownloadEngine()->getRequestGroupMan().get();
  auto uploadQueue = rgman ? rgman->getEd2kUploadQueue() : nullptr;
  return ed2k::SharedResponder(
      uploadQueue, rgman, endpoint_, remotePeerInfo_.userHash,
      [this](uint8_t protocol, uint8_t opcode, const std::string& payload) {
        queuePacket(protocol, opcode, payload);
      });
}

bool Ed2kCommand::updatePeerEndpointFromHello(bool helloPacket)
{
  const auto minimumSize = ed2k::HASH_LENGTH + 6 + (helloPacket ? 1 : 0);
  if (!incoming_ || body_.size() < minimumSize) {
    return true;
  }
  size_t offset = helloPacket ? 1 : 0;
  const auto userHash = body_.substr(offset, ed2k::HASH_LENGTH);
  offset += ed2k::HASH_LENGTH + 4;
  const auto listenPort = ed2k::readUInt16(body_.data() + offset);
  if (listenPort == 0 || listenPort == endpoint_.port) {
    auto state =
        getEd2kPeerState(getEd2kAttrs(getDownloadContext()), endpoint_);
    if (state && userHash.size() == ed2k::HASH_LENGTH) {
      state->endpoint.userHash = userHash;
    }
    return true;
  }

  auto attrs = getEd2kAttrs(getDownloadContext());
  auto oldEndpoint = endpoint_;
  ed2k::Endpoint newEndpoint = endpoint_;
  newEndpoint.port = listenPort;
  newEndpoint.userHash = userHash;
  auto findState = [&](const ed2k::Endpoint& peer) -> ed2k::PeerState* {
    auto i = std::find_if(attrs->peerStates.begin(), attrs->peerStates.end(),
                          [&](const ed2k::PeerState& state) {
                            return state.endpoint.host == peer.host &&
                                   state.endpoint.port == peer.port;
                          });
    return i == attrs->peerStates.end() ? nullptr : &*i;
  };
  auto oldState = findState(oldEndpoint);
  auto existingState = findState(newEndpoint);
  if (existingState && existingState != oldState &&
      (existingState->connecting || existingState->accepted)) {
    if (oldState) {
      oldState->connecting = false;
      oldState->accepted = false;
      releaseEd2kRequestedRanges(attrs, oldState->requestedParts);
      oldState->requestedParts.clear();
    }
    state_ = State::DONE;
    return false;
  }
  endpoint_ = newEndpoint;
  addEd2kPeer(attrs, endpoint_);
  auto newState = getEd2kPeerState(attrs, endpoint_);
  if (newState) {
    newState->connecting = true;
    newState->dead = false;
    newState->endpoint.userHash = userHash;
  }
  markEd2kDirectCallbackAccepted(attrs, endpoint_, nowSeconds());
  oldState = findState(oldEndpoint);
  if (oldState && oldState != newState) {
    oldState->connecting = false;
    oldState->accepted = false;
    releaseEd2kRequestedRanges(attrs, oldState->requestedParts);
    oldState->requestedParts.clear();
  }
  return true;
}

void Ed2kCommand::routeIncomingFileRequest()
{
  if (!incoming_ || body_.size() < ed2k::HASH_LENGTH) {
    return;
  }
  switch (currentHeader_.opcode) {
  case ed2k::OP_REQUESTFILENAME:
  case ed2k::OP_SETREQFILEID:
  case ed2k::OP_HASHSETREQUEST:
  case ed2k::OP_STARTUPLOADREQ:
  case ed2k::OP_REQUESTPARTS:
  case ed2k::OP_REQUESTPARTS_I64:
  case ed2k::OP_AICHFILEHASHREQ:
  case ed2k::OP_MULTIPACKET:
  case ed2k::OP_MULTIPACKET_EXT:
    break;
  default:
    return;
  }

  const auto fileHash = body_.substr(0, ed2k::HASH_LENGTH);
  auto currentAttrs = getEd2kAttrs(getDownloadContext());
  if (currentAttrs && currentAttrs->link.hash == fileHash) {
    return;
  }
  auto session = getDownloadEngine()->getRequestGroupMan()->getEd2kSession();
  auto target =
      std::find_if(session->downloads().begin(), session->downloads().end(),
                   [&](RequestGroup* group) {
                     auto attrs = getEd2kAttrs(group->getDownloadContext());
                     return attrs && !group->isHaltRequested() &&
                            attrs->link.hash == fileHash;
                   });
  if (target == session->downloads().end()) {
    return;
  }

  if (currentAttrs) {
    markEd2kPeerDisconnected(currentAttrs, endpoint_);
  }
  changeRequestGroup(*target);
  use64BitOffsets_ = getDownloadContext()->getTotalLength() >
                     std::numeric_limits<uint32_t>::max();
  auto attrs = getEd2kAttrs(getDownloadContext());
  addEd2kPeer(attrs, endpoint_, ed2k::PEER_SOURCE_INCOMING);
  markEd2kPeerConnecting(attrs, endpoint_);
  A2_LOG_TRACE(fmt("CUID#%" PRId64
                   " - Routed incoming ED2K peer %s:%u to file %s.",
                   getCuid(), endpoint_.host.c_str(), endpoint_.port,
                   util::toHex(fileHash).c_str()));
}

bool Ed2kCommand::switchToAlternativeDownload()
{
  if (mode_ != Mode::PEER || incoming_ || firewallCheck_) {
    return false;
  }
  auto session = getDownloadEngine()->getRequestGroupMan()->getEd2kSession();
  auto target = session->findAlternativeDownload(getRequestGroup(), endpoint_);
  if (!target) {
    return false;
  }

  auto oldAttrs = getEd2kAttrs(getDownloadContext());
  if (auto oldState = getEd2kPeerState(oldAttrs, endpoint_)) {
    releaseEd2kRequestedRanges(oldAttrs, oldState->requestedParts);
    oldState->requestedParts.clear();
    oldState->accepted = false;
    oldState->connecting = false;
  }
  changeRequestGroup(target);
  use64BitOffsets_ = getDownloadContext()->getTotalLength() >
                     std::numeric_limits<uint32_t>::max();
  peerFileStatusReceived_ = false;
  peerFileRequestSent_ = false;
  peerFileStatusRequested_ = false;
  peerAccepted_ = false;
  sourceExchangeRequested_ = false;
  aichFileHashRequested_ = false;
  resetCompressedPartInflaters();

  auto attrs = getEd2kAttrs(getDownloadContext());
  addEd2kPeer(attrs, endpoint_, ed2k::PEER_SOURCE_EXCHANGE);
  markEd2kPeerConnecting(attrs, endpoint_);
  queuePeerFileRequest();
  state_ = State::WRITE;
  A2_LOG_DEBUG(fmt("CUID#%" PRId64
                   " - Switched ED2K peer %s:%u to alternative file %s.",
                   getCuid(), endpoint_.host.c_str(), endpoint_.port,
                   util::toHex(attrs->link.hash).c_str()));
  return true;
}

} // namespace aria2
