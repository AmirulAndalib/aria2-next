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
#ifndef D_RANGE_PLANNER_H
#define D_RANGE_PLANNER_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <utility>
#include <vector>

namespace aria2 {

struct RangeLease {
  int64_t begin = 0;
  int64_t end = 0;
  size_t uriIndex = 0;

  bool empty() const { return begin >= end; }
  int64_t length() const { return end - begin; }

  RangeLease remainder(int64_t offset) const
  {
    auto result = *this;
    result.begin = std::clamp(offset, begin, end);
    return result;
  }
};

// Tracks which bytes are done and which ranges still need a request. Ranges
// carry no timing state: when a request may be issued is decided elsewhere.
class RangePlanner {
public:
  using StoredRange = std::pair<int64_t, int64_t>;

  void clear();
  void restore(const std::vector<StoredRange>& ranges);
  void configure(int64_t totalLength, int64_t chunkSize,
                 const std::vector<RangeLease>& active);
  void commit(int64_t begin, int64_t end);

  int64_t totalLength() const { return totalLength_; }
  int64_t completedLength() const;
  int64_t contiguousLength() const;
  int64_t gapEnd(int64_t begin, int64_t proposedEnd) const;
  bool complete() const;
  const std::vector<StoredRange>& completedRanges() const { return completed_; }

  // Ready ranges stay ordered by offset so the file fills front to back.
  void enqueue(RangeLease lease);
  std::optional<RangeLease> takeReady();
  bool hasReady() const { return !ready_.empty(); }
  size_t readyCount() const { return ready_.size(); }

  size_t refillReady(size_t targetCount, int64_t preferredPieceSize,
                     int64_t minimumPieceSize);

private:
  void normalizeCompleted();
  void enqueueGap(int64_t begin, int64_t end);

  int64_t totalLength_ = 0;
  int64_t chunkSize_ = 0;
  std::vector<StoredRange> completed_;
  std::deque<RangeLease> ready_;
};

} // namespace aria2

#endif // D_RANGE_PLANNER_H
