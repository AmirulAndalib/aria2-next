#include "a2functional.h"
#include <chrono>
#include "SpeedCalc.h"
#include "a2doctest.h"
#include "wallclock.h"

namespace aria2 {

TEST_CASE("SpeedCalcTest.testUpdate")
{
  global::wallclock().reset(24_h);
  SpeedCalc calc;
  calc.update(1000);
}

TEST_CASE("SpeedCalcTest.testFixedWindow")
{
  global::wallclock().reset(24_h);
  SpeedCalc calc;
  calc.reset();

  calc.update(2000);
  REQUIRE_EQ(8000, calc.calculateSpeed());

  global::wallclock().advance(1_s);
  calc.update(2000);
  REQUIRE_EQ(4000, calc.calculateSpeed());

  global::wallclock().advance(1_s);
  REQUIRE_EQ(2000, calc.calculateSpeed());

  global::wallclock().advance(8_s);
  REQUIRE_EQ(200, calc.calculateSpeed());

  global::wallclock().advance(1_s);
  REQUIRE_EQ(0, calc.calculateSpeed());
}

TEST_CASE("SpeedCalcTest.testRefreshCadence")
{
  global::wallclock().reset(24_h);
  SpeedCalc calc;
  calc.reset();
  calc.update(2000);
  REQUIRE_EQ(8000, calc.calculateSpeed());

  calc.update(2000);
  REQUIRE_EQ(8000, calc.calculateSpeed());

  global::wallclock().advance(std::chrono::milliseconds(250));
  REQUIRE_EQ(16000, calc.calculateSpeed());
}

} // namespace aria2
