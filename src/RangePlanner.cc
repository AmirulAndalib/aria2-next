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
#include "RangePlanner.h"

#include <algorithm>
#include <limits>

namespace aria2 {

void RangePlanner::clear()
{
  totalLength_ = 0;
  chunkSize_ = 0;
  completed_.clear();
  ready_.clear();
  deferred_.clear();
}

void RangePlanner::restore(const std::vector<StoredRange>& ranges)
{
  completed_ = ranges;
  normalizeCompleted();
}

void RangePlanner::normalizeCompleted()
{
  completed_.erase(std::remove_if(completed_.begin(), completed_.end(),
                                  [](const StoredRange& range) {
                                    return range.first < 0 ||
                                           range.second <= range.first;
                                  }),
                   completed_.end());
  std::sort(completed_.begin(), completed_.end());
  std::vector<StoredRange> merged;
  for (const auto& range : completed_) {
    if (merged.empty() || merged.back().second < range.first) {
      merged.push_back(range);
    }
    else {
      merged.back().second = std::max(merged.back().second, range.second);
    }
  }
  completed_ = std::move(merged);
}

void RangePlanner::commit(int64_t begin, int64_t end)
{
  if (begin < 0 || end <= begin) {
    return;
  }
  if (totalLength_ > 0) {
    end = std::min(end, totalLength_);
  }
  if (end <= begin) {
    return;
  }
  auto position = std::lower_bound(completed_.begin(), completed_.end(), begin,
                                   [](const StoredRange& range, int64_t value) {
                                     return range.second < value;
                                   });
  while (position != completed_.end() && position->first <= end) {
    begin = std::min(begin, position->first);
    end = std::max(end, position->second);
    position = completed_.erase(position);
  }
  completed_.insert(position, {begin, end});
}

int64_t RangePlanner::completedLength() const
{
  int64_t result = 0;
  for (const auto& range : completed_) {
    const auto length = range.second - range.first;
    result = result > std::numeric_limits<int64_t>::max() - length
                 ? std::numeric_limits<int64_t>::max()
                 : result + length;
  }
  return result;
}

int64_t RangePlanner::contiguousLength() const
{
  return !completed_.empty() && completed_.front().first == 0
             ? completed_.front().second
             : 0;
}

int64_t RangePlanner::gapEnd(int64_t begin, int64_t proposedEnd) const
{
  for (const auto& range : completed_) {
    if (range.first > begin) {
      return std::min(proposedEnd, range.first);
    }
  }
  return proposedEnd;
}

bool RangePlanner::complete() const
{
  return totalLength_ > 0 && completed_.size() == 1 &&
         completed_.front().first == 0 &&
         completed_.front().second >= totalLength_;
}

void RangePlanner::enqueueGap(int64_t begin, int64_t end)
{
  while (begin < end) {
    const auto next = std::min(end, begin + chunkSize_);
    ready_.push_back({begin, next, 0, 0});
    begin = next;
  }
}

void RangePlanner::configure(int64_t totalLength, int64_t chunkSize,
                             const std::vector<RangeLease>& active)
{
  totalLength_ = std::max<int64_t>(0, totalLength);
  chunkSize_ = std::max<int64_t>(1, chunkSize);
  ready_.clear();
  deferred_.clear();
  if (totalLength_ <= 0) {
    return;
  }

  std::vector<StoredRange> covered = completed_;
  covered.reserve(covered.size() + active.size());
  for (const auto& lease : active) {
    if (!lease.empty()) {
      covered.emplace_back(std::max<int64_t>(0, lease.begin),
                           std::min(totalLength_, lease.end));
    }
  }
  std::sort(covered.begin(), covered.end());
  int64_t cursor = 0;
  for (const auto& range : covered) {
    if (range.second <= cursor) {
      continue;
    }
    if (range.first > cursor) {
      enqueueGap(cursor, std::min(range.first, totalLength_));
    }
    cursor = std::max(cursor, range.second);
    if (cursor >= totalLength_) {
      break;
    }
  }
  if (cursor < totalLength_) {
    enqueueGap(cursor, totalLength_);
  }
}

void RangePlanner::enqueue(RangeLease lease)
{
  if (!lease.empty()) {
    ready_.push_back(std::move(lease));
  }
}

void RangePlanner::defer(RangeLease lease, TimePoint readyAt)
{
  if (!lease.empty()) {
    deferred_.push_back({std::move(lease), readyAt});
  }
}

void RangePlanner::releaseDeferred(TimePoint now)
{
  for (auto it = deferred_.begin(); it != deferred_.end();) {
    if (it->readyAt <= now) {
      ready_.push_back(std::move(it->lease));
      it = deferred_.erase(it);
    }
    else {
      ++it;
    }
  }
}

std::optional<RangeLease> RangePlanner::takeReady(TimePoint now)
{
  releaseDeferred(now);
  if (ready_.empty()) {
    return std::nullopt;
  }
  auto lease = std::move(ready_.front());
  ready_.pop_front();
  return lease;
}

std::optional<RangePlanner::TimePoint> RangePlanner::nextDeadline() const
{
  if (deferred_.empty()) {
    return std::nullopt;
  }
  return std::min_element(
             deferred_.begin(), deferred_.end(),
             [](const DeferredLease& lhs, const DeferredLease& rhs) {
               return lhs.readyAt < rhs.readyAt;
             })
      ->readyAt;
}

bool RangePlanner::hasReady(TimePoint now) const
{
  return !ready_.empty() || std::any_of(deferred_.begin(), deferred_.end(),
                                        [now](const DeferredLease& entry) {
                                          return entry.readyAt <= now;
                                        });
}

void RangePlanner::splitAndEnqueue(RangeLease lease, size_t maxPieces,
                                   int64_t minimumPieceSize)
{
  if (lease.empty()) {
    return;
  }
  maxPieces = std::max<size_t>(1, maxPieces);
  minimumPieceSize = std::max<int64_t>(1, minimumPieceSize);
  const auto possible = static_cast<size_t>(
      std::max<int64_t>(1, lease.length() / minimumPieceSize));
  const auto pieces = std::min(maxPieces, possible);
  const auto target = std::max<int64_t>(
      minimumPieceSize, (lease.length() + static_cast<int64_t>(pieces) - 1) /
                            static_cast<int64_t>(pieces));
  auto begin = lease.begin;
  for (size_t index = 0; index < pieces && begin < lease.end; ++index) {
    const auto remainingPieces = static_cast<int64_t>(pieces - index);
    const auto remaining = lease.end - begin;
    const auto length =
        index + 1 == pieces
            ? remaining
            : std::max<int64_t>(
                  minimumPieceSize,
                  std::min(target, remaining - minimumPieceSize *
                                                   (remainingPieces - 1)));
    const auto end = std::min(lease.end, begin + length);
    ready_.push_back({begin, end, lease.attempts, lease.uriIndex});
    begin = end;
  }
}

} // namespace aria2
