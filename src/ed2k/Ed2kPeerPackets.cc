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
#include "DownloadFailureException.h"
#include "Ed2kAttribute.h"
#include "Ed2kCommand.h"
#include "GroupId.h"
#include "Log.h"
#include "ed2k_compression.h"
#include "ed2k_constants.h"
#include "ed2k_hash.h"
#include "ed2k_packet.h"
#include "ed2k_peer.h"
#include "error_code.h"
#include "fmt.h"
#include "prefs.h"
#include <algorithm>
#include <chrono>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>
#include "ed2k/Ed2kCommandInternal.h"

namespace aria2 {
using namespace ed2k_command;

void Ed2kCommand::handleEdonkeyPacket(Ed2kAttribute* attrs)
{
  switch (currentHeader_.opcode) {
  case ed2k::OP_KAD_FWTCPCHECK_ACK:
    return receiveFirewallCheck();
  case ed2k::OP_HELLO:
    return receivePeerHello(attrs);
  case ed2k::OP_HELLOANSWER:
    return receivePeerHelloAnswer(attrs);
  case ed2k::OP_REQFILENAMEANSWER:
    return receiveFileName(attrs);
  case ed2k::OP_REQUESTFILENAME:
    return answerFileName();
  case ed2k::OP_SETREQFILEID:
    return answerFileStatus();
  case ed2k::OP_FILESTATUS:
    return receiveFileStatus(attrs);
  case ed2k::OP_HASHSETANSWER:
    return receiveHashSet(attrs);
  case ed2k::OP_HASHSETREQUEST:
    return answerHashSet();
  case ed2k::OP_STARTUPLOADREQ:
    return receiveUploadRequest();
  case ed2k::OP_ACCEPTUPLOADREQ:
    return receiveUploadAcceptance(attrs);
  case ed2k::OP_OUTOFPARTREQS:
    return receiveOutOfParts(attrs);
  case ed2k::OP_FILEREQANSNOFIL:
    return receiveMissingFile(attrs);
  case ed2k::OP_CANCELTRANSFER:
    return receiveCancellation(attrs);
  case ed2k::OP_QUEUERANK:
  case ed2k::OP_QUEUERANKING:
    return receiveQueueRank(attrs);
  case ed2k::OP_SENDINGPART:
  case ed2k::OP_SENDINGPART_I64:
    return receivePart(attrs);
  case ed2k::OP_REQUESTPARTS:
    return answerParts();
  case ed2k::OP_REQUESTPARTS_I64:
    return answerLargeParts();
  case ed2k::OP_COMPRESSEDPART:
  case ed2k::OP_COMPRESSEDPART_I64:
    return receiveCompressedPart(attrs);
  default:
    return;
  }
}

void Ed2kCommand::receiveFirewallCheck()
{
  auto session = getDownloadEngine()->getRequestGroupMan()->getEd2kSession();
  bool expected = false;
  for (auto group : session->downloads()) {
    auto groupAttrs = getEd2kAttrs(group->getDownloadContext());
    if (groupAttrs &&
        consumeEd2kKadFirewallCheckHost(groupAttrs, endpoint_.host)) {
      expected = true;
    }
  }
  if (!expected) {
    throw DL_RETRY_EX("Unsolicited ED2K Kad firewall-check acknowledgement.");
  }
  for (auto group : session->downloads()) {
    auto groupAttrs = getEd2kAttrs(group->getDownloadContext());
    if (groupAttrs) {
      groupAttrs->kadFirewalled = false;
    }
  }
  state_ = State::DONE;
}

void Ed2kCommand::receivePeerHello(Ed2kAttribute* attrs)
{
  if (!updatePeerEndpointFromHello(true)) {
    return;
  }
  if (!ed2k::parsePeerHelloPayload(remotePeerInfo_, body_, true)) {
    throw DL_RETRY_EX("Bad ED2K peer hello.");
  }
  updatePeerUdpMetadata(attrs, endpoint_, remotePeerInfo_);
  queuePeerHelloAnswer();
  if (firewallCheck_) {
    queuePacket(ed2k::KAD_PROTOCOL, ed2k::OP_KAD_FWTCPCHECK_ACK, std::string());
    closeAfterOutbox_ = true;
  }
  else {
    queuePeerFileRequest();
  }
  state_ = State::WRITE;
}

void Ed2kCommand::receivePeerHelloAnswer(Ed2kAttribute* attrs)
{
  if (!updatePeerEndpointFromHello(false)) {
    return;
  }
  if (!ed2k::parsePeerHelloPayload(remotePeerInfo_, body_, false)) {
    throw DL_RETRY_EX("Bad ED2K peer hello answer.");
  }
  updatePeerUdpMetadata(attrs, endpoint_, remotePeerInfo_);
  if (firewallCheck_) {
    queuePacket(ed2k::KAD_PROTOCOL, ed2k::OP_KAD_FWTCPCHECK_ACK, std::string());
    closeAfterOutbox_ = true;
  }
  else {
    queuePeerFileRequest();
  }
  state_ = State::WRITE;
}

void Ed2kCommand::receiveFileName(Ed2kAttribute* attrs)
{
  if (body_.size() < ed2k::HASH_LENGTH ||
      body_.substr(0, ed2k::HASH_LENGTH) != attrs->link.hash) {
    throw DL_RETRY_EX("ED2K file answer hash mismatch.");
  }
  if (!peerFileStatusRequested_ && !peerFileStatusReceived_ &&
      getDownloadContext()->getTotalLength() > ed2k::PIECE_LENGTH) {
    queuePeerFileStatusRequest();
    state_ = State::WRITE;
  }
  else if (ed2k::hashSetPartCount(getDownloadContext()->getTotalLength()) > 0 &&
           attrs->pieceHashes.empty()) {
    queuePeerHashSetRequest();
    state_ = State::WRITE;
  }
  else {
    queueSourceExchangeRequest();
    queuePeerStartUpload();
    state_ = State::WRITE;
  }
}

void Ed2kCommand::answerFileName()
{
  if (body_.size() < ed2k::HASH_LENGTH) {
    throw DL_RETRY_EX("Bad ED2K file request.");
  }
  createSharedResponder().queueFileNameAnswer(
      body_.substr(0, ed2k::HASH_LENGTH));
  state_ = State::WRITE;
}

void Ed2kCommand::answerFileStatus()
{
  if (body_.size() != ed2k::HASH_LENGTH) {
    throw DL_RETRY_EX("Bad ED2K file status request.");
  }
  createSharedResponder().queueFileStatusAnswer(body_);
  state_ = State::WRITE;
}

void Ed2kCommand::receiveFileStatus(Ed2kAttribute* attrs)
{
  std::vector<bool> bitfield;
  if (!ed2k::parseFileStatusPayload(bitfield, body_, attrs->link.hash,
                                    getDownloadContext()->getNumPieces())) {
    throw DL_RETRY_EX("ED2K file status hash mismatch.");
  }
  updateEd2kPeerPartStatus(attrs, endpoint_, bitfield);
  peerFileStatusReceived_ = true;
  queuePeerPostFileStatusRequests();
  state_ = State::WRITE;
}

void Ed2kCommand::receiveHashSet(Ed2kAttribute* attrs)
{
  std::vector<std::string> pieceHashes;
  if (!ed2k::parseHashSetAnswerPayload(pieceHashes, body_, attrs->link.hash) ||
      pieceHashes.size() !=
          ed2k::hashSetPartCount(getDownloadContext()->getTotalLength()) ||
      ed2k::rootHash(pieceHashes) != attrs->link.hash) {
    throw DOWNLOAD_FAILURE_EXCEPTION2("Bad ED2K hash set.",
                                      error_code::CHECKSUM_ERROR);
  }
  attrs->pieceHashes = std::move(pieceHashes);
  queuePeerStartUpload();
  state_ = State::WRITE;
}

void Ed2kCommand::answerHashSet()
{
  if (body_.size() != ed2k::HASH_LENGTH) {
    throw DL_RETRY_EX("Bad ED2K hash set request.");
  }
  createSharedResponder().queueHashSetAnswer(body_);
  state_ = State::WRITE;
}

void Ed2kCommand::receiveUploadRequest()
{
  if (body_.size() != ed2k::HASH_LENGTH) {
    throw DL_RETRY_EX("Bad ED2K upload request.");
  }
  createSharedResponder().requestUploadSlot(
      body_, std::chrono::duration_cast<std::chrono::seconds>(
                 global::wallclock().getTime().time_since_epoch())
                 .count());
  state_ = State::WRITE;
}

void Ed2kCommand::receiveUploadAcceptance(Ed2kAttribute* attrs)
{
  peerAccepted_ = true;
  markEd2kPeerAccepted(attrs, endpoint_);
  A2_LOG_DEBUG(fmt("component=ed2k event=peer_download_ready gid=%s "
                   "cuid=%" PRId64 " endpoint=%s:%u",
                   GroupId::toHex(getRequestGroup()->getGID()).c_str(),
                   getCuid(), logging::sanitizeText(endpoint_.host).c_str(),
                   endpoint_.port));
  queuePeerPartRequest();
  state_ = State::WRITE;
}

void Ed2kCommand::receiveOutOfParts(Ed2kAttribute* attrs)
{
  markEd2kPeerOutOfParts(attrs, endpoint_, nowSeconds());
  if (switchToAlternativeDownload()) {
    return;
  }
  schedulePendingPeers();
  state_ = State::DONE;
}

void Ed2kCommand::receiveMissingFile(Ed2kAttribute* attrs)
{
  markEd2kPeerDead(
      attrs, endpoint_,
      std::chrono::duration_cast<std::chrono::seconds>(
          global::wallclock().getTime().time_since_epoch())
          .count(),
      std::max<int64_t>(1, getOption()->getAsInt(PREF_RETRY_WAIT)));
  if (switchToAlternativeDownload()) {
    return;
  }
  schedulePendingPeers();
  state_ = State::DONE;
}

void Ed2kCommand::receiveCancellation(Ed2kAttribute* attrs)
{
  markEd2kPeerCancelled(attrs, endpoint_);
  schedulePendingPeers();
  state_ = State::DONE;
}

void Ed2kCommand::receiveQueueRank(Ed2kAttribute* attrs)
{
  uint16_t rank = 0;
  if (!ed2k::parseQueueRankPayload(rank, body_)) {
    throw DL_RETRY_EX("Bad ED2K queue rank.");
  }
  auto peerState = getEd2kPeerState(attrs, endpoint_);
  const std::vector<bool> partStatus =
      peerState ? peerState->partStatus : std::vector<bool>();
  markEd2kPeerQueued(attrs, endpoint_, rank, partStatus);
  schedulePendingPeers();
  state_ = State::DONE;
}

void Ed2kCommand::receivePart(Ed2kAttribute* attrs)
{
  const bool is64 = currentHeader_.opcode == ed2k::OP_SENDINGPART_I64;
  const size_t metaLength = is64 ? 32 : 24;
  if (body_.size() < metaLength) {
    throw DL_RETRY_EX("Truncated ED2K part packet.");
  }
  auto hash = body_.substr(0, ed2k::HASH_LENGTH);
  if (hash != attrs->link.hash) {
    throw DL_RETRY_EX("ED2K part hash mismatch.");
  }
  const int64_t begin = is64 ? ed2k::readUInt64(body_.data() + 16)
                             : ed2k::readUInt32(body_.data() + 16);
  const int64_t end = is64 ? ed2k::readUInt64(body_.data() + 24)
                           : ed2k::readUInt32(body_.data() + 20);
  if (begin < 0 || end <= begin ||
      static_cast<size_t>(end - begin) != body_.size() - metaLength) {
    throw DL_RETRY_EX("Bad ED2K part range.");
  }
  const auto data = body_.substr(metaLength);
  if (!remotePeerInfo_.userHash.empty()) {
    auto rgman = getDownloadEngine()->getRequestGroupMan().get();
    if (rgman && rgman->getEd2kUploadQueue()) {
      rgman->getEd2kUploadQueue()->noteDownloaded(remotePeerInfo_.userHash,
                                                  data.size());
    }
  }
  handlePartData(begin, data);
  if (getRequestGroup()->downloadFinished()) {
    state_ = State::DONE;
  }
  else {
    queuePeerPartRequest();
    state_ = State::WRITE;
  }
}

void Ed2kCommand::answerParts()
{
  createSharedResponder().queuePartAnswers(body_, false);
  if (!outbox_.empty()) {
    state_ = State::WRITE;
  }
}

void Ed2kCommand::answerLargeParts()
{
  createSharedResponder().queuePartAnswers(body_, true);
  if (!outbox_.empty()) {
    state_ = State::WRITE;
  }
}

void Ed2kCommand::receiveCompressedPart(Ed2kAttribute* attrs)
{
  const bool is64 = currentHeader_.opcode == ed2k::OP_COMPRESSEDPART_I64;
  ed2k::CompressedPartHeader header;
  std::string compressedData;
  if (!ed2k::parseCompressedPartPayload(header, compressedData, body_,
                                        attrs->link.hash, is64)) {
    throw DL_RETRY_EX("Bad compressed ED2K part packet.");
  }
  const auto peerState = getEd2kPeerState(attrs, endpoint_);
  auto compressedState = findCompressedPartState(header.begin);
  if (!compressedState && peerState) {
    auto requested = std::find_if(
        peerState->requestedParts.begin(), peerState->requestedParts.end(),
        [&](const ed2k::PartRange& range) {
          return range.begin <= header.begin && header.begin < range.end;
        });
    if (requested != peerState->requestedParts.end()) {
      compressedState = getOrCreateCompressedPartState(*requested);
    }
  }
  if (!compressedState) {
    throw DL_RETRY_EX("Unexpected compressed ED2K part range.");
  }
  const auto writeBegin =
      compressedState->block.begin + compressedState->inflater.inflatedLength();
  if (writeBegin < compressedState->block.begin ||
      writeBegin >= compressedState->block.end) {
    throw DL_RETRY_EX("Unexpected compressed ED2K part range.");
  }
  const auto maxOutput = static_cast<size_t>(std::min<int64_t>(
      ed2k::BLOCK_LENGTH, compressedState->block.end - writeBegin));
  std::string data;
  if (!compressedState->inflater.inflateChunk(
          data, compressedData, compressedState->block.begin, maxOutput)) {
    throw DL_RETRY_EX("Bad compressed ED2K part data.");
  }
  if (!data.empty()) {
    handlePartData(writeBegin, data);
  }
  if (!compressedState->inflater.active()) {
    releaseCompletedCompressedPartState(compressedState->block);
  }
  if (getRequestGroup()->downloadFinished()) {
    state_ = State::DONE;
  }
  else {
    queuePeerPartRequest();
    state_ = State::WRITE;
  }
}

} // namespace aria2
