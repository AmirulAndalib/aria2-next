#include "SpeedCalc.h"
#include <string>
#include "a2doctest.h"
#include "wallclock.h"

namespace aria2 {

class SpeedCalcTest {


private:
public:
  void setUp() {}

  void testUpdate();
  void testFixedWindow();
  void testRefreshCadence();
};

A2_TEST(SpeedCalcTest, testUpdate)
A2_TEST(SpeedCalcTest, testFixedWindow)
A2_TEST(SpeedCalcTest, testRefreshCadence)

void SpeedCalcTest::testUpdate()
{
  global::wallclock().reset(24_h);
  SpeedCalc calc;
  calc.update(1000);
}

void SpeedCalcTest::testFixedWindow()
{
  global::wallclock().reset(24_h);
  SpeedCalc calc;
  calc.reset();

  calc.update(2000);
  REQUIRE_EQ(200, calc.calculateSpeed());

  global::wallclock().advance(1_s);
  calc.update(2000);
  REQUIRE_EQ(400, calc.calculateSpeed());

  global::wallclock().advance(1_s);
  REQUIRE_EQ(400, calc.calculateSpeed());

  global::wallclock().advance(8_s);
  REQUIRE_EQ(200, calc.calculateSpeed());

  global::wallclock().advance(1_s);
  REQUIRE_EQ(0, calc.calculateSpeed());
}

void SpeedCalcTest::testRefreshCadence()
{
  global::wallclock().reset(24_h);
  SpeedCalc calc;
  calc.reset();
  calc.update(2000);
  REQUIRE_EQ(200, calc.calculateSpeed());

  calc.update(2000);
  REQUIRE_EQ(200, calc.calculateSpeed());

  global::wallclock().advance(std::chrono::milliseconds(250));
  REQUIRE_EQ(400, calc.calculateSpeed());
}

} // namespace aria2
