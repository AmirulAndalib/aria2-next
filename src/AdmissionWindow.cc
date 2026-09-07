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
  progressed_ = false;
  growthInterval_ = std::max<Duration>(retryWait, std::chrono::seconds(1));
  holdUntil_ = {};
  nextGrowth_ = {};
}

bool AdmissionWindow::admitted(int admitted)
{
  const auto previous = limit_;
  limit_ = std::clamp(std::max(limit_, admitted), 1, maximum_);
  return limit_ != previous;
}

bool AdmissionWindow::rejected(uint64_t epoch, int admitted, TimePoint now)
{
  if (epoch < decision_) {
    return false;
  }
  const auto previous = limit_;
  limit_ = std::clamp(std::max(admitted, limit_ / 2), 1, maximum_);
  progressed_ = false;
  nextGrowth_ = now + growthInterval_;
  decision_ = ++epoch_;
  return limit_ != previous;
}

bool AdmissionWindow::grow(TimePoint now, size_t inflight, bool hasWork)
{
  if (!progressed_ || limit_ >= maximum_ || !hasWork ||
      inflight < static_cast<size_t>(limit_) || now < nextGrowth_ ||
      now < holdUntil_) {
    return false;
  }
  ++limit_;
  ++epoch_;
  progressed_ = false;
  nextGrowth_ = now + growthInterval_;
  return true;
}

void AdmissionWindow::hold(TimePoint until)
{
  holdUntil_ = std::max(holdUntil_, until);
  nextGrowth_ = std::max(nextGrowth_, until);
}

std::optional<AdmissionWindow::TimePoint>
AdmissionWindow::nextEvent(TimePoint now, bool hasWork) const
{
  if (now < holdUntil_) {
    return holdUntil_;
  }
  if (hasWork && progressed_ && limit_ < maximum_ && now < nextGrowth_) {
    return nextGrowth_;
  }
  return std::nullopt;
}

} // namespace aria2
