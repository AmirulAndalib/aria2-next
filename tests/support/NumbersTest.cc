/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
#include "Exception.h"
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>
#include "support/Text.h"
#include "a2functional.h"
#include <cstring>
#include <string>
#include <cassert>
#include <iostream>
#include "a2doctest.h"
#include "FileEntry.h"
#include "TestUtil.h"
#include "support/Numbers.h"
#include "support/Storage.h"

namespace aria2 {

TEST_CASE("UtilTest2.testGetRealSize")
{
  REQUIRE_EQ((int64_t)4_g, util::getRealSize("4096M"));
  REQUIRE_EQ((int64_t)1_k, util::getRealSize("1K"));
  REQUIRE_EQ((int64_t)4_g, util::getRealSize("4096m"));
  REQUIRE_EQ((int64_t)1_k, util::getRealSize("1k"));
  REQUIRE_EQ((int64_t)1572864, util::getRealSize("1.5M"));
  REQUIRE_EQ((int64_t)512, util::getRealSize("0.5K"));
  REQUIRE_EQ((int64_t)1, util::getRealSize("1.9"));
  REQUIRE_EQ((int64_t)1364, util::getRealSize("1.333K"));
  REQUIRE_EQ((int64_t)0, util::getRealSize("0.0000000000000000000000001M"));
  try {
    util::getRealSize("");
    FAIL("exception must be thrown.");
  }
  catch (Exception& e) {
    std::cerr << e.stackTrace();
  }
  try {
    util::getRealSize("foo");
    FAIL("exception must be thrown.");
  }
  catch (Exception& e) {
    std::cerr << e.stackTrace();
  }
  try {
    util::getRealSize("-1");
    FAIL("exception must be thrown.");
  }
  catch (Exception& e) {
    std::cerr << e.stackTrace();
  }
  try {
    util::getRealSize("1.2.3K");
    FAIL("exception must be thrown.");
  }
  catch (Exception& e) {
    std::cerr << e.stackTrace();
  }
  try {
    util::getRealSize("1K2");
    FAIL("exception must be thrown.");
  }
  catch (Exception& e) {
    std::cerr << e.stackTrace();
  }
  try {
    util::getRealSize("9223372036854775807K");
    FAIL("exception must be thrown.");
  }
  catch (Exception& e) {
    std::cerr << e.stackTrace();
  }
  try {
    util::getRealSize("9223372036854775807M");
    FAIL("exception must be thrown.");
  }
  catch (Exception& e) {
    std::cerr << e.stackTrace();
  }
}

TEST_CASE("UtilTest2.testAbbrevSize")
{
  REQUIRE_EQ(std::string("8,589,934,591Gi"),
             util::abbrevSize(9223372036854775807LL));
  REQUIRE_EQ(std::string("4.0Gi"), util::abbrevSize(4_g));
  REQUIRE_EQ(std::string("1.0Ki"), util::abbrevSize(1_k));
  REQUIRE_EQ(std::string("0.9Ki"), util::abbrevSize(1023));
  REQUIRE_EQ(std::string("511"), util::abbrevSize(511));
  REQUIRE_EQ(std::string("0"), util::abbrevSize(0));
  REQUIRE_EQ(std::string("1.1Ki"), util::abbrevSize(1127));
  REQUIRE_EQ(std::string("1.5Mi"), util::abbrevSize(1572864));
}

TEST_CASE("UtilTest2.testIsNumber")
{
  std::string s = "000";
  REQUIRE_EQ(true, util::isNumber(s.begin(), s.end()));
  s = "a";
  REQUIRE_EQ(false, util::isNumber(s.begin(), s.end()));
  s = "0a";
  REQUIRE_EQ(false, util::isNumber(s.begin(), s.end()));
  s = "";
  REQUIRE_EQ(false, util::isNumber(s.begin(), s.end()));
  s = " ";
  REQUIRE_EQ(false, util::isNumber(s.begin(), s.end()));
}

TEST_CASE("UtilTest2.testParseIntSegments")
{
  {
    auto sgl = util::parseIntSegments("1,3-8,10");

    REQUIRE(sgl.hasNext());
    REQUIRE_EQ(1, sgl.next());
    REQUIRE(sgl.hasNext());
    REQUIRE_EQ(3, sgl.next());
    REQUIRE(sgl.hasNext());
    REQUIRE_EQ(4, sgl.next());
    REQUIRE(sgl.hasNext());
    REQUIRE_EQ(5, sgl.next());
    REQUIRE(sgl.hasNext());
    REQUIRE_EQ(6, sgl.next());
    REQUIRE(sgl.hasNext());
    REQUIRE_EQ(7, sgl.next());
    REQUIRE(sgl.hasNext());
    REQUIRE_EQ(8, sgl.next());
    REQUIRE(sgl.hasNext());
    REQUIRE_EQ(10, sgl.next());
    REQUIRE(!sgl.hasNext());
    REQUIRE_EQ(0, sgl.next());
  }
  {
    auto sgl = util::parseIntSegments(",,,1,,,3,,,");
    REQUIRE_EQ(1, sgl.next());
    REQUIRE_EQ(3, sgl.next());
    REQUIRE(!sgl.hasNext());
  }
}

TEST_CASE("UtilTest2.testParseIntSegments_invalidRange")
{
  try {
    auto sgl = util::parseIntSegments("-1");
    FAIL("exception must be thrown.");
  }
  catch (Exception& e) {
  }
  try {
    auto sgl = util::parseIntSegments("1-");
    FAIL("exception must be thrown.");
  }
  catch (Exception& e) {
  }
  try {
    auto sgl = util::parseIntSegments("2147483648");
    FAIL("exception must be thrown.");
  }
  catch (Exception& e) {
  }
  try {
    auto sgl = util::parseIntSegments("2147483647-2147483648");
    FAIL("exception must be thrown.");
  }
  catch (Exception& e) {
  }
  try {
    auto sgl = util::parseIntSegments("1-2x");
    FAIL("exception must be thrown.");
  }
  catch (Exception& e) {
  }
  try {
    auto sgl = util::parseIntSegments("3x-4");
    FAIL("exception must be thrown.");
  }
  catch (Exception& e) {
  }
}

TEST_CASE("UtilTest2.testParseIntNoThrow")
{
  std::string s;
  int32_t n;
  s = " -1 ";
  REQUIRE(util::parseIntNoThrow(n, s));
  REQUIRE_EQ((int32_t)-1, n);

  s = "2147483647";
  REQUIRE(util::parseIntNoThrow(n, s));
  REQUIRE_EQ((int32_t)2147483647, n);

  s = "2147483648";
  REQUIRE(!util::parseIntNoThrow(n, s));
  s = "-2147483649";
  REQUIRE(!util::parseIntNoThrow(n, s));

  s = "12x";
  REQUIRE(!util::parseIntNoThrow(n, s));
  s = "";
  REQUIRE(!util::parseIntNoThrow(n, s));
}

TEST_CASE("UtilTest2.testParseUIntNoThrow")
{
  std::string s;
  uint32_t n;
  s = " 2147483647 ";
  REQUIRE(util::parseUIntNoThrow(n, s));
  REQUIRE_EQ((uint32_t)INT32_MAX, n);
  s = "2147483648";
  REQUIRE(!util::parseUIntNoThrow(n, s));
  s = "-1";
  REQUIRE(!util::parseUIntNoThrow(n, s));
}

TEST_CASE("UtilTest2.testParseLLIntNoThrow")
{
  std::string s;
  int64_t n;
  s = " 9223372036854775807 ";
  REQUIRE(util::parseLLIntNoThrow(n, s));
  REQUIRE_EQ((int64_t)INT64_MAX, n);
  s = "9223372036854775808";
  REQUIRE(!util::parseLLIntNoThrow(n, s));
  s = "-9223372036854775808";
  REQUIRE(util::parseLLIntNoThrow(n, s));
  REQUIRE_EQ((int64_t)INT64_MIN, n);
  s = "-9223372036854775809";
  REQUIRE(!util::parseLLIntNoThrow(n, s));
}

TEST_CASE("UtilTest2.testItos")
{
  {
    int i = 0;
    REQUIRE_EQ(std::string("0"), util::itos(i));
  }
  {
    int i = 100;
    REQUIRE_EQ(std::string("100"), util::itos(i, true));
  }
  {
    int i = 100;
    REQUIRE_EQ(std::string("100"), util::itos(i));
  }
  {
    int i = 12345;
    REQUIRE_EQ(std::string("12,345"), util::itos(i, true));
  }
  {
    int i = 12345;
    REQUIRE_EQ(std::string("12345"), util::itos(i));
  }
  {
    int i = -12345;
    REQUIRE_EQ(std::string("-12,345"), util::itos(i, true));
  }
  {
    int64_t i = INT64_MAX;
    REQUIRE_EQ(std::string("9,223,372,036,854,775,807"), util::itos(i, true));
  }
  {
    int64_t i = INT64_MIN;
    REQUIRE_EQ(std::string("-9,223,372,036,854,775,808"), util::itos(i, true));
  }
}

TEST_CASE("UtilTest2.testUitos")
{
  {
    uint16_t i = 12345;
    REQUIRE_EQ(std::string("12345"), util::uitos(i));
  }
  {
    int16_t i = -12345;
    REQUIRE_EQ(std::string("-12345"), util::uitos(i));
  }
  {
    const auto maximum = std::numeric_limits<uint64_t>::max();
    REQUIRE_EQ(std::string("18446744073709551615"), util::uitos(maximum));
    REQUIRE_EQ(std::string("18,446,744,073,709,551,615"),
               util::uitos(maximum, true));
  }
}

TEST_CASE("UtilTest2.testParsePrioritizePieceRange")
{
  // piece index
  // 0     1     2     3     4     5     6     7
  // |     |              |                    |
  // file1 |              |                    |
  //       |              |                    |
  //       file2          |                    |
  //                    file3                  |
  //                      |                    |
  //                      file4                |
  constexpr size_t pieceLength = 1_k;
  std::vector<std::shared_ptr<FileEntry>> entries(4,
                                                  std::shared_ptr<FileEntry>());
  entries[0].reset(new FileEntry("file1", 1024, 0));
  entries[1].reset(new FileEntry("file2", 2560, entries[0]->getLastOffset()));
  entries[2].reset(new FileEntry("file3", 0, entries[1]->getLastOffset()));
  entries[3].reset(new FileEntry("file4", 3584, entries[2]->getLastOffset()));

  std::vector<size_t> result;
  util::parsePrioritizePieceRange(result, "head=1", entries, pieceLength);
  REQUIRE_EQ((size_t)3, result.size());
  REQUIRE_EQ((size_t)0, result[0]);
  REQUIRE_EQ((size_t)1, result[1]);
  REQUIRE_EQ((size_t)3, result[2]);
  result.clear();
  util::parsePrioritizePieceRange(result, "tail=1", entries, pieceLength);
  REQUIRE_EQ((size_t)3, result.size());
  REQUIRE_EQ((size_t)0, result[0]);
  REQUIRE_EQ((size_t)3, result[1]);
  REQUIRE_EQ((size_t)6, result[2]);
  result.clear();
  util::parsePrioritizePieceRange(result, "head=1K", entries, pieceLength);
  REQUIRE_EQ((size_t)4, result.size());
  REQUIRE_EQ((size_t)0, result[0]);
  REQUIRE_EQ((size_t)1, result[1]);
  REQUIRE_EQ((size_t)3, result[2]);
  REQUIRE_EQ((size_t)4, result[3]);
  result.clear();
  util::parsePrioritizePieceRange(result, "head", entries, pieceLength, 1_k);
  REQUIRE_EQ((size_t)4, result.size());
  REQUIRE_EQ((size_t)0, result[0]);
  REQUIRE_EQ((size_t)1, result[1]);
  REQUIRE_EQ((size_t)3, result[2]);
  REQUIRE_EQ((size_t)4, result[3]);
  result.clear();
  util::parsePrioritizePieceRange(result, "tail=1K", entries, pieceLength);
  REQUIRE_EQ((size_t)4, result.size());
  REQUIRE_EQ((size_t)0, result[0]);
  REQUIRE_EQ((size_t)2, result[1]);
  REQUIRE_EQ((size_t)3, result[2]);
  REQUIRE_EQ((size_t)6, result[3]);
  result.clear();
  util::parsePrioritizePieceRange(result, "tail", entries, pieceLength, 1_k);
  REQUIRE_EQ((size_t)4, result.size());
  REQUIRE_EQ((size_t)0, result[0]);
  REQUIRE_EQ((size_t)2, result[1]);
  REQUIRE_EQ((size_t)3, result[2]);
  REQUIRE_EQ((size_t)6, result[3]);
  result.clear();
  util::parsePrioritizePieceRange(result, "head=1,tail=1", entries,
                                  pieceLength);
  REQUIRE_EQ((size_t)4, result.size());
  REQUIRE_EQ((size_t)0, result[0]);
  REQUIRE_EQ((size_t)1, result[1]);
  REQUIRE_EQ((size_t)3, result[2]);
  REQUIRE_EQ((size_t)6, result[3]);
  result.clear();
  util::parsePrioritizePieceRange(result, "head=300M,tail=300M", entries,
                                  pieceLength);
  REQUIRE_EQ((size_t)7, result.size());
  for (size_t i = 0; i < 7; ++i) {
    REQUIRE_EQ(i, result[i]);
  }
  result.clear();
  util::parsePrioritizePieceRange(result, "", entries, pieceLength);
  REQUIRE(result.empty());
}

TEST_CASE("UtilTest2.testSecfmt")
{
  REQUIRE_EQ(std::string("0s"), util::secfmt(0));
  REQUIRE_EQ(std::string("1s"), util::secfmt(1));
  REQUIRE_EQ(std::string("9s"), util::secfmt(9));
  REQUIRE_EQ(std::string("10s"), util::secfmt(10));
  REQUIRE_EQ(std::string("1m"), util::secfmt(60));
  REQUIRE_EQ(std::string("1m59s"), util::secfmt(119));
  REQUIRE_EQ(std::string("2m"), util::secfmt(120));
  REQUIRE_EQ(std::string("59m59s"), util::secfmt(3599));
  REQUIRE_EQ(std::string("1h"), util::secfmt(3600));
}

TEST_CASE("UtilTest2.testParseDoubleNoThrow")
{
  double n;

  REQUIRE(util::parseDoubleNoThrow(n, " 123 "));
  REQUIRE_EQ(123., n);

  REQUIRE(util::parseDoubleNoThrow(n, "3.14"));
  REQUIRE_EQ(3.14, n);

  REQUIRE(util::parseDoubleNoThrow(n, "-3.14"));
  REQUIRE_EQ(-3.14, n);

  REQUIRE(!util::parseDoubleNoThrow(n, ""));
  REQUIRE(!util::parseDoubleNoThrow(n, "123x"));
}

} // namespace aria2
