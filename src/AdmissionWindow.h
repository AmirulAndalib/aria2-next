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
// The origin's admission capacity is unknown, so the window is adjusted the
// way TCP adjusts a congestion window: multiplicative decrease on refusal,
// additive growth probes afterwards, exponential recovery after a round in
// which nothing was served. Every decision is tied to an epoch so that a burst
// of refusals from one batch of requests only lowers the window once, and the
// connections the origin is serving form a floor that a late or early refusal
// can never cut below. Time-based holds are reserved for explicit server
// signals (Retry-After) and for rounds in which nothing was served.
class AdmissionWindow {
public:
  using Clock = std::chrono::steady_clock;
  using TimePoint = Clock::time_point;
  using Duration = Clock::duration;

  static constexpr std::chrono::seconds maxProbeInterval{60};

  void reset(int maximum, Duration retryWait);

  int limit() const { return limit_; }
  int maximum() const { return maximum_; }
  uint64_t epoch() const { return epoch_; }
  bool slowStart() const { return slowStart_; }
  // Requests issued before the latest decrease or hold carry no news; a
  // growth step does not make earlier requests stale because their refusal
  // still disproves the level they were issued at.
  bool stale(uint64_t epoch) const { return epoch < decision_; }
  // True when `epoch` belongs to a request issued after the latest growth
  // step, i.e. one that tried to exceed the level the origin had accepted.
  bool probe(uint64_t epoch) const { return probing_ && epoch >= growthEpoch_; }
  bool open(TimePoint now) const { return now >= holdUntil_; }
  Duration probeInterval() const { return probeInterval_; }

  // A request issued in `epoch` received a successful range response while
  // `admitted` connections (including this one) are being served. Returns
  // true when the window grew.
  bool admitted(uint64_t epoch, int admitted);
  // A request issued in `epoch` was refused or never served. `admitted` is
  // the number of connections the origin is serving right now. Returns true
  // when the window changed.
  bool rejected(uint64_t epoch, int admitted, TimePoint now);
  // Attempt one additive growth step. Returns true when the window grew.
  bool grow(TimePoint now, size_t inflight, bool hasWork);
  // Forbid new requests until `until`.
  void hold(TimePoint until);
  // Keep replacing served connections but do not try to exceed the current
  // level before `until`.
  void deferGrowth(TimePoint until);
  // Nothing was served during a whole round: fall back to a single probe.
  void starve();
  // The next moment a scheduling decision can change, if any.
  std::optional<TimePoint> nextEvent(TimePoint now, bool hasWork) const;

private:
  int maximum_ = 1;
  int limit_ = 1;
  uint64_t epoch_ = 0;
  uint64_t decision_ = 0;
  uint64_t growthEpoch_ = 0;
  bool probing_ = false;
  bool slowStart_ = true;
  Duration baseInterval_{};
  Duration probeInterval_{};
  TimePoint holdUntil_{};
  TimePoint nextGrowth_{};
};

} // namespace aria2

#endif // D_ADMISSION_WINDOW_H
