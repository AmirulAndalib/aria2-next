#include "AdmissionWindow.h"

#include "a2doctest.h"

namespace aria2 {

class AdmissionWindowTest {
public:
  void testStartsAtMaximum();
  void testRejectionOrderDoesNotMatter();
  void testOneDecreasePerBatch();
  void testProbeBackoffAndRecovery();
  void testHoldAndStarve();
};

A2_TEST(AdmissionWindowTest, testStartsAtMaximum)
A2_TEST(AdmissionWindowTest, testRejectionOrderDoesNotMatter)
A2_TEST(AdmissionWindowTest, testOneDecreasePerBatch)
A2_TEST(AdmissionWindowTest, testProbeBackoffAndRecovery)
A2_TEST(AdmissionWindowTest, testHoldAndStarve)

namespace {

using namespace std::chrono_literals;
const AdmissionWindow::TimePoint origin{};

// A window that recovered to `limit` served connections and was then refused
// once at that level: congestion avoidance with a 2 s probe interval.
AdmissionWindow settled(int maximum, int limit)
{
  AdmissionWindow window;
  window.reset(maximum, 0s);
  window.starve();
  while (window.limit() < limit) {
    window.admitted(window.epoch(), 0);
  }
  window.rejected(window.epoch(), limit, origin);
  return window;
}

} // namespace

void AdmissionWindowTest::testStartsAtMaximum()
{
  AdmissionWindow window;
  window.reset(64, 0s);
  CHECK_EQ(64, window.limit());
  CHECK(window.open(origin));
  CHECK(!window.nextEvent(origin, true));
  CHECK(!window.grow(origin, 64, true));
  CHECK(!window.admitted(window.epoch(), 64));
  CHECK_EQ(64, window.limit());

  window.reset(0, 0s);
  CHECK_EQ(1, window.limit());
  CHECK_EQ(1, window.maximum());
}

void AdmissionWindowTest::testRejectionOrderDoesNotMatter()
{
  // Early refusal: it arrives while most siblings are unanswered.
  AdmissionWindow early;
  early.reset(64, 0s);
  const auto batch = early.epoch();
  CHECK(early.rejected(batch, 1, origin));
  CHECK_EQ(32, early.limit());
  CHECK(!early.slowStart());
  // Late refusals from the same batch add nothing.
  CHECK(!early.rejected(batch, 15, origin + 1s));
  CHECK_EQ(32, early.limit());
  // Late admissions from the same batch keep streaming; the window never
  // falls below what the origin is serving and rises to it when the batch
  // turns out to be served after all.
  CHECK(!early.admitted(batch, 20));
  CHECK_EQ(32, early.limit());
  CHECK(early.admitted(batch, 40));
  CHECK_EQ(40, early.limit());
  CHECK(!early.admitted(batch, 40));

  // Late refusal: fifteen siblings were already admitted.
  AdmissionWindow late;
  late.reset(64, 0s);
  CHECK(late.rejected(late.epoch(), 15, origin));
  CHECK_EQ(32, late.limit());

  // Served connections are a floor even when halving would go lower.
  auto floor = settled(64, 11);
  CHECK_EQ(11, floor.limit());
  CHECK(!floor.rejected(floor.epoch() - 1, 11, origin));
  CHECK_EQ(11, floor.limit());
  const auto level = floor.epoch();
  CHECK(!floor.rejected(level, 11, origin));
  CHECK(floor.stale(level));
  CHECK_EQ(11, floor.limit());
  CHECK(floor.rejected(floor.epoch(), 0, origin));
  CHECK_EQ(5, floor.limit());
}

void AdmissionWindowTest::testOneDecreasePerBatch()
{
  AdmissionWindow window;
  window.reset(64, 0s);
  const auto batch = window.epoch();
  CHECK(window.rejected(batch, 4, origin));
  CHECK_EQ(32, window.limit());
  for (int i = 0; i < 48; ++i) {
    CHECK(!window.rejected(batch, 4, origin));
  }
  CHECK_EQ(32, window.limit());
  CHECK(window.stale(batch));
  // The next batch is judged on its own.
  CHECK(window.rejected(window.epoch(), 4, origin + 1s));
  CHECK_EQ(16, window.limit());
}

void AdmissionWindowTest::testProbeBackoffAndRecovery()
{
  auto window = settled(64, 11);
  auto now = origin;
  CHECK_EQ(11, window.limit());
  CHECK_EQ(2s, window.probeInterval());
  // Growth waits for the probe interval and requires a saturated window.
  CHECK(!window.grow(now, 11, true));
  CHECK(!window.grow(now + 2s, 10, true));
  CHECK(!window.grow(now + 2s, 11, false));
  REQUIRE(window.nextEvent(now, true));
  CHECK_EQ(now + 2s, *window.nextEvent(now, true));
  CHECK(!window.nextEvent(now, false));
  now += 2s;
  CHECK(!window.probe(window.epoch()));
  CHECK(window.grow(now, 11, true));
  CHECK_EQ(12, window.limit());
  const auto probe = window.epoch();
  CHECK(window.probe(probe));
  CHECK(!window.probe(probe - 1));
  CHECK(!window.grow(now, 12, true));
  // The probe is refused while replacements are still pending: only the extra
  // slot is disproved, the interval doubles.
  CHECK(window.rejected(probe, 4, now));
  CHECK(!window.probe(probe));
  CHECK_EQ(11, window.limit());
  CHECK_EQ(4s, window.probeInterval());
  // A Retry-After aimed at the probe only postpones growth.
  window.deferGrowth(now + 9s);
  CHECK(window.open(now));
  CHECK(!window.grow(now + 8s, 11, true));
  now += 9s;
  CHECK(window.grow(now, 11, true));
  // The probe is admitted: the interval resets so growth continues quickly.
  CHECK(!window.admitted(window.epoch(), 12));
  CHECK_EQ(1s, window.probeInterval());
  CHECK_EQ(12, window.limit());
  CHECK(!window.grow(now, 12, true));
  CHECK(window.grow(now + 4s, 12, true));
  CHECK_EQ(13, window.limit());
  // A request issued just before that growth is not a probe and not stale:
  // its refusal disproves the previous level and halves the window.
  const auto before = window.epoch() - 1;
  CHECK(!window.stale(before));
  CHECK(!window.probe(before));
  CHECK(window.rejected(before, 6, now));
  CHECK_EQ(6, window.limit());
  CHECK(window.stale(before));
  CHECK(window.stale(before + 1));
  // Repeated refusals saturate at the ceiling.
  for (int i = 0; i < 8; ++i) {
    window.rejected(window.epoch(), 11, now);
  }
  CHECK_EQ(AdmissionWindow::maxProbeInterval, window.probeInterval());
  CHECK_EQ(64, window.maximum());

  // The configured retry wait becomes the base probe interval.
  AdmissionWindow slow;
  slow.reset(4, 10s);
  CHECK_EQ(10s, slow.probeInterval());
  slow.rejected(slow.epoch(), 1, origin);
  CHECK_EQ(20s, slow.probeInterval());
}

void AdmissionWindowTest::testHoldAndStarve()
{
  AdmissionWindow window;
  window.reset(64, 0s);
  const auto batch = window.epoch();
  window.hold(origin + 5s);
  CHECK(window.stale(batch));
  CHECK(!window.open(origin + 4s));
  CHECK(window.open(origin + 5s));
  REQUIRE(window.nextEvent(origin, false));
  CHECK_EQ(origin + 5s, *window.nextEvent(origin, false));
  // Holds only extend; a shorter one does not shorten a longer one.
  window.hold(origin + 3s);
  CHECK(!window.open(origin + 4s));
  CHECK(!window.grow(origin + 4s, 64, true));
  CHECK_EQ(64, window.limit());

  // A round without service falls back to a single probe; service then
  // doubles the window per round-trip like TCP slow start.
  window.rejected(window.epoch(), 20, origin);
  window.starve();
  CHECK_EQ(1, window.limit());
  CHECK(window.slowStart());
  CHECK_EQ(1s, window.probeInterval());
  CHECK(!window.grow(origin + 10s, 1, true));
  // Service from requests issued before the starve still counts.
  CHECK(window.admitted(0, 1));
  CHECK_EQ(2, window.limit());
  int admissions = 0;
  while (window.limit() < 64) {
    CHECK(window.admitted(window.epoch(), 0));
    ++admissions;
  }
  CHECK_EQ(62, admissions);
  CHECK(!window.admitted(window.epoch(), 0));
  // The first refusal ends slow start again.
  CHECK(window.rejected(window.epoch(), 40, origin));
  CHECK_EQ(40, window.limit());
  CHECK(!window.slowStart());
}

} // namespace aria2
