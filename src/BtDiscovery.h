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
#ifndef D_BT_DISCOVERY_H
#define D_BT_DISCOVERY_H

#include <cstdint>
#include <string>

namespace aria2 {

enum class BtDiscoveryPhase { Idle, Discovering, Recovering, Healthy };

struct BtDiscoveryState {
  uint64_t epoch = 0;
  uint64_t networkEpoch = 0;
  BtDiscoveryPhase phase = BtDiscoveryPhase::Idle;
  bool trackerRetryUsed = false;
  int trackerPeersReceived = 0;
  std::string retryTracker;
};

class BtDiscoveryController {
private:
  uint64_t nextEpoch_ = 0;

public:
  void activate(BtDiscoveryState& state, uint64_t networkEpoch);
  bool requestTrackerRetry(BtDiscoveryState& state, bool transient,
                           int consecutiveFailures, const std::string& url);
  void trackerReplied(BtDiscoveryState& state, int peers);
  void peersObserved(BtDiscoveryState& state, int connectedPeers);
  void stop(BtDiscoveryState& state);
};

const char* btDiscoveryPhaseName(BtDiscoveryPhase phase);

} // namespace aria2

#endif // D_BT_DISCOVERY_H
