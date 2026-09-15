#include "SystemResolver.h"

#include <chrono>
#include <thread>

#include "a2doctest.h"

namespace aria2 {

class SystemResolverTest {
public:
  void testResolveLocalhostIPv4();
};

A2_TEST(SystemResolverTest, testResolveLocalhostIPv4)

void SystemResolverTest::testResolveLocalhostIPv4()
{
  SystemResolver resolver;
  const auto id = resolver.resolve("localhost", 80, false,
                                   std::chrono::seconds(2));
  std::vector<std::string> addresses;
  std::string error;
  auto status = SystemResolver::Status::Pending;
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::seconds(2);
  while (status == SystemResolver::Status::Pending &&
         std::chrono::steady_clock::now() < deadline) {
    resolver.poll();
    status = resolver.take(id, addresses, error);
    if (status == SystemResolver::Status::Pending) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
  REQUIRE_EQ(SystemResolver::Status::Success, status);
  REQUIRE(error.empty());
  REQUIRE(!addresses.empty());
  for (const auto& address : addresses) {
    REQUIRE(address.find(':') == std::string::npos);
  }
}

} // namespace aria2
