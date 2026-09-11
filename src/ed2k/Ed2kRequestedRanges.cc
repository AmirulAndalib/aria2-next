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
#include "ed2k_hash.h"
#include "ed2k_link.h"
#include "ed2k_peer.h"
#include <cstddef>
#include <cstdint>
#include <vector>
#include "Ed2kAttribute.h"
#include "ed2k/Ed2kStateInternal.h"
#include "SegmentMan.h"
#include "ed2k_policy.h"
#include <algorithm>

namespace aria2 {

using ed2k_state::sameEndpoint;

bool updateEd2kPeerRequestedParts(Ed2kAttribute* attrs,
                                  const ed2k::Endpoint& peer,
                                  const std::vector<ed2k::PartRange>& ranges)
{
  return updateEd2kPeerRequestedParts(attrs, peer, ranges, 0);
}

bool updateEd2kPeerRequestedParts(Ed2kAttribute* attrs,
                                  const ed2k::Endpoint& peer,
                                  const std::vector<ed2k::PartRange>& ranges,
                                  int64_t now)
{
  auto state = getEd2kPeerState(attrs, peer);
  if (!state) {
    return false;
  }
  releaseEd2kRequestedRanges(attrs, state->requestedParts);
  state->requestedParts = ranges;
  markEd2kRequestedRanges(attrs, ranges);
  if (!ranges.empty() && now != 0) {
    state->lastPartRequestTime = now;
    state->lastTransferProgressTime = now;
    state->cancelTransferSent = false;
  }
  return true;
}

bool markEd2kRequestedRanges(Ed2kAttribute* attrs,
                             const std::vector<ed2k::PartRange>& ranges)
{
  if (!attrs) {
    return false;
  }
  attrs->requestedPartRanges.insert(attrs->requestedPartRanges.end(),
                                    ranges.begin(), ranges.end());
  return true;
}

void releaseEd2kRequestedRanges(Ed2kAttribute* attrs,
                                const std::vector<ed2k::PartRange>& ranges)
{
  if (!attrs || ranges.empty()) {
    return;
  }
  for (const auto& range : ranges) {
    auto i = std::find_if(
        attrs->requestedPartRanges.begin(), attrs->requestedPartRanges.end(),
        [&](const ed2k::PartRange& item) {
          return item.begin == range.begin && item.end == range.end;
        });
    if (i != attrs->requestedPartRanges.end()) {
      attrs->requestedPartRanges.erase(i);
    }
  }
}

size_t removeEd2kPeerCompletedRequestedRange(Ed2kAttribute* attrs,
                                             const ed2k::Endpoint& peer,
                                             int64_t begin, int64_t end,
                                             int64_t now)
{
  auto state = getEd2kPeerState(attrs, peer);
  if (!state || end <= begin) {
    return 0;
  }
  size_t updated = 0;
  std::vector<ed2k::PartRange> removedRanges;
  std::vector<ed2k::PartRange> addedRanges;
  std::vector<ed2k::PartRange> splitTails;
  for (auto& range : state->requestedParts) {
    if (end <= range.begin || begin >= range.end) {
      continue;
    }
    removedRanges.push_back(range);
    if (begin <= range.begin && end < range.end) {
      range.begin = end;
      addedRanges.push_back(range);
    }
    else if (begin > range.begin && end >= range.end) {
      range.end = begin;
      addedRanges.push_back(range);
    }
    else if (begin > range.begin && end < range.end) {
      ed2k::PartRange tail;
      tail.begin = end;
      tail.end = range.end;
      range.end = begin;
      addedRanges.push_back(range);
      splitTails.push_back(tail);
      addedRanges.push_back(tail);
    }
    else {
      range.begin = range.end = 0;
    }
    ++updated;
  }
  // Appending during traversal would invalidate the active vector iterators.
  state->requestedParts.insert(state->requestedParts.end(), splitTails.begin(),
                               splitTails.end());
  state->requestedParts.erase(std::remove_if(state->requestedParts.begin(),
                                             state->requestedParts.end(),
                                             [](const ed2k::PartRange& range) {
                                               return range.end <= range.begin;
                                             }),
                              state->requestedParts.end());
  releaseEd2kRequestedRanges(attrs, removedRanges);
  markEd2kRequestedRanges(attrs, addedRanges);
  if (updated != 0) {
    state->lastTransferProgressTime = now;
    state->cancelTransferSent = false;
  }
  return updated;
}

bool clearEd2kPeerRequestedParts(Ed2kAttribute* attrs,
                                 const ed2k::Endpoint& peer)
{
  auto state = getEd2kPeerState(attrs, peer);
  if (!state) {
    return false;
  }
  releaseEd2kRequestedRanges(attrs, state->requestedParts);
  state->requestedParts.clear();
  return true;
}

bool reclaimEd2kStalledRequestedRange(
    Ed2kAttribute* attrs, const ed2k::Endpoint& requester,
    const std::vector<bool>& requesterPartStatus, int64_t now,
    int64_t staleSeconds, ed2k::PartRange& reclaimed)
{
  if (!attrs || requester.host.empty() || requester.port == 0 ||
      staleSeconds <= 0) {
    return false;
  }
  const auto requesterState = getEd2kPeerState(attrs, requester);
  for (auto& state : attrs->peerStates) {
    if (sameEndpoint(state.endpoint, requester) ||
        state.requestedParts.empty() || state.cancelTransferSent) {
      continue;
    }
    const auto lastProgress = state.lastTransferProgressTime != 0
                                  ? state.lastTransferProgressTime
                                  : state.lastPartRequestTime;
    if (lastProgress == 0 || now - lastProgress < staleSeconds) {
      continue;
    }
    for (auto range = state.requestedParts.begin();
         range != state.requestedParts.end(); ++range) {
      const auto pieceIndex =
          static_cast<size_t>(range->begin / ed2k::PIECE_LENGTH);
      if (!requesterPartStatus.empty() &&
          (pieceIndex >= requesterPartStatus.size() ||
           !requesterPartStatus[pieceIndex])) {
        continue;
      }
      if (requesterState && std::any_of(requesterState->requestedParts.begin(),
                                        requesterState->requestedParts.end(),
                                        [&](const ed2k::PartRange& existing) {
                                          return existing.begin < range->end &&
                                                 range->begin < existing.end;
                                        })) {
        continue;
      }
      reclaimed = *range;
      releaseEd2kRequestedRanges(attrs, std::vector<ed2k::PartRange>{*range});
      state.requestedParts.erase(range);
      state.cancelTransferSent = true;
      return true;
    }
  }
  return false;
}

bool canReclaimEd2kStalledRequestedRange(
    Ed2kAttribute* attrs, const ed2k::Endpoint& requester,
    const std::vector<bool>& requesterPartStatus, int64_t now,
    int64_t staleSeconds)
{
  if (!attrs || requester.host.empty() || requester.port == 0 ||
      staleSeconds <= 0) {
    return false;
  }
  const auto requesterState = getEd2kPeerState(attrs, requester);
  for (const auto& state : attrs->peerStates) {
    if (sameEndpoint(state.endpoint, requester) ||
        state.requestedParts.empty() || state.cancelTransferSent) {
      continue;
    }
    const auto lastProgress = state.lastTransferProgressTime != 0
                                  ? state.lastTransferProgressTime
                                  : state.lastPartRequestTime;
    if (lastProgress == 0 || now - lastProgress < staleSeconds) {
      continue;
    }
    for (const auto& range : state.requestedParts) {
      const auto pieceIndex =
          static_cast<size_t>(range.begin / ed2k::PIECE_LENGTH);
      if (!requesterPartStatus.empty() &&
          (pieceIndex >= requesterPartStatus.size() ||
           !requesterPartStatus[pieceIndex])) {
        continue;
      }
      if (requesterState && std::any_of(requesterState->requestedParts.begin(),
                                        requesterState->requestedParts.end(),
                                        [&](const ed2k::PartRange& existing) {
                                          return existing.begin < range.end &&
                                                 range.begin < existing.end;
                                        })) {
        continue;
      }
      return true;
    }
  }
  return false;
}

bool activelyReclaimEd2kStalledRequestedRange(
    Ed2kAttribute* attrs, const ed2k::Endpoint& requester,
    const std::vector<bool>& requesterPartStatus, int64_t now,
    ed2k::PartRange& reclaimed)
{
  return reclaimEd2kStalledRequestedRange(
      attrs, requester, requesterPartStatus, now,
      ed2k::ACTIVE_ENDGAME_RECLAIM_STALL_SECONDS, reclaimed);
}

bool expireEd2kStalledPeerTransfer(Ed2kAttribute* attrs, SegmentMan* segmentMan,
                                   const ed2k::Endpoint& peer, int64_t cuid,
                                   int64_t now, int64_t timeoutSeconds,
                                   int64_t baseRetrySeconds)
{
  auto state = getEd2kPeerState(attrs, peer);
  if (!state || !segmentMan || timeoutSeconds <= 0 ||
      state->requestedParts.empty()) {
    return false;
  }
  const auto lastProgress = state->lastTransferProgressTime != 0
                                ? state->lastTransferProgressTime
                                : state->lastPartRequestTime;
  if (lastProgress == 0 || now - lastProgress < timeoutSeconds) {
    return false;
  }
  segmentMan->cancelSegment(cuid);
  releaseEd2kRequestedRanges(attrs, state->requestedParts);
  state->requestedParts.clear();
  state->cancelTransferSent = true;
  if (!markEd2kPeerFailure(attrs, peer, now, baseRetrySeconds)) {
    return false;
  }
  state = getEd2kPeerState(attrs, peer);
  state->queued = true;
  state->cancelTransferSent = true;
  return true;
}

} // namespace aria2
