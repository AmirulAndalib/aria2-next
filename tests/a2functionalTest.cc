#include <ctime>
#include "a2functional.h"

#include <string>
#include <algorithm>
#include <vector>

#include "a2doctest.h"

namespace aria2 {

class a2functionalTest {
public:
  struct LastAccess {
    time_t lastAccess_;
    LastAccess(time_t lastAccess) : lastAccess_(lastAccess) {}

    time_t getLastAccessTime() const { return lastAccess_; }
  };
};

TEST_CASE_FIXTURE(a2functionalTest, "a2functionalTest.testStrjoin")
{
  std::vector<std::string> v;
  REQUIRE_EQ(std::string(""), strjoin(v.begin(), v.end(), " "));

  v.push_back("A");

  REQUIRE_EQ(std::string("A"), strjoin(v.begin(), v.end(), " "));

  v.push_back("hero");
  v.push_back("is");
  v.push_back("lonely");

  REQUIRE_EQ(std::string("A hero is lonely"), strjoin(v.begin(), v.end(), " "));
}

TEST_CASE_FIXTURE(a2functionalTest, "a2functionalTest.testLeastRecentAccess")
{
  std::vector<LastAccess> v;
  for (int i = 99; i >= 0; --i) {
    v.push_back(LastAccess(i));
  }
  std::sort(v.begin(), v.end(), LeastRecentAccess<LastAccess>());
  for (int i = 0; i < 100; ++i) {
    REQUIRE_EQ((time_t)i, v[i].lastAccess_);
  }
}

} // namespace aria2
