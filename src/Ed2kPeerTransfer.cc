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
#include "Ed2kPeerTransfer.h"

#include <algorithm>
#include <limits>
#include <vector>

#include "DiskAdaptor.h"
#include "DlRetryEx.h"
#include "DownloadContext.h"
#include "Ed2kAttribute.h"
#include "Piece.h"
#include "PiecedSegment.h"
#include "PieceStorage.h"
#include "Segment.h"
#include "SegmentMan.h"
#include "ed2k_hash.h"

namespace aria2 {

namespace ed2k {

namespace {

int64_t addReceivedRange(std::vector<PartRange>& ranges, int64_t begin,
                         int64_t end)
{
  int64_t newBytes = end - begin;
  for (const auto& range : ranges) {
    const auto overlapBegin = std::max(begin, range.begin);
    const auto overlapEnd = std::min(end, range.end);
    if (overlapEnd > overlapBegin) {
      newBytes -= overlapEnd - overlapBegin;
    }
  }

  PartRange merged{begin, end};
  for (auto i = ranges.begin(); i != ranges.end();) {
    if (i->end < merged.begin || merged.end < i->begin) {
      ++i;
      continue;
    }
    merged.begin = std::min(merged.begin, i->begin);
    merged.end = std::max(merged.end, i->end);
    i = ranges.erase(i);
  }
  ranges.push_back(merged);
  std::sort(ranges.begin(), ranges.end(),
            [](const PartRange& lhs, const PartRange& rhs) {
              return lhs.begin < rhs.begin;
            });
  return std::max<int64_t>(0, newBytes);
}

bool receivedRangeCovers(const std::vector<PartRange>& ranges, int64_t begin,
                         int64_t end)
{
  return std::any_of(ranges.begin(), ranges.end(),
                     [&](const PartRange& range) {
                       return range.begin <= begin && end <= range.end;
                     });
}

void eraseReceivedRange(std::vector<PartRange>& ranges, int64_t begin,
                        int64_t end)
{
  ranges.erase(std::remove_if(ranges.begin(), ranges.end(),
                              [&](const PartRange& range) {
                                return begin <= range.begin && range.end <= end;
                              }),
               ranges.end());
}

} // namespace

PeerTransfer::PeerTransfer(DownloadContext* dctx, PieceStorage* pieceStorage,
                           SegmentMan* segmentMan, cuid_t cuid)
    : dctx_(dctx),
      pieceStorage_(pieceStorage),
      segmentMan_(segmentMan),
      cuid_(cuid)
{
}

int64_t PeerTransfer::expectedPartLength(int64_t begin) const
{
  std::vector<std::shared_ptr<Segment>> segments;
  segmentMan_->getInFlightSegment(segments, cuid_);
  for (const auto& segment : segments) {
    if (segment->getPositionToWrite() == begin) {
      return std::min(begin + static_cast<int64_t>(BLOCK_LENGTH),
                      segment->getPosition() + segment->getLength()) -
             begin;
    }
  }
  return 0;
}

std::shared_ptr<Segment> PeerTransfer::getOrCreateSegment(size_t index) const
{
  std::vector<std::shared_ptr<Segment>> segments;
  segmentMan_->getInFlightSegment(segments, cuid_);
  for (const auto& segment : segments) {
    if (segment && segment->getIndex() == index) {
      return segment;
    }
  }

  auto segment = segmentMan_->getSegmentWithIndex(cuid_, index);
  if (segment) {
    return segment;
  }

  auto piece = pieceStorage_->getPiece(index);
  if (!piece || piece->pieceComplete()) {
    return nullptr;
  }
  return std::make_shared<PiecedSegment>(dctx_->getPieceLength(), piece);
}

std::shared_ptr<Segment>
PeerTransfer::writePartData(int64_t begin, const std::string& data)
{
  if (begin < 0 || data.empty() ||
      static_cast<uint64_t>(data.size()) >
          static_cast<uint64_t>(std::numeric_limits<int64_t>::max() -
                                begin)) {
    throw DL_RETRY_EX("Bad ED2K part range.");
  }
  const auto end = begin + static_cast<int64_t>(data.size());
  if (end > dctx_->getTotalLength()) {
    throw DL_RETRY_EX("Bad ED2K part range.");
  }

  const auto index = static_cast<size_t>(begin / dctx_->getPieceLength());
  const auto pieceBegin = static_cast<int64_t>(index) * dctx_->getPieceLength();
  const auto pieceEnd =
      std::min(pieceBegin + static_cast<int64_t>(dctx_->getPieceLength()),
               dctx_->getTotalLength());
  if (begin >= pieceBegin && end <= pieceEnd && pieceStorage_->hasPiece(index)) {
    return nullptr;
  }
  if (begin < pieceBegin || end > pieceEnd) {
    throw DL_RETRY_EX("Unexpected ED2K part range.");
  }

  auto segment = getOrCreateSegment(index);
  if (!segment) {
    throw DL_RETRY_EX("Unexpected ED2K part range.");
  }
  auto piece = segment->getPiece();
  auto attrs = getEd2kAttrs(dctx_);
  const auto blockLength = piece->getBlockLength();
  const auto firstBlock =
      static_cast<size_t>((begin - pieceBegin) / blockLength);
  const auto lastBlock =
      static_cast<size_t>((end - pieceBegin - 1) / blockLength);

  pieceStorage_->getDiskAdaptor()->writeData(
      reinterpret_cast<const unsigned char*>(data.data()), data.size(), begin);
  dctx_->updateDownload(
      static_cast<size_t>(addReceivedRange(attrs->receivedPartRanges, begin,
                                           end)));
  for (auto block = firstBlock; block <= lastBlock; ++block) {
    const auto blockBegin =
        pieceBegin + static_cast<int64_t>(block) * blockLength;
    const auto blockEnd =
        std::min(blockBegin + static_cast<int64_t>(piece->getBlockLength(block)),
                 pieceEnd);
    if (!piece->hasBlock(block) &&
        receivedRangeCovers(attrs->receivedPartRanges, blockBegin, blockEnd)) {
      piece->completeBlock(block);
    }
  }
  if (!piece->pieceComplete()) {
    return nullptr;
  }
  if (!verifyPiece(index)) {
    corruptPieceIndex_ = index;
    const auto pieceData = readPiece(index);
    segment->clear(pieceStorage_->getWrDiskCache());
    eraseReceivedRange(attrs->receivedPartRanges, pieceBegin, pieceEnd);
    if (!pieceData.empty()) {
      applyAichRecovery(piece, pieceData);
    }
    segmentMan_->cancelSegmentByIndex(index);
    throw DL_RETRY_EX("Bad ED2K piece hash.");
  }
  return segment;
}

bool PeerTransfer::applyAichRecovery(const std::shared_ptr<Piece>& piece,
                                     const std::string& data) const
{
  auto attrs = getEd2kAttrs(dctx_);
  if (!piece || attrs->aichRootHash.empty()) {
    return false;
  }

  auto recoverySet =
      std::find_if(attrs->aichRecoverySets.begin(),
                   attrs->aichRecoverySets.end(),
                   [&](const AichRecoverySet& item) {
                     return item.partIndex == piece->getIndex();
                   });
  if (recoverySet == attrs->aichRecoverySets.end()) {
    return false;
  }

  bool keptAny = false;
  for (const auto& block : recoverySet->blocks) {
    if (block.offset + block.length > data.size() ||
        aichHash(data.data() + block.offset, block.length) != block.hash) {
      continue;
    }
    const auto begin = block.offset / piece->getBlockLength();
    const auto end =
        (block.offset + block.length + piece->getBlockLength() - 1) /
        piece->getBlockLength();
    for (auto i = begin; i < end && i < piece->countBlock(); ++i) {
      const auto blockBegin = i * piece->getBlockLength();
      const auto blockEnd =
          blockBegin + static_cast<size_t>(piece->getBlockLength(i));
      if (blockBegin >= block.offset &&
          blockEnd <= block.offset + block.length) {
        piece->completeBlock(i);
        keptAny = true;
      }
    }
  }
  return keptAny;
}

bool PeerTransfer::completeVerifiedSegment(const std::shared_ptr<Segment>& segment)
{
  if (!segment) {
    return false;
  }
  if (!segmentMan_->completeSegment(cuid_, segment)) {
    return false;
  }
  const auto begin = static_cast<int64_t>(segment->getIndex()) *
                     dctx_->getPieceLength();
  const auto end = std::min(begin + static_cast<int64_t>(segment->getLength()),
                            dctx_->getTotalLength());
  eraseReceivedRange(getEd2kAttrs(dctx_)->receivedPartRanges, begin, end);
  return true;
}

std::string PeerTransfer::readPiece(size_t index) const
{
  auto piece = pieceStorage_->getPiece(index);
  if (!piece) {
    return std::string();
  }
  std::string data(piece->getLength(), '\0');
  auto nread = pieceStorage_->getDiskAdaptor()->readData(
      reinterpret_cast<unsigned char*>(&data[0]), data.size(),
      static_cast<int64_t>(index) * dctx_->getPieceLength());
  if (nread != static_cast<ssize_t>(data.size())) {
    return std::string();
  }
  return data;
}

bool PeerTransfer::verifyPiece(size_t index) const
{
  auto attrs = getEd2kAttrs(dctx_);
  if (attrs->pieceHashes.empty() && dctx_->getNumPieces() == 1) {
    attrs->pieceHashes.push_back(attrs->link.hash);
  }
  if (index >= attrs->pieceHashes.size()) {
    return false;
  }
  auto data = readPiece(index);
  return !data.empty() && md4Digest(data) == attrs->pieceHashes[index];
}

} // namespace ed2k

} // namespace aria2
