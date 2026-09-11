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
#include "Ed2kAttribute.h"
#include "ContextAttribute.h"
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include "DownloadContext.h"
#include "SimpleRandomizer.h"
#include "ed2k_hash.h"
#include <algorithm>
#include <chrono>
#include <limits>

namespace aria2 {

void Ed2kSharingTime::restore(int64_t seconds)
{
  accumulatedSeconds_ = std::max<int64_t>(0, seconds);
  startedAt_ = Timer::zero();
  active_ = false;
}

int64_t Ed2kSharingTime::seconds(const Timer& now) const
{
  if (!active_) {
    return accumulatedSeconds_;
  }
  const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                           startedAt_.difference(now))
                           .count();
  if (elapsed >= std::numeric_limits<int64_t>::max() - accumulatedSeconds_) {
    return std::numeric_limits<int64_t>::max();
  }
  return accumulatedSeconds_ + elapsed;
}

void Ed2kSharingTime::synchronize(bool active, const Timer& now)
{
  if (active == active_) {
    return;
  }
  if (active) {
    startedAt_ = now;
    active_ = true;
    return;
  }
  accumulatedSeconds_ = seconds(now);
  startedAt_ = Timer::zero();
  active_ = false;
}

Ed2kAttribute* getEd2kAttrs(const std::shared_ptr<DownloadContext>& dctx)
{
  return getEd2kAttrs(dctx.get());
}

Ed2kAttribute* getEd2kAttrs(DownloadContext* dctx)
{
  if (!dctx || !dctx->hasAttribute(CTX_ATTR_ED2K)) {
    return nullptr;
  }
  return static_cast<Ed2kAttribute*>(dctx->getAttribute(CTX_ATTR_ED2K).get());
}

std::string normalizeEd2kClientHash(std::string clientHash)
{
  if (clientHash.size() >= ed2k::HASH_LENGTH) {
    clientHash = clientHash.substr(0, ed2k::HASH_LENGTH);
  }
  else {
    clientHash.append(ed2k::HASH_LENGTH - clientHash.size(), '\0');
  }
  clientHash[5] = 14;
  clientHash[14] = 111;
  return clientHash;
}

std::string createEd2kClientHash()
{
  std::string clientHash(ed2k::HASH_LENGTH, '\0');
  SimpleRandomizer::getInstance()->getRandomBytes(
      reinterpret_cast<unsigned char*>(&clientHash[0]), clientHash.size());
  return normalizeEd2kClientHash(std::move(clientHash));
}

uint32_t createEd2kKadUdpVerifyKey()
{
  uint32_t key = 0;
  SimpleRandomizer::getInstance()->getRandomBytes(
      reinterpret_cast<unsigned char*>(&key), sizeof(key));
  return key == 0 ? 1 : key;
}

} // namespace aria2
