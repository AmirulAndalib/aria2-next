#include "BtPeerBlocklist.h"

#include <sstream>

#include "a2doctest.h"

#include "Exception.h"

namespace aria2 {

class BtPeerBlocklistTest {

public:
  void testLoadBtnRules();
  void testIdempotentReplacement();
  void testRejectInvalidReload();
};

A2_TEST(BtPeerBlocklistTest, testLoadBtnRules)
A2_TEST(BtPeerBlocklistTest, testIdempotentReplacement)
A2_TEST(BtPeerBlocklistTest, testRejectInvalidReload)

void BtPeerBlocklistTest::testLoadBtnRules()
{
  std::istringstream input(
      "# BTN rules\n"
      "203.0.113.25\n"
      "198.51.100.0/24\n"
      "2001:db8::1234\n"
      "2001:250:3c08:4500::/56\n"
      "::ffff:192.0.2.45\n");
  BtPeerBlocklist blocklist;

  blocklist.load(input, "memory");

  REQUIRE_EQ((size_t)5, blocklist.count());
  REQUIRE(blocklist.contains("203.0.113.25"));
  REQUIRE(!blocklist.contains("203.0.113.26"));
  REQUIRE(blocklist.contains("198.51.100.255"));
  REQUIRE(!blocklist.contains("198.51.101.0"));
  REQUIRE(blocklist.contains("2001:db8::1234"));
  REQUIRE(blocklist.contains("2001:250:3c08:45ff::1"));
  REQUIRE(!blocklist.contains("2001:250:3c08:4600::1"));
  REQUIRE(blocklist.contains("192.0.2.45"));
  REQUIRE(blocklist.contains("::ffff:192.0.2.45"));
}

void BtPeerBlocklistTest::testIdempotentReplacement()
{
  BtPeerBlocklist blocklist;

  REQUIRE(blocklist.replace({"203.0.113.0/24", "203.0.113.0/25"},
                            "first"));
  REQUIRE_EQ((size_t)1, blocklist.count());
  const auto revision = blocklist.revision();

  REQUIRE(!blocklist.replace({"203.0.113.0/24"}, "equivalent"));
  REQUIRE_EQ(revision, blocklist.revision());
  REQUIRE_EQ((size_t)1, blocklist.count());

  REQUIRE(blocklist.replace({"198.51.100.0/24"}, "changed"));
  REQUIRE_EQ(revision + 1, blocklist.revision());
}

void BtPeerBlocklistTest::testRejectInvalidReload()
{
  BtPeerBlocklist blocklist;
  std::istringstream valid("203.0.113.0/24\n");
  blocklist.load(valid, "valid");

  std::istringstream invalid("not-an-ip\n");
  REQUIRE_THROWS_AS(blocklist.load(invalid, "invalid"), Exception);

  REQUIRE_EQ((size_t)1, blocklist.count());
  REQUIRE(blocklist.contains("203.0.113.10"));
}

} // namespace aria2
