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
#include <iterator>
#include <limits>

namespace aria2 {

void RangePlanner::clear()
{
  totalLength_ = 0;
  chunkSize_ = 0;
  completed_.clear();
  ready_.clear();
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
    ready_.push_back({begin, next, 0});
    begin = next;
  }
}

void RangePlanner::configure(int64_t totalLength, int64_t chunkSize,
                             const std::vector<RangeLease>& active)
{
  totalLength_ = std::max<int64_t>(0, totalLength);
  chunkSize_ = std::max<int64_t>(1, chunkSize);
  ready_.clear();
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
  if (lease.empty()) {
    return;
  }
  const auto position =
      std::upper_bound(ready_.begin(), ready_.end(), lease.begin,
                       [](int64_t value, const RangeLease& entry) {
                         return value < entry.begin;
                       });
  ready_.insert(position, std::move(lease));
}

std::optional<RangeLease> RangePlanner::takeReady(TimePoint now)
{
  // Due retries precede fresh work. A delayed retry cannot occupy a slot or
  // hold up an unrelated range while its backoff is running.
  auto position =
      std::find_if(ready_.begin(), ready_.end(), [now](const auto& r) {
        return r.attempts > 0 && r.readyAt <= now;
      });
  if (position == ready_.end()) {
    position = std::find_if(ready_.begin(), ready_.end(),
                            [now](const auto& r) { return r.readyAt <= now; });
  }
  if (position == ready_.end()) {
    return std::nullopt;
  }
  auto lease = std::move(*position);
  ready_.erase(position);
  lease.readyAt = {};
  return lease;
}

bool RangePlanner::hasReady(TimePoint now) const
{
  return std::any_of(ready_.begin(), ready_.end(),
                     [now](const auto& r) { return r.readyAt <= now; });
}

std::optional<RangePlanner::TimePoint>
RangePlanner::nextDeadline(TimePoint now) const
{
  std::optional<TimePoint> next;
  for (const auto& range : ready_) {
    if (range.readyAt > now && (!next || range.readyAt < *next)) {
      next = range.readyAt;
    }
  }
  return next;
}

size_t RangePlanner::refillReady(size_t targetCount, int64_t preferredPieceSize,
                                 int64_t minimumPieceSize, TimePoint now)
{
  minimumPieceSize = std::max<int64_t>(1, minimumPieceSize);
  preferredPieceSize = std::max<int64_t>(minimumPieceSize, preferredPieceSize);
  auto eligible = static_cast<size_t>(
      std::count_if(ready_.begin(), ready_.end(),
                    [now](const auto& r) { return r.readyAt <= now; }));
  while (eligible < targetCount) {
    auto candidate = std::max_element(
        ready_.begin(), ready_.end(), [](const auto& lhs, const auto& rhs) {
          return (lhs.attempts == 0 ? lhs.length() : 0) <
                 (rhs.attempts == 0 ? rhs.length() : 0);
        });
    if (candidate == ready_.end() || candidate->attempts != 0 ||
        candidate->length() <= preferredPieceSize ||
        candidate->length() < minimumPieceSize * 2) {
      break;
    }
    const auto split =
        candidate->begin +
        (candidate->length() / 2 / minimumPieceSize) * minimumPieceSize;
    if (split <= candidate->begin || split >= candidate->end) {
      break;
    }
    auto suffix = *candidate;
    suffix.begin = split;
    candidate->end = split;
    ready_.insert(std::next(candidate), std::move(suffix));
    ++eligible;
  }
  return ready_.size();
}

} // namespace aria2
