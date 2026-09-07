#include "AdmissionWindow.h"

#include "a2doctest.h"

namespace aria2 {

class AdmissionWindowTest {
public:
  void testResponseOrder();
  void testProgressAndHold();
};

A2_TEST(AdmissionWindowTest, testResponseOrder)
A2_TEST(AdmissionWindowTest, testProgressAndHold)

void AdmissionWindowTest::testResponseOrder()
{
  using namespace std::chrono_literals;
  const AdmissionWindow::TimePoint now{};
  AdmissionWindow early, late;
  early.reset(8, 0s);
  late.reset(8, 0s);
  const auto batch = early.epoch();
  REQUIRE(early.rejected(batch, 1, now));
  CHECK_EQ(4, early.limit());
  CHECK(early.admitted(7));
  CHECK(late.rejected(late.epoch(), 7, now));
  CHECK_EQ(7, early.limit());
  CHECK_EQ(late.limit(), early.limit());
  CHECK(!early.rejected(batch, 1, now + 1s));
  CHECK_EQ(7, early.limit());
}

void AdmissionWindowTest::testProgressAndHold()
{
  using namespace std::chrono_literals;
  const AdmissionWindow::TimePoint now{};
  AdmissionWindow window;
  window.reset(8, 0s);
  CHECK_EQ(8, window.limit());
  window.received();
  window.rejected(window.epoch(), 0, now);
  // Pre-overload data must not justify growth, nor may an idle socket.
  CHECK(!window.grow(now + 2s, 4, true));
  window.received();
  CHECK(!window.grow(now, 4, true));
  CHECK(!window.grow(now + 2s, 3, true));
  CHECK(!window.grow(now + 2s, 4, false));
  REQUIRE(window.grow(now + 2s, 4, true));
  CHECK_EQ(5, window.limit());
  CHECK(!window.grow(now + 4s, 5, true));
  // The first probe does not get an exemption from Retry-After.
  window.rejected(window.epoch(), 4, now + 2s);
  window.hold(now + 12s);
  window.hold(now + 3s);
  window.received();
  CHECK(!window.open(now + 11s));
  CHECK(!window.grow(now + 11s, 4, true));
  REQUIRE(window.nextEvent(now + 3s, true));
  CHECK_EQ(now + 12s, *window.nextEvent(now + 3s, true));
  CHECK(window.open(now + 12s));
  CHECK(window.grow(now + 12s, 4, true));
  CHECK_EQ(5, window.limit());
}

} // namespace aria2
