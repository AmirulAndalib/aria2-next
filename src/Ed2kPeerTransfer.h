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
#ifndef D_ED2K_PEER_TRANSFER_H
#define D_ED2K_PEER_TRANSFER_H

#include "common.h"
#include "Command.h"

#include <memory>
#include <string>

namespace aria2 {

class DownloadContext;
class Piece;
class PieceStorage;
class SegmentMan;
class Segment;

namespace ed2k {

class PeerTransfer {
private:
  DownloadContext* dctx_;
  PieceStorage* pieceStorage_;
  SegmentMan* segmentMan_;
  cuid_t cuid_;
  size_t corruptPieceIndex_ = static_cast<size_t>(-1);

  std::string readPiece(size_t index) const;
  bool verifyPiece(size_t index) const;
  bool applyAichRecovery(const std::shared_ptr<Piece>& piece,
                         const std::string& data) const;
  std::shared_ptr<Segment> getOrCreateSegment(size_t index) const;

public:
  PeerTransfer(DownloadContext* dctx, PieceStorage* pieceStorage,
               SegmentMan* segmentMan, cuid_t cuid);

  int64_t expectedPartLength(int64_t begin) const;
  std::shared_ptr<Segment> writePartData(int64_t begin,
                                         const std::string& data);
  bool completeVerifiedSegment(const std::shared_ptr<Segment>& segment);
  bool hasCorruptPiece() const
  {
    return corruptPieceIndex_ != static_cast<size_t>(-1);
  }
  size_t corruptPieceIndex() const { return corruptPieceIndex_; }
};

} // namespace ed2k

} // namespace aria2

#endif // D_ED2K_PEER_TRANSFER_H
