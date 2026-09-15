#include <ctime>
#include "TimeA2.h"

#include <iostream>

#include "a2doctest.h"

#include "Exception.h"
#include "common.h"

namespace aria2 {

class TimeTest {
public:
  void testParseRFC1123Alt();
};

TEST_CASE_FIXTURE(TimeTest, "TimeTest.testParseRFC1123")
{
  Time t1 = Time::parseRFC1123("Sat, 06 Sep 2008 15:26:33 GMT");
  REQUIRE_EQ((time_t)1220714793, t1.getTimeFromEpoch());
}

void TimeTest::testParseRFC1123Alt()
{
  Time t1 = Time::parseRFC1123Alt("Sat, 06 Sep 2008 15:26:33 +0000");
  REQUIRE_EQ((time_t)1220714793, t1.getTimeFromEpoch());
}

TEST_CASE_FIXTURE(TimeTest, "TimeTest.testParseRFC850")
{
  Time t1 = Time::parseRFC850("Saturday, 06-Sep-08 15:26:33 GMT");
  REQUIRE_EQ((time_t)1220714793, t1.getTimeFromEpoch());
}

TEST_CASE_FIXTURE(TimeTest, "TimeTest.testParseRFC850Ext")
{
  Time t1 = Time::parseRFC850Ext("Saturday, 06-Sep-2008 15:26:33 GMT");
  REQUIRE_EQ((time_t)1220714793, t1.getTimeFromEpoch());
}

TEST_CASE_FIXTURE(TimeTest, "TimeTest.testParseAsctime")
{
  Time t1 = Time::parseAsctime("Sun Sep  6 15:26:33 2008");
  REQUIRE_EQ((time_t)1220714793, t1.getTimeFromEpoch());
}

TEST_CASE_FIXTURE(TimeTest, "TimeTest.testParseHTTPDate")
{
  REQUIRE_EQ(
      (time_t)1220714793,
      Time::parseHTTPDate("Sat, 06 Sep 2008 15:26:33 GMT").getTimeFromEpoch());
  REQUIRE_EQ(
      (time_t)1220714793,
      Time::parseHTTPDate("Sat, 06-Sep-2008 15:26:33 GMT").getTimeFromEpoch());
  REQUIRE_EQ(
      (time_t)1220714793,
      Time::parseHTTPDate("Sat, 06-Sep-08 15:26:33 GMT").getTimeFromEpoch());
  REQUIRE_EQ(
      (time_t)1220714793,
      Time::parseHTTPDate("Sun Sep  6 15:26:33 2008").getTimeFromEpoch());
  REQUIRE(Time::parseHTTPDate("Sat, 2008-09-06 15:26:33 GMT").bad());
}

TEST_CASE_FIXTURE(TimeTest, "TimeTest.testOperatorLess")
{
  REQUIRE(Time(1) < Time(2));
  REQUIRE(!(Time(1) < Time(1)));
  REQUIRE(!(Time(2) < Time(1)));
}

TEST_CASE_FIXTURE(TimeTest, "TimeTest.testToHTTPDate")
{
// This test disabled for MinGW32, because the garbage will be
// displayed and it hides real errors.
#ifndef __MINGW32__
  Time t(1220714793);
  REQUIRE_EQ(std::string("Sat, 06 Sep 2008 15:26:33 GMT"), t.toHTTPDate());
#endif // !__MINGW32__
}

} // namespace aria2
