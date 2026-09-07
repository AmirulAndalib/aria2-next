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
#include "AdmissionWindow.h"

#include <algorithm>

namespace aria2 {

void AdmissionWindow::reset(int maximum, Duration retryWait)
{
  // The configured ceiling is the user's declared intent: use it at once so
  // cold starts do not pay extra round trips. An origin that refuses part of
  // the burst is judged once and the window converges from there.
  maximum_ = std::max(1, maximum);
  limit_ = maximum_;
  epoch_ = 0;
  decision_ = 0;
  growthEpoch_ = 0;
  probing_ = false;
  slowStart_ = true;
  baseInterval_ = std::max<Duration>(retryWait, std::chrono::seconds(1));
  probeInterval_ = baseInterval_;
  holdUntil_ = {};
  nextGrowth_ = {};
}

bool AdmissionWindow::admitted(uint64_t epoch, int admitted)
{
  // Service is evidence regardless of when the request was issued: a batch
  // that was pending during a rejection or a starved round still proves what
  // the origin serves, so the window never sits below that count.
  const auto previous = limit_;
  if (probing_ && epoch == growthEpoch_) {
    // The origin accepted a request at the grown level: probe faster again.
    probeInterval_ = baseInterval_;
    probing_ = false;
  }
  if (slowStart_) {
    ++limit_;
  }
  limit_ = std::clamp(std::max(limit_, admitted), 1, maximum_);
  return limit_ != previous;
}

bool AdmissionWindow::rejected(uint64_t epoch, int admitted, TimePoint now)
{
  if (stale(epoch)) {
    return false;
  }
  const auto previous = limit_;
  // A refused growth probe only disproves the extra slot: the level below it
  // was being served. Any other refusal is judged at an unproven level and
  // halves, never below the connections the origin is serving right now.
  const auto fallback = probe(epoch) ? limit_ - 1 : limit_ / 2;
  limit_ = std::clamp(std::max(admitted, fallback), 1, maximum_);
  slowStart_ = false;
  probing_ = false;
  probeInterval_ = std::min<Duration>(probeInterval_ * 2, maxProbeInterval);
  nextGrowth_ = now + probeInterval_;
  decision_ = ++epoch_;
  return limit_ != previous;
}

bool AdmissionWindow::grow(TimePoint now, size_t inflight, bool hasWork)
{
  if (slowStart_ || limit_ >= maximum_ || !hasWork ||
      inflight < static_cast<size_t>(limit_) || now < nextGrowth_ ||
      now < holdUntil_) {
    return false;
  }
  ++limit_;
  growthEpoch_ = ++epoch_;
  probing_ = true;
  nextGrowth_ = now + probeInterval_;
  return true;
}

void AdmissionWindow::hold(TimePoint until)
{
  holdUntil_ = std::max(holdUntil_, until);
  decision_ = ++epoch_;
}

void AdmissionWindow::deferGrowth(TimePoint until)
{
  nextGrowth_ = std::max(nextGrowth_, until);
}

void AdmissionWindow::starve()
{
  limit_ = 1;
  slowStart_ = true;
  probing_ = false;
  probeInterval_ = baseInterval_;
  decision_ = ++epoch_;
}

std::optional<AdmissionWindow::TimePoint>
AdmissionWindow::nextEvent(TimePoint now, bool hasWork) const
{
  if (now < holdUntil_) {
    return holdUntil_;
  }
  if (hasWork && !slowStart_ && limit_ < maximum_ && now < nextGrowth_) {
    return nextGrowth_;
  }
  return std::nullopt;
}

} // namespace aria2
