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
#ifndef D_ADMISSION_WINDOW_H
#define D_ADMISSION_WINDOW_H

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace aria2 {

// Decides how many HTTP range requests one download may keep in flight.
//
// Explicit overload reduces admission once per request generation. Late
// payload corrects incomplete observations; subsequent growth requires new
// payload progress. Retry timing and limits belong to individual ranges.
class AdmissionWindow {
public:
  using Clock = std::chrono::steady_clock;
  using TimePoint = Clock::time_point;
  using Duration = Clock::duration;

  void reset(int maximum, Duration retryWait);

  int limit() const { return limit_; }
  int maximum() const { return maximum_; }
  uint64_t epoch() const { return epoch_; }
  bool open(TimePoint now) const { return now >= holdUntil_; }
  void received() { progressed_ = true; }

  // Payload, not response headers, proves admission.
  bool admitted(int admitted);
  bool rejected(uint64_t epoch, int admitted, TimePoint now);
  // Attempt one additive growth step. Returns true when the window grew.
  bool grow(TimePoint now, size_t inflight, bool hasWork);
  // Forbid new requests until `until`.
  void hold(TimePoint until);
  // The next moment a scheduling decision can change, if any.
  std::optional<TimePoint> nextEvent(TimePoint now, bool hasWork) const;

private:
  int maximum_ = 1;
  int limit_ = 1;
  uint64_t epoch_ = 0;
  uint64_t decision_ = 0;
  bool progressed_ = false;
  Duration growthInterval_{};
  TimePoint holdUntil_{};
  TimePoint nextGrowth_{};
};

} // namespace aria2

#endif // D_ADMISSION_WINDOW_H
