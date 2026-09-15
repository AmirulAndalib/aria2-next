/* <!-- copyright */
/*
 * aria2 - The high speed download utility
 *
 * Copyright (C) 2006 Tatsuhiro Tsujikawa
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * In addition, as a special exception, the copyright holders give
 * permission to link the code of portions of this program with the
 * OpenSSL library under certain conditions as described in each
 * individual source file, and distribute linked combinations
 * including the two.
 * You must obey the GNU General Public License in all respects
 * for all of the code used other than OpenSSL.  If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so.  If you
 * do not wish to do so, delete this exception statement from your
 * version.  If you delete this exception statement from all source
 * files in the program, then also delete it here.
 */
/* copyright --> */
#include "SpeedCalc.h"

#include <algorithm>
#include <cstdint>
#include <limits>

#include "wallclock.h"

namespace aria2 {

namespace {
constexpr auto SPEED_WINDOW = 10_s;
constexpr auto SLOT_TIME = std::chrono::milliseconds(250);

int rateForWindow(const std::deque<std::pair<Timer, size_t>>& timeSlots,
                  const Timer& now, std::chrono::milliseconds window,
                  std::chrono::milliseconds elapsed)
{
  uint64_t bytes = 0;
  for (auto it = timeSlots.rbegin(); it != timeSlots.rend(); ++it) {
    if (it->first.difference(now) >= window) {
      break;
    }
    const auto sample = static_cast<uint64_t>(it->second);
    bytes = bytes > std::numeric_limits<uint64_t>::max() - sample
                ? std::numeric_limits<uint64_t>::max()
                : bytes + sample;
  }
  const auto denominator = std::max(SLOT_TIME, std::min(window, elapsed));
  const auto rate = static_cast<long double>(bytes) * 1000.0L /
                    static_cast<long double>(denominator.count());
  return static_cast<int>(
      std::min<long double>(rate, std::numeric_limits<int>::max()));
}
} // namespace

SpeedCalc::SpeedCalc() : accumulatedLength_(0), currentSpeed_(0), maxSpeed_(0)
{
}

void SpeedCalc::reset()
{
  timeSlots_.clear();
  start_ = global::wallclock();
  lastCalculation_ = Timer::zero();
  accumulatedLength_ = 0;
  currentSpeed_ = 0;
  maxSpeed_ = 0;
}

void SpeedCalc::removeStaleTimeSlot(const Timer& now)
{
  while (!timeSlots_.empty()) {
    if (timeSlots_[0].first.difference(now) < SPEED_WINDOW) {
      break;
    }
    timeSlots_.pop_front();
  }
}

int SpeedCalc::calculateSpeed()
{
  const auto& now = global::wallclock();
  removeStaleTimeSlot(now);
  if (timeSlots_.empty()) {
    currentSpeed_ = 0;
    return 0;
  }
  if (!lastCalculation_.isZero() &&
      lastCalculation_.difference(now) < SLOT_TIME) {
    return currentSpeed_;
  }
  lastCalculation_ = now;
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      start_.difference(now));
  currentSpeed_ = rateForWindow(
      timeSlots_, now,
      std::chrono::duration_cast<std::chrono::milliseconds>(SPEED_WINDOW),
      elapsed);
  maxSpeed_ = std::max(currentSpeed_, maxSpeed_);
  return currentSpeed_;
}

int SpeedCalc::calculateNewestSpeed(int seconds)
{
  const auto& now = global::wallclock();
  removeStaleTimeSlot(now);

  if (seconds <= 0 || timeSlots_.empty()) {
    return 0;
  }
  return rateForWindow(timeSlots_, now,
                       std::chrono::milliseconds(seconds * 1000LL),
                       std::chrono::duration_cast<std::chrono::milliseconds>(
                           start_.difference(now)));
}

void SpeedCalc::update(size_t bytes)
{
  const auto& now = global::wallclock();
  removeStaleTimeSlot(now);
  if (timeSlots_.empty() ||
      timeSlots_.back().first.difference(now) >= SLOT_TIME) {
    timeSlots_.push_back(std::make_pair(now, bytes));
  }
  else {
    timeSlots_.back().second += bytes;
  }
  accumulatedLength_ += bytes;
}

int SpeedCalc::calculateAvgSpeed() const
{
  auto milliElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                          start_.difference(global::wallclock()))
                          .count();
  // if milliElapsed is too small, the average speed is rubbish, better
  // return 0
  if (milliElapsed > 4) {
    int speed = accumulatedLength_ * 1000 / milliElapsed;
    return speed;
  }
  else {
    return 0;
  }
}

} // namespace aria2
