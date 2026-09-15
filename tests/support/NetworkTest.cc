/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
#include "support/Text.h"
#include <cstring>
#include <string>
#include <cassert>
#include "a2doctest.h"
#include "TestUtil.h"
#include "support/Network.h"

namespace aria2 {

TEST_CASE("UtilTest2.testIsNumericHost")
{
  REQUIRE(util::isNumericHost("192.168.0.1"));
  REQUIRE(!util::isNumericHost("aria2.sf.net"));
  REQUIRE(util::isNumericHost("::1"));
}

TEST_CASE("UtilTest2.testInSameCidrBlock")
{
  REQUIRE(util::inSameCidrBlock("192.168.128.1", "192.168.0.1", 16));
  REQUIRE(!util::inSameCidrBlock("192.168.128.1", "192.168.0.1", 17));

  REQUIRE(util::inSameCidrBlock("192.168.0.1", "192.168.0.1", 32));
  REQUIRE(!util::inSameCidrBlock("192.168.0.1", "192.168.0.0", 32));

  REQUIRE(util::inSameCidrBlock("192.168.0.1", "10.0.0.1", 0));

  REQUIRE(util::inSameCidrBlock("2001:db8::2:1", "2001:db0::2:2", 28));
  REQUIRE(!util::inSameCidrBlock("2001:db8::2:1", "2001:db0::2:2", 29));

  REQUIRE(!util::inSameCidrBlock("2001:db8::2:1", "192.168.0.1", 8));
}

TEST_CASE("UtilTest2.testNoProxyDomainMatch")
{
  REQUIRE(util::noProxyDomainMatch("localhost", "localhost"));
  REQUIRE(util::noProxyDomainMatch("192.168.0.1", "192.168.0.1"));
  REQUIRE(util::noProxyDomainMatch("www.example.org", ".example.org"));
  REQUIRE(!util::noProxyDomainMatch("www.example.org", "example.org"));
  REQUIRE(!util::noProxyDomainMatch("192.168.0.1", "0.1"));
  REQUIRE(!util::noProxyDomainMatch("example.org", "example.com"));
  REQUIRE(!util::noProxyDomainMatch("example.org", "www.example.org"));
}

TEST_CASE("UtilTest2.testInPrivateAddress")
{
  REQUIRE(!util::inPrivateAddress("localhost"));
  REQUIRE(util::inPrivateAddress("192.168.0.1"));
  // Only checks prefix..
  REQUIRE(util::inPrivateAddress("10."));
  REQUIRE(!util::inPrivateAddress("172."));
  REQUIRE(!util::inPrivateAddress("172.15.0.0"));
  REQUIRE(util::inPrivateAddress("172.16.0.0"));
  REQUIRE(util::inPrivateAddress("172.31.0.0"));
  REQUIRE(!util::inPrivateAddress("172.32.0.0"));
}

} // namespace aria2
