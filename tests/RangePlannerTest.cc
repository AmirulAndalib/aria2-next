#include "RangePlanner.h"

#include "a2doctest.h"

namespace aria2 {

class RangePlannerTest {
public:
  void testRestoreAndScheduleGaps();
  void testLocalizedRetry();
  void testTailSplit();
};

A2_TEST(RangePlannerTest, testRestoreAndScheduleGaps)
A2_TEST(RangePlannerTest, testLocalizedRetry)
A2_TEST(RangePlannerTest, testTailSplit)

void RangePlannerTest::testRestoreAndScheduleGaps()
{
  RangePlanner planner;
  planner.restore({{0, 10}, {20, 30}});
  planner.configure(50, 10, {{30, 40}});

  REQUIRE_EQ(20, planner.completedLength());
  auto first = planner.takeReady({});
  REQUIRE(first);
  CHECK_EQ(10, first->begin);
  CHECK_EQ(20, first->end);
  auto second = planner.takeReady({});
  REQUIRE(second);
  CHECK_EQ(40, second->begin);
  CHECK_EQ(50, second->end);
  CHECK(!planner.takeReady({}));
}

void RangePlannerTest::testLocalizedRetry()
{
  RangePlanner planner;
  planner.configure(100, 100, {});
  auto lease = planner.takeReady({});
  REQUIRE(lease);
  planner.commit(lease->begin, 35);
  lease->begin = 35;
  lease->attempts = 1;
  const auto deadline = RangePlanner::TimePoint{} + std::chrono::seconds(1);
  planner.defer(*lease, deadline);

  CHECK_EQ(35, planner.completedLength());
  CHECK(!planner.takeReady(deadline - std::chrono::milliseconds(1)));
  auto retry = planner.takeReady(deadline);
  REQUIRE(retry);
  CHECK_EQ(35, retry->begin);
  CHECK_EQ(100, retry->end);
  CHECK_EQ(1, retry->attempts);
}

void RangePlannerTest::testTailSplit()
{
  RangePlanner planner;
  planner.splitAndEnqueue({40, 100, 2, 1}, 4, 10);
  int64_t cursor = 40;
  size_t count = 0;
  while (auto lease = planner.takeReady({})) {
    CHECK_EQ(cursor, lease->begin);
    CHECK(lease->end > lease->begin);
    CHECK_EQ(2, lease->attempts);
    CHECK_EQ(1, lease->uriIndex);
    cursor = lease->end;
    ++count;
  }
  CHECK_EQ(100, cursor);
  CHECK_EQ(4, count);
}

} // namespace aria2
