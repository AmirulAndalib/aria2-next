/* <!-- copyright */
/*
 * aria2 - The high speed download utility
 *
 * Copyright (C) 2026 The aria2-next contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
/* copyright --> */
#include "BtDiscovery.h"

#include <algorithm>

namespace aria2 {

void BtDiscoveryController::activate(BtDiscoveryState& state,
                                     uint64_t networkEpoch)
{
  state.epoch = ++nextEpoch_;
  state.networkEpoch = networkEpoch;
  state.phase = BtDiscoveryPhase::Discovering;
  state.trackerRetryUsed = false;
  state.trackerPeersReceived = 0;
  state.retryTracker.clear();
}

bool BtDiscoveryController::requestTrackerRetry(BtDiscoveryState& state,
                                                bool transient,
                                                int consecutiveFailures,
                                                const std::string& url)
{
  if (!transient || consecutiveFailures != 1 || state.trackerRetryUsed ||
      state.phase == BtDiscoveryPhase::Idle ||
      state.phase == BtDiscoveryPhase::Healthy) {
    return false;
  }
  state.trackerRetryUsed = true;
  state.phase = BtDiscoveryPhase::Recovering;
  state.retryTracker = url;
  return true;
}

void BtDiscoveryController::trackerReplied(BtDiscoveryState& state, int peers)
{
  state.trackerPeersReceived += std::max(0, peers);
  if (state.phase == BtDiscoveryPhase::Recovering) {
    state.phase = BtDiscoveryPhase::Discovering;
  }
}

void BtDiscoveryController::peersObserved(BtDiscoveryState& state,
                                          int connectedPeers)
{
  if (connectedPeers > 0 && state.phase != BtDiscoveryPhase::Idle) {
    state.phase = BtDiscoveryPhase::Healthy;
  }
  else if (connectedPeers == 0 && state.phase == BtDiscoveryPhase::Healthy) {
    state.phase = BtDiscoveryPhase::Discovering;
  }
}

void BtDiscoveryController::stop(BtDiscoveryState& state)
{
  state.phase = BtDiscoveryPhase::Idle;
}

const char* btDiscoveryPhaseName(BtDiscoveryPhase phase)
{
  switch (phase) {
  case BtDiscoveryPhase::Idle:
    return "idle";
  case BtDiscoveryPhase::Discovering:
    return "discovering";
  case BtDiscoveryPhase::Recovering:
    return "recovering";
  case BtDiscoveryPhase::Healthy:
    return "healthy";
  }
  return "idle";
}

} // namespace aria2
