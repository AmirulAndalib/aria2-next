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
#include "Ed2kPeerTransfer.h"
#include "Log.h"
#include "Piece.h"
#include "Segment.h"
#include "SimpleRandomizer.h"
#include "a2functional.h"
#include "ed2k_constants.h"
#include "ed2k_hash.h"
#include "ed2k_peer.h"
#include "ed2k_policy.h"
#include "fmt.h"
#include "prefs.h"
#include <algorithm>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>
#include "ed2k/Ed2kCommandInternal.h"

namespace aria2 {
using namespace ed2k_command;

bool Ed2kCommand::downloadRateLimited() const
{
  if (mode_ != Mode::PEER || state_ != State::READ_BODY ||
      (currentHeader_.opcode != ed2k::OP_SENDINGPART &&
       currentHeader_.opcode != ed2k::OP_SENDINGPART_I64 &&
       currentHeader_.opcode != ed2k::OP_COMPRESSEDPART &&
       currentHeader_.opcode != ed2k::OP_COMPRESSEDPART_I64)) {
    return false;
  }
  const auto rgman = getDownloadEngine()->getRequestGroupMan().get();
  return (rgman && rgman->doesOverallDownloadSpeedExceed()) ||
         getRequestGroup()->doesDownloadSpeedExceed();
}

bool Ed2kCommand::uploadRateLimited() const
{
  if (outboxTransferData_.empty() || !outboxTransferData_.front()) {
    return false;
  }
  const auto rgman = getDownloadEngine()->getRequestGroupMan().get();
  return (rgman && rgman->doesOverallUploadSpeedExceed()) ||
         getRequestGroup()->doesUploadSpeedExceed();
}

void Ed2kCommand::resetCompressedPartInflaters()
{
  compressedPartStates_.clear();
}

Ed2kCommand::CompressedPartState*
Ed2kCommand::findCompressedPartState(int64_t begin)
{
  for (const auto& state : compressedPartStates_) {
    if (state && state->block.begin <= begin && begin < state->block.end) {
      return state.get();
    }
  }
  return nullptr;
}

Ed2kCommand::CompressedPartState*
Ed2kCommand::getOrCreateCompressedPartState(const ed2k::PartRange& block)
{
  if (block.end <= block.begin) {
    return nullptr;
  }
  if (auto state = findCompressedPartState(block.begin)) {
    return state;
  }
  auto state = make_unique<CompressedPartState>();
  state->block = block;
  compressedPartStates_.push_back(std::move(state));
  return compressedPartStates_.back().get();
}

void Ed2kCommand::releaseCompletedCompressedPartState(
    const ed2k::PartRange& block)
{
  compressedPartStates_.erase(
      std::remove_if(compressedPartStates_.begin(), compressedPartStates_.end(),
                     [&](const std::unique_ptr<CompressedPartState>& state) {
                       return !state || (state->block.begin == block.begin &&
                                         state->block.end == block.end);
                     }),
      compressedPartStates_.end());
}

void Ed2kCommand::queuePeerPartRequest()
{
  if (getRequestGroup()->downloadFinished()) {
    closeAfterOutbox_ = true;
    return;
  }
  std::vector<ed2k::PartRange> ranges;
  const auto attrs = getEd2kAttrs(getDownloadContext());
  auto state = getEd2kPeerState(attrs, endpoint_);
  const std::vector<ed2k::PartRange> outstanding =
      state ? state->requestedParts : std::vector<ed2k::PartRange>();

  if (outstanding.size() >= 3) {
    return;
  }
  const auto maxNewRanges = 3 - outstanding.size();

  std::vector<ed2k::PieceSelectionCandidate> candidates;
  const auto pieceLength = getDownloadContext()->getPieceLength();
  const auto continuingPiece =
      outstanding.empty()
          ? getDownloadContext()->getNumPieces()
          : static_cast<size_t>(outstanding.front().begin / pieceLength);
  for (size_t index = 0; index < getDownloadContext()->getNumPieces();
       ++index) {
    if (getPieceStorage()->hasPiece(index)) {
      continue;
    }
    if (state && !state->partStatus.empty() &&
        (index >= state->partStatus.size() || !state->partStatus[index])) {
      continue;
    }

    size_t frequency = 0;
    for (const auto& peer : attrs->peerStates) {
      if (index < peer.partStatus.size() && peer.partStatus[index]) {
        ++frequency;
      }
    }
    auto localPiece = getPieceStorage()->getPiece(index);
    size_t completedBlocks = 0;
    if (localPiece) {
      for (size_t block = 0; block < localPiece->countBlock(); ++block) {
        completedBlocks += localPiece->hasBlock(block) ? 1 : 0;
      }
    }
    const auto pieceBegin = static_cast<int64_t>(index) * pieceLength;
    const auto pieceEnd =
        std::min(pieceBegin + static_cast<int64_t>(pieceLength),
                 getDownloadContext()->getTotalLength());
    const auto requested = std::any_of(
        attrs->requestedPartRanges.begin(), attrs->requestedPartRanges.end(),
        [&](const ed2k::PartRange& range) {
          return range.begin < pieceEnd && range.end > pieceBegin;
        });
    bool preview = false;
    if (getRequestGroup()->getOption()->getAsBool(PREF_ED2K_PREVIEW_PRIORITY)) {
      const auto pieceCount = getDownloadContext()->getNumPieces();
      preview = index == 0 || index + 1 == pieceCount;
      if (!preview && index + 2 == pieceCount && pieceCount > 1) {
        const auto lastBegin =
            static_cast<int64_t>(pieceCount - 1) * pieceLength;
        preview = getDownloadContext()->getTotalLength() - lastBegin <
                  pieceLength / 3;
      }
    }
    ed2k::PieceSelectionCandidate candidate;
    candidate.index = index;
    candidate.frequency = frequency;
    candidate.completedBlocks = completedBlocks;
    candidate.totalBlocks =
        localPiece ? localPiece->countBlock()
                   : static_cast<size_t>(
                         (pieceEnd - pieceBegin + Piece::BLOCK_LENGTH - 1) /
                         Piece::BLOCK_LENGTH);
    candidate.requested = requested;
    candidate.preview = preview;
    candidate.continuing = index == continuingPiece;
    candidates.push_back(candidate);
  }
  std::shuffle(candidates.begin(), candidates.end(),
               *SimpleRandomizer::getInstance());
  const auto sourceCount = attrs->peerStates.size();
  std::stable_sort(candidates.begin(), candidates.end(),
                   [&](const auto& lhs, const auto& rhs) {
                     return ed2k::rankPieceSelection(lhs, sourceCount) <
                            ed2k::rankPieceSelection(rhs, sourceCount);
                   });

  for (const auto& candidate : candidates) {
    if (ranges.size() >= maxNewRanges) {
      break;
    }
    const auto index = candidate.index;

    const int64_t pieceBegin =
        static_cast<int64_t>(index) * getDownloadContext()->getPieceLength();
    const int64_t pieceEnd =
        std::min(pieceBegin + static_cast<int64_t>(
                                  getDownloadContext()->getPieceLength()),
                 getDownloadContext()->getTotalLength());
    auto piece = getPieceStorage()->getPiece(index);
    const auto pieceBlockLength = static_cast<int64_t>(Piece::BLOCK_LENGTH);
    const auto requestBlockLength =
        static_cast<int64_t>(ed2k::BLOCK_LENGTH / Piece::BLOCK_LENGTH) *
        pieceBlockLength;
    for (int64_t begin = pieceBegin;
         ranges.size() < maxNewRanges && begin < pieceEnd;) {
      const auto block =
          static_cast<size_t>((begin - pieceBegin) / pieceBlockLength);
      if (piece && piece->hasBlock(block)) {
        begin += pieceBlockLength;
        continue;
      }
      auto end = std::min(begin + requestBlockLength, pieceEnd);
      if (piece) {
        for (auto next = begin + pieceBlockLength; next < end;
             next += pieceBlockLength) {
          const auto nextBlock =
              static_cast<size_t>((next - pieceBegin) / pieceBlockLength);
          if (piece->hasBlock(nextBlock)) {
            end = next;
            break;
          }
        }
      }
      ed2k::PartRange range;
      range.begin = begin;
      range.end = end;
      if (!blockRangeAvailable(attrs, range) ||
          !blockRangeAvailable(ranges, range)) {
        begin += pieceBlockLength;
        continue;
      }
      ranges.push_back(range);
      begin = end;
    }
  }

  if (ranges.empty() && maxNewRanges > 0) {
    ed2k::PartRange reclaimed;
    const auto requesterPartStatus =
        state ? state->partStatus : std::vector<bool>();
    if (reclaimEd2kStalledRequestedRange(
            attrs, endpoint_, requesterPartStatus, nowSeconds(),
            ed2k::ENDGAME_RECLAIM_STALL_SECONDS, reclaimed) &&
        blockRangeAvailable(ranges, reclaimed)) {
      ranges.push_back(reclaimed);
      A2_LOG_TRACE(fmt("CUID#%" PRId64
                       " - Reclaimed stalled ED2K part request begin=%" PRId64
                       " end=%" PRId64 ".",
                       getCuid(), reclaimed.begin, reclaimed.end));
    }
  }

  if (ranges.empty()) {
    return;
  }

  for (const auto& range : ranges) {
    A2_LOG_TRACE(fmt("CUID#%" PRId64 " - Queue ED2K part request begin=%" PRId64
                     " end=%" PRId64 ".",
                     getCuid(), range.begin, range.end));
  }
  if (!state || state->requestedParts.empty()) {
    getSegmentMan()->getSegmentWithIndex(
        getCuid(), static_cast<size_t>(ranges.front().begin /
                                       getDownloadContext()->getPieceLength()));
  }
  auto requested = outstanding;
  requested.insert(requested.end(), ranges.begin(), ranges.end());
  updateEd2kPeerRequestedParts(attrs, endpoint_, requested, nowSeconds());
  const auto protocol =
      use64BitOffsets_ ? ed2k::PROTO_EMULE : ed2k::PROTO_EDONKEY;
  queuePacket(protocol,
              use64BitOffsets_ ? ed2k::OP_REQUESTPARTS_I64
                               : ed2k::OP_REQUESTPARTS,
              ed2k::createRequestPartsPayload(attrs->link.hash, ranges,
                                              use64BitOffsets_));
}

bool Ed2kCommand::queueActivePeerPartReclaim()
{
  if (mode_ != Mode::PEER || incoming_ || !peerAccepted_ ||
      getRequestGroup()->downloadFinished() || !outbox_.empty()) {
    return false;
  }
  if (tailReclaimTimer_.difference(global::wallclock()) < 10_s) {
    return false;
  }
  tailReclaimTimer_ = global::wallclock();

  const auto attrs = getEd2kAttrs(getDownloadContext());
  auto state = getEd2kPeerState(attrs, endpoint_);
  if (!state || !state->accepted || state->dead || state->cancelled ||
      state->noFile || state->outOfParts || state->remoteQueueFull ||
      state->udpReaskPending || state->requestedParts.size() >= 3) {
    return false;
  }

  ed2k::PartRange reclaimed;
  if (!activelyReclaimEd2kStalledRequestedRange(
          attrs, endpoint_, state->partStatus, nowSeconds(), reclaimed) ||
      !blockRangeAvailable(state->requestedParts, reclaimed)) {
    return false;
  }

  std::vector<ed2k::PartRange> ranges;
  ranges.push_back(reclaimed);
  if (state->requestedParts.empty()) {
    getSegmentMan()->getSegmentWithIndex(
        getCuid(), static_cast<size_t>(reclaimed.begin /
                                       getDownloadContext()->getPieceLength()));
  }
  auto requested = state->requestedParts;
  requested.push_back(reclaimed);
  updateEd2kPeerRequestedParts(attrs, endpoint_, requested, nowSeconds());
  const auto protocol =
      use64BitOffsets_ ? ed2k::PROTO_EMULE : ed2k::PROTO_EDONKEY;
  queuePacket(protocol,
              use64BitOffsets_ ? ed2k::OP_REQUESTPARTS_I64
                               : ed2k::OP_REQUESTPARTS,
              ed2k::createRequestPartsPayload(attrs->link.hash, ranges,
                                              use64BitOffsets_));
  A2_LOG_TRACE(
      fmt("CUID#%" PRId64
          " - Actively reclaimed stalled ED2K part request begin=%" PRId64
          " end=%" PRId64 ".",
          getCuid(), reclaimed.begin, reclaimed.end));
  state_ = State::WRITE;
  return true;
}

void Ed2kCommand::queueCancelTransfer()
{
  queuePacket(ed2k::PROTO_EDONKEY, ed2k::OP_CANCELTRANSFER, std::string());
}

bool Ed2kCommand::sendPendingCancelTransfer()
{
  if (mode_ != Mode::PEER || incoming_ || !peerAccepted_) {
    return false;
  }
  auto state = getEd2kPeerState(getEd2kAttrs(getDownloadContext()), endpoint_);
  if (!state || !state->cancelTransferSent || !state->requestedParts.empty()) {
    return false;
  }
  A2_LOG_TRACE(fmt("CUID#%" PRId64
                   " - Sending ED2K cancel transfer for reclaimed range.",
                   getCuid()));
  queueCancelTransfer();
  state_ = State::WRITE;
  peerAccepted_ = false;
  closeAfterOutbox_ = true;
  return true;
}

bool Ed2kCommand::expireStalledTransfer()
{
  if (mode_ != Mode::PEER || incoming_) {
    return false;
  }
  constexpr int64_t TRANSFER_TIMEOUT = 100;
  const auto retryWait =
      std::max<int64_t>(1, getOption()->getAsInt(PREF_RETRY_WAIT));
  if (!expireEd2kStalledPeerTransfer(
          getEd2kAttrs(getDownloadContext()), getSegmentMan().get(), endpoint_,
          getCuid(), nowSeconds(), TRANSFER_TIMEOUT, retryWait)) {
    return false;
  }
  queueCancelTransfer();
  state_ = State::WRITE;
  return true;
}

void Ed2kCommand::handlePartData(int64_t begin, const std::string& data)
{
  const auto end = begin + static_cast<int64_t>(data.size());
  const auto attrs = getEd2kAttrs(getDownloadContext());
  const auto peerState = getEd2kPeerState(attrs, endpoint_);
  const auto requested =
      peerState && std::any_of(peerState->requestedParts.begin(),
                               peerState->requestedParts.end(),
                               [begin, end](const ed2k::PartRange& range) {
                                 return range.begin <= begin &&
                                        end <= range.end;
                               });
  if (!requested) {
    throw DL_RETRY_EX("Unrequested ED2K part data.");
  }
  ed2k::PeerTransfer transfer(getDownloadContext().get(),
                              getPieceStorage().get(), getSegmentMan().get(),
                              getCuid());
  if (peerStat_) {
    peerStat_->updateDownload(data.size());
  }
  std::shared_ptr<Segment> completedSegment;
  try {
    completedSegment = transfer.writePartData(begin, data);
  }
  catch (DlRetryEx&) {
    if (!transfer.hasCorruptPiece()) {
      throw;
    }
    removeEd2kPeerCompletedRequestedRange(attrs, endpoint_, begin, end,
                                          nowSeconds());
    queueAichRecoveryRequest(transfer.corruptPieceIndex());
    return;
  }
  removeEd2kPeerCompletedRequestedRange(attrs, endpoint_, begin, end,
                                        nowSeconds());
  if (!completedSegment) {
    return;
  }
  transfer.completeVerifiedSegment(completedSegment);
  if (getRequestGroup()->downloadFinished()) {
    getRequestGroup()->enableSeedOnly();
  }
}

} // namespace aria2
