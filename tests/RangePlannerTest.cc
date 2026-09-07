#include "RangePlanner.h"

#include "a2doctest.h"

namespace aria2 {

class RangePlannerTest {
public:
  void testRestoreAndScheduleGaps();
  void testRequeueKeepsOffsetOrder();
  void testReadyRefill();
  void testRetryDeadlineAndPriority();
};

A2_TEST(RangePlannerTest, testRestoreAndScheduleGaps)
A2_TEST(RangePlannerTest, testRequeueKeepsOffsetOrder)
A2_TEST(RangePlannerTest, testReadyRefill)
A2_TEST(RangePlannerTest, testRetryDeadlineAndPriority)

void RangePlannerTest::testRetryDeadlineAndPriority()
{
  const auto deadline = RangePlanner::TimePoint{} + std::chrono::seconds(10);
  RangePlanner planner;
  planner.enqueue({20, 100, 2, 1, deadline});
  planner.enqueue({0, 10});
  CHECK_EQ(2, planner.refillReady(8, 10, 1, {}));
  REQUIRE(planner.nextDeadline({}));
  CHECK_EQ(deadline, *planner.nextDeadline({}));
  auto fresh = planner.takeReady({});
  REQUIRE(fresh);
  CHECK_EQ(0, fresh->begin);
  CHECK(planner.hasPending());
  CHECK(!planner.hasReady({}));
  CHECK(!planner.takeReady(deadline - std::chrono::milliseconds(1)));
  planner.enqueue({10, 20});
  auto retry = planner.takeReady(deadline);
  REQUIRE(retry);
  CHECK_EQ(20, retry->begin);
  CHECK_EQ(1, retry->attempts);
  CHECK_EQ(2, retry->uriIndex);
  CHECK_EQ(RangePlanner::TimePoint{}, retry->readyAt);
  CHECK(!planner.nextDeadline(deadline));
}

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

void RangePlannerTest::testRequeueKeepsOffsetOrder()
{
  RangePlanner planner;
  planner.configure(100, 25, {});
  auto first = planner.takeReady({});
  auto second = planner.takeReady({});
  REQUIRE(first);
  REQUIRE(second);
  planner.commit(first->begin, 10);
  // A partially served range returns ahead of later work, regardless of
  // the order in which requests finished.
  planner.enqueue(second->remainder(30));
  auto remainder = first->remainder(10);
  remainder.uriIndex = 2;
  planner.enqueue(remainder);

  CHECK_EQ(4, planner.readyCount());
  auto retry = planner.takeReady({});
  REQUIRE(retry);
  CHECK_EQ(10, retry->begin);
  CHECK_EQ(25, retry->end);
  CHECK_EQ(2, retry->uriIndex);
  auto next = planner.takeReady({});
  REQUIRE(next);
  CHECK_EQ(30, next->begin);
  CHECK_EQ(50, next->end);
  planner.enqueue({});
  CHECK_EQ(2, planner.readyCount());
  planner.commit(10, 100);
  CHECK(planner.complete());
}

void RangePlannerTest::testReadyRefill()
{
  RangePlanner planner;
  planner.enqueue({0, 80, 1});
  planner.enqueue({80, 120, 2});

  CHECK_EQ(4, planner.refillReady(4, 20, 10, {}));
  int64_t cursor = 0;
  while (auto lease = planner.takeReady({})) {
    CHECK_EQ(cursor, lease->begin);
    CHECK(lease->end > lease->begin);
    CHECK_EQ(lease->begin < 80 ? 1 : 2, lease->uriIndex);
    cursor = lease->end;
  }
  CHECK_EQ(120, cursor);
}

} // namespace aria2
