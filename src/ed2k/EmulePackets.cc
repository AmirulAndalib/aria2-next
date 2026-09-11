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
#include "Log.h"
#include "a2functional.h"
#include "ed2k_aich.h"
#include "ed2k_constants.h"
#include "ed2k_hash.h"
#include "ed2k_kad.h"
#include "ed2k_peer.h"
#include "fmt.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include "ed2k/Ed2kCommandInternal.h"

namespace aria2 {
using namespace ed2k_command;

void Ed2kCommand::handleEmulePacket(Ed2kAttribute* attrs)
{
  switch (currentHeader_.opcode) {
  case ed2k::OP_EMULEINFO:
    return receiveEmuleInfo(attrs);
  case ed2k::OP_EMULEINFOANSWER:
    return receiveEmuleInfoAnswer(attrs);
  case ed2k::OP_ANSWERSOURCES2:
    return receiveSourceExchangeV2(attrs);
  case ed2k::OP_ANSWERSOURCES:
    return receiveSourceExchange(attrs);
  case ed2k::OP_REQUESTSOURCES:
    return answerSourceExchange(attrs);
  case ed2k::OP_REQUESTSOURCES2:
    return answerSourceExchangeV2(attrs);
  case ed2k::OP_AICHFILEHASHREQ:
    return answerAichRoot();
  case ed2k::OP_AICHFILEHASHANS:
    return receiveAichRoot(attrs);
  case ed2k::OP_MULTIPACKETANSWER:
    return receiveMultipacket(attrs);
  case ed2k::OP_MULTIPACKET:
  case ed2k::OP_MULTIPACKET_EXT:
    return answerMultipacket();
  case ed2k::OP_AICHANSWER:
    return receiveAichRecovery(attrs);
  case ed2k::OP_CALLBACK:
    return receiveBuddyCallback(attrs);
  case ed2k::OP_AICHREQUEST:
    return answerAichRecovery();
  default:
    return;
  }
}

void Ed2kCommand::receiveEmuleInfo(Ed2kAttribute* attrs)
{
  if (ed2k::parseEmuleInfoPayload(remotePeerInfo_, body_)) {
    updatePeerUdpMetadata(attrs, endpoint_, remotePeerInfo_);
    queueEmuleInfo(true);
    state_ = State::WRITE;
  }
}

void Ed2kCommand::receiveEmuleInfoAnswer(Ed2kAttribute* attrs)
{
  if (!ed2k::parseEmuleInfoPayload(remotePeerInfo_, body_)) {
    throw DL_RETRY_EX("Bad eMule info answer.");
  }
  updatePeerUdpMetadata(attrs, endpoint_, remotePeerInfo_);
  if (!outbox_.empty()) {
    state_ = State::WRITE;
  }
}

void Ed2kCommand::receiveSourceExchangeV2(Ed2kAttribute* attrs)
{
  ed2k::SourceExchangeAnswer answer;
  if (!ed2k::parseAnswerSources2Payload(answer, body_, attrs->link.hash)) {
    throw DL_RETRY_EX("Bad ED2K source exchange answer.");
  }
  mergeEd2kSourceExchangePeers(attrs, answer.entries, endpoint_);
  schedulePendingPeers();
}

void Ed2kCommand::receiveSourceExchange(Ed2kAttribute* attrs)
{
  ed2k::SourceExchangeAnswer answer;
  const auto version = remotePeerInfo_.miscOptions.sourceExchange1Version;
  if (!ed2k::parseAnswerSourcesPayload(answer, body_, attrs->link.hash,
                                       version)) {
    throw DL_RETRY_EX("Bad ED2K source exchange answer.");
  }
  mergeEd2kSourceExchangePeers(attrs, answer.entries, endpoint_);
  schedulePendingPeers();
}

void Ed2kCommand::answerSourceExchange(Ed2kAttribute* attrs)
{
  if (body_ == attrs->link.hash) {
    queueSourceExchangeAnswer(std::max<uint8_t>(
        1, remotePeerInfo_.miscOptions.sourceExchange1Version));
    if (!outbox_.empty()) {
      state_ = State::WRITE;
    }
  }
  else if (createSharedResponder().queueSourceExchangeAnswer(
               body_,
               std::max<uint8_t>(
                   1, remotePeerInfo_.miscOptions.sourceExchange1Version))) {
    if (!outbox_.empty()) {
      state_ = State::WRITE;
    }
  }
}

void Ed2kCommand::answerSourceExchangeV2(Ed2kAttribute* attrs)
{
  uint8_t version = 0;
  if (ed2k::parseRequestSources2Payload(version, body_, attrs->link.hash)) {
    queueSourceExchangeAnswer(version);
    if (!outbox_.empty()) {
      state_ = State::WRITE;
    }
  }
  else if (body_.size() >= ed2k::HASH_LENGTH + 3 &&
           ed2k::parseRequestSources2Payload(
               version, body_, body_.substr(3, ed2k::HASH_LENGTH)) &&
           createSharedResponder().queueSourceExchangeAnswer(
               body_.substr(3, ed2k::HASH_LENGTH), version)) {
    if (!outbox_.empty()) {
      state_ = State::WRITE;
    }
  }
  else {
    throw DL_RETRY_EX("Bad ED2K source exchange request.");
  }
}

void Ed2kCommand::answerAichRoot()
{
  if (body_.size() != ed2k::HASH_LENGTH ||
      !createSharedResponder().queueAichFileHashAnswer(body_)) {
    return;
  }
  state_ = State::WRITE;
}

void Ed2kCommand::receiveAichRoot(Ed2kAttribute* attrs)
{
  ed2k::AichFileHashAnswer answer;
  if (!ed2k::parseAichFileHashAnswerPayload(answer, body_, attrs->link.hash)) {
    throw DL_RETRY_EX("Bad ED2K AICH file hash answer.");
  }
  recordEd2kAichHashVote(attrs, answer.rootHash, endpoint_.host);
}

void Ed2kCommand::receiveMultipacket(Ed2kAttribute* attrs)
{
  ed2k::MultipacketAnswer answer;
  if (!ed2k::parseMultipacketAnswerPayload(answer, body_, attrs->link.hash)) {
    throw DL_RETRY_EX("Bad ED2K multipacket answer.");
  }
  if (answer.hasFileStatus) {
    if (answer.completeSource) {
      answer.partStatus.assign(getDownloadContext()->getNumPieces(), true);
    }
    updateEd2kPeerPartStatus(attrs, endpoint_, answer.partStatus);
    peerFileStatusReceived_ = true;
  }
  if (answer.hasAichRootHash) {
    recordEd2kAichHashVote(attrs, answer.aichRootHash, endpoint_.host);
  }
  if (answer.hasFileStatus) {
    queuePeerPostFileStatusRequests();
    state_ = State::WRITE;
  }
  else if (answer.hasFileName &&
           getDownloadContext()->getTotalLength() <= ed2k::PIECE_LENGTH) {
    queuePeerPostFileStatusRequests();
    state_ = State::WRITE;
  }
}

void Ed2kCommand::answerMultipacket()
{
  if (createSharedResponder().queueMultipacketAnswer(
          body_, currentHeader_.opcode == ed2k::OP_MULTIPACKET_EXT,
          remotePeerInfo_.miscOptions.extendedRequestsVersion,
          remotePeerInfo_.miscOptions.sourceExchange1Version)) {
    state_ = State::WRITE;
  }
}

void Ed2kCommand::receiveAichRecovery(Ed2kAttribute* attrs)
{
  ed2k::AichAnswer answer;
  if (!ed2k::parseAichAnswerPayload(answer, body_, attrs->link.hash)) {
    throw DL_RETRY_EX("Bad ED2K AICH answer.");
  }
  if (!answer.failed) {
    if (!attrs->aichRootTrusted) {
      throw DL_RETRY_EX("Untrusted ED2K AICH recovery root.");
    }
    if (answer.rootHash != attrs->aichRootHash) {
      throw DL_RETRY_EX("Bad ED2K AICH recovery root.");
    }
    const auto partSize = std::min<int64_t>(
        ed2k::PIECE_LENGTH,
        getDownloadContext()->getTotalLength() -
            static_cast<int64_t>(answer.partIndex) * ed2k::PIECE_LENGTH);
    ed2k::AichRecoveryData recovery;
    ed2k::AichRecoverySet recoverySet;
    if (partSize <= 0 ||
        !ed2k::parseAichRecoveryData(recovery, answer.recoveryData,
                                     static_cast<size_t>(partSize),
                                     use64BitOffsets_) ||
        !ed2k::buildAichRecoverySet(
            recoverySet, recovery, attrs->aichRootHash,
            static_cast<size_t>(getDownloadContext()->getTotalLength()),
            answer.partIndex)) {
      throw DL_RETRY_EX("Bad ED2K AICH recovery data.");
    }
    storeAichRecoverySet(attrs, recoverySet);
  }
}

void Ed2kCommand::receiveBuddyCallback(Ed2kAttribute* attrs)
{
  ed2k::BuddyCallback callback;
  if (!ed2k::parseBuddyCallbackPayload(callback, body_) ||
      callback.buddyId != ed2k::ed2kHashToKadId(attrs->clientHash) ||
      callback.fileId != ed2k::ed2kHashToKadId(attrs->link.hash)) {
    return;
  }
  addEd2kPeer(attrs, callback.endpoint, ed2k::PEER_SOURCE_KAD);
  auto e = getDownloadEngine();
  e->addCommand(make_unique<Ed2kCommand>(e->newCUID(), getRequestGroup(), e,
                                         callback.endpoint, false));
  A2_LOG_TRACE(fmt("Accepted ED2K buddy callback for %s:%u.",
                   callback.endpoint.host.c_str(), callback.endpoint.port));
}

void Ed2kCommand::answerAichRecovery()
{
  if (body_.size() >= ed2k::HASH_LENGTH) {
    createSharedResponder().queueAichAnswer(body_.substr(0, ed2k::HASH_LENGTH),
                                            body_);
    if (!outbox_.empty()) {
      state_ = State::WRITE;
    }
  }
}

} // namespace aria2
