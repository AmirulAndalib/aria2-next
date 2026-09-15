#include <cstddef>
#include <cstdint>
#include "bitfield.h"

#include "a2doctest.h"

namespace aria2 {

TEST_CASE("bitfieldTest.testTest")
{
  unsigned char bitfield[] = {0xaa};

  REQUIRE(bitfield::test(bitfield, 8, 0));
  REQUIRE(!bitfield::test(bitfield, 8, 1));
}

TEST_CASE("bitfieldTest.testCountBit32")
{
  REQUIRE_EQ((size_t)32, bitfield::countBit32(UINT32_MAX));
  REQUIRE_EQ((size_t)8, bitfield::countBit32(255));
}

TEST_CASE("bitfieldTest.testCountSetBit")
{
  unsigned char bitfield[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf9};
  // (nbits+7)/8 == 0 && nbits%32 == 0
  REQUIRE_EQ((size_t)62, bitfield::countSetBit(bitfield, 64));
  REQUIRE_EQ((size_t)62, bitfield::countSetBitSlow(bitfield, 64));
  // (nbits+7)/8 != 0 && nbits%32 != 0 && len%4 == 0
  REQUIRE_EQ((size_t)56, bitfield::countSetBit(bitfield, 56));
  REQUIRE_EQ((size_t)56, bitfield::countSetBitSlow(bitfield, 56));
  // (nbits+7)/8 != 0 && nbits%32 != 0 && len%4 != 0
  REQUIRE_EQ((size_t)40, bitfield::countSetBit(bitfield, 40));
  REQUIRE_EQ((size_t)40, bitfield::countSetBitSlow(bitfield, 40));
  // (nbits+7)/8 == 0 && nbits%32 != 0
  REQUIRE_EQ((size_t)61, bitfield::countSetBit(bitfield, 63));
  REQUIRE_EQ((size_t)61, bitfield::countSetBitSlow(bitfield, 63));
  // nbts == 0
  REQUIRE_EQ((size_t)0, bitfield::countSetBit(bitfield, 0));
  REQUIRE_EQ((size_t)0, bitfield::countSetBitSlow(bitfield, 0));
}

TEST_CASE("bitfieldTest.testLastByteMask")
{
  REQUIRE_EQ((unsigned int)0, (unsigned int)bitfield::lastByteMask(0));
  REQUIRE_EQ((unsigned int)128, (unsigned int)bitfield::lastByteMask(9));
  REQUIRE_EQ((unsigned int)240, (unsigned int)bitfield::lastByteMask(12));
  REQUIRE_EQ((unsigned int)255, (unsigned int)bitfield::lastByteMask(16));
}

} // namespace aria2
