#include "RangePlanner.h"

#include "a2doctest.h"

namespace aria2 {

class RangePlannerTest {
public:
  void testRestoreAndScheduleGaps();
  void testLocalizedRetry();
  void testReadyRefill();
  void testBalancedEnqueue();
};

A2_TEST(RangePlannerTest, testRestoreAndScheduleGaps)
A2_TEST(RangePlannerTest, testLocalizedRetry)
A2_TEST(RangePlannerTest, testReadyRefill)
A2_TEST(RangePlannerTest, testBalancedEnqueue)

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
  lease->attempts = 1;
  lease->uriIndex = 2;
  const auto deadline = RangePlanner::TimePoint{} + std::chrono::seconds(1);
  planner.defer(lease->remainder(35), deadline);

  CHECK_EQ(35, planner.completedLength());
  CHECK(!planner.takeReady(deadline - std::chrono::milliseconds(1)));
  auto retry = planner.takeReady(deadline);
  REQUIRE(retry);
  CHECK_EQ(35, retry->begin);
  CHECK_EQ(100, retry->end);
  CHECK_EQ(1, retry->attempts);
  CHECK_EQ(2, retry->uriIndex);
  CHECK(!planner.takeReady(deadline));
  planner.commit(retry->begin, retry->end);
  CHECK(planner.complete());
  CHECK_EQ(100, planner.completedLength());
}

void RangePlannerTest::testReadyRefill()
{
  RangePlanner planner;
  planner.enqueue({0, 80, 0, 1});
  planner.enqueue({80, 120, 1, 2});

  CHECK_EQ(4, planner.refillReady(4, 20, 10));
  int64_t cursor = 0;
  while (auto lease = planner.takeReady({})) {
    CHECK_EQ(cursor, lease->begin);
    CHECK(lease->end > lease->begin);
    if (lease->begin < 80) {
      CHECK_EQ(0, lease->attempts);
      CHECK_EQ(1, lease->uriIndex);
    }
    else {
      CHECK_EQ(1, lease->attempts);
      CHECK_EQ(2, lease->uriIndex);
    }
    cursor = lease->end;
  }
  CHECK_EQ(120, cursor);
}

void RangePlannerTest::testBalancedEnqueue()
{
  RangePlanner planner;
  planner.enqueueBalanced({10, 110, 2, 3}, 4, 10);

  int64_t cursor = 10;
  size_t count = 0;
  while (auto lease = planner.takeReady({})) {
    CHECK_EQ(cursor, lease->begin);
    CHECK(lease->end > lease->begin);
    CHECK_EQ(2, lease->attempts);
    CHECK_EQ(3, lease->uriIndex);
    cursor = lease->end;
    ++count;
  }
  CHECK_EQ(110, cursor);
  CHECK_EQ(4, count);
}

} // namespace aria2
