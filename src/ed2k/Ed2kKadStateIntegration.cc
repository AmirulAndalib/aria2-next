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
#include "Ed2kKadState.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include "Ed2kAttribute.h"
#include <algorithm>

namespace aria2 {

bool consumeEd2kKadFirewallCheckHost(Ed2kAttribute* attrs,
                                     const std::string& host)
{
  if (!attrs) {
    return false;
  }
  auto itr = std::find(attrs->kadFirewallCheckHosts.begin(),
                       attrs->kadFirewallCheckHosts.end(), host);
  if (itr == attrs->kadFirewallCheckHosts.end()) {
    return false;
  }
  attrs->kadFirewallCheckHosts.clear();
  return true;
}

ed2k::KadRoutingSnapshot createEd2kKadSnapshot(const Ed2kAttribute* attrs)
{
  ed2k::KadRoutingSnapshot snapshot = attrs->kadRoutingTable->snapshot();
  snapshot.lastFirewalledCheck = attrs->lastKadFirewalledCheck;
  snapshot.lastSourcePublish = attrs->lastKadSourcePublish;
  snapshot.lastSourceSearch = attrs->lastKadSourceSearch;
  snapshot.sourceSearchCount = attrs->kadSourceSearchCount;
  snapshot.udpVerifyKey = attrs->kadUdpVerifyKey;
  snapshot.observedAddresses = attrs->kadObservedAddresses;
  snapshot.firewalled = attrs->kadFirewalled;
  return snapshot;
}

void restoreEd2kKadOperationalState(Ed2kAttribute* attrs,
                                    const ed2k::KadRoutingSnapshot& snapshot)
{
  attrs->lastKadFirewalledCheck = snapshot.lastFirewalledCheck;
  attrs->lastKadSourcePublish = snapshot.lastSourcePublish;
  attrs->lastKadSourceSearch = snapshot.lastSourceSearch;
  attrs->kadSourceSearchCount = snapshot.sourceSearchCount;
  attrs->kadUdpVerifyKey = snapshot.udpVerifyKey;
  attrs->kadObservedAddresses = snapshot.observedAddresses;
  attrs->kadFirewalled = snapshot.firewalled;
}

bool shouldStartEd2kKadSourceSearch(const Ed2kAttribute* attrs, int64_t now)
{
  if (!attrs || attrs->link.hash.empty() || !attrs->kadRoutingTable ||
      attrs->kadRoutingTable->usefulSize() == 0) {
    return false;
  }
  constexpr size_t MAX_SOURCES_PER_FILE_UDP = 50;
  if (attrs->peerStates.size() >= MAX_SOURCES_PER_FILE_UDP) {
    return false;
  }
  if (attrs->kadSourceTraversal && !attrs->kadSourceTraversal->done()) {
    return false;
  }
  const auto count =
      std::max<uint32_t>(1, std::min<uint32_t>(attrs->kadSourceSearchCount, 7));
  const auto delay = static_cast<int64_t>(3600) * count;
  return attrs->lastKadSourceSearch == 0 ||
         now >= attrs->lastKadSourceSearch + delay;
}

void markEd2kKadSourceSearchStarted(Ed2kAttribute* attrs, int64_t now)
{
  if (!attrs) {
    return;
  }
  if (attrs->kadSourceSearchCount < 7) {
    ++attrs->kadSourceSearchCount;
  }
  attrs->lastKadSourceSearch = now;
}

} // namespace aria2
