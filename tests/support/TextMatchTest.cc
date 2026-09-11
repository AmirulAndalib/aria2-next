/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
#include <deque>
#include <memory>
#include <sstream>
#include <utility>
#include "support/Text.h"
#include "a2iterator.h"
#include <cstring>
#include <string>
#include <cassert>
#include "a2doctest.h"
#include "FileEntry.h"
#include "BufferedFile.h"
#include "TestUtil.h"
#include "support/Storage.h"

namespace aria2 {

TEST_CASE("UtilTest1.testStrip")
{
  std::string str1 = "aria2";
  REQUIRE_EQ(str1, util::strip("aria2"));
  REQUIRE_EQ(str1, util::strip(" aria2"));
  REQUIRE_EQ(str1, util::strip("aria2 "));
  REQUIRE_EQ(str1, util::strip(" aria2 "));
  REQUIRE_EQ(str1, util::strip("  aria2  "));
  std::string str2 = "aria2 debut";
  REQUIRE_EQ(str2, util::strip("aria2 debut"));
  REQUIRE_EQ(str2, util::strip(" aria2 debut "));
  std::string str3 = "";
  REQUIRE_EQ(str3, util::strip(""));
  REQUIRE_EQ(str3, util::strip(" "));
  REQUIRE_EQ(str3, util::strip("  "));
  std::string str4 = "A";
  REQUIRE_EQ(str4, util::strip("A"));
  REQUIRE_EQ(str4, util::strip(" A "));
  REQUIRE_EQ(str4, util::strip("  A  "));
}

TEST_CASE("UtilTest1.testStripIter")
{
  Scip p;
  std::string str1 = "aria2";
  std::string s = "aria2";
  p = util::stripIter(s.begin(), s.end());
  REQUIRE_EQ(str1, std::string(p.first, p.second));
  s = " aria2";
  p = util::stripIter(s.begin(), s.end());
  REQUIRE_EQ(str1, std::string(p.first, p.second));
  s = "aria2 ";
  p = util::stripIter(s.begin(), s.end());
  REQUIRE_EQ(str1, std::string(p.first, p.second));
  s = " aria2 ";
  p = util::stripIter(s.begin(), s.end());
  REQUIRE_EQ(str1, std::string(p.first, p.second));
  s = "  aria2  ";
  p = util::stripIter(s.begin(), s.end());
  REQUIRE_EQ(str1, std::string(p.first, p.second));
  std::string str2 = "aria2 debut";
  s = "aria2 debut";
  p = util::stripIter(s.begin(), s.end());
  REQUIRE_EQ(str2, std::string(p.first, p.second));
  s = " aria2 debut ";
  p = util::stripIter(s.begin(), s.end());
  REQUIRE_EQ(str2, std::string(p.first, p.second));
  std::string str3 = "";
  s = "";
  p = util::stripIter(s.begin(), s.end());
  REQUIRE_EQ(str3, std::string(p.first, p.second));
  s = " ";
  p = util::stripIter(s.begin(), s.end());
  REQUIRE_EQ(str3, std::string(p.first, p.second));
  s = "  ";
  p = util::stripIter(s.begin(), s.end());
  REQUIRE_EQ(str3, std::string(p.first, p.second));
  std::string str4 = "A";
  s = "A";
  p = util::stripIter(s.begin(), s.end());
  REQUIRE_EQ(str4, std::string(p.first, p.second));
  s = " A ";
  p = util::stripIter(s.begin(), s.end());
  REQUIRE_EQ(str4, std::string(p.first, p.second));
  s = "  A  ";
  p = util::stripIter(s.begin(), s.end());
  REQUIRE_EQ(str4, std::string(p.first, p.second));
}

TEST_CASE("UtilTest1.testLstripIter")
{
  std::string::iterator r;
  std::string s = "foo";
  r = util::lstripIter(s.begin(), s.end());
  REQUIRE_EQ(std::string("foo"), std::string(r, s.end()));

  s = "  foo bar  ";
  r = util::lstripIter(s.begin(), s.end());
  REQUIRE_EQ(std::string("foo bar  "), std::string(r, s.end()));

  s = "f";
  r = util::lstripIter(s.begin(), s.end());
  REQUIRE_EQ(std::string("f"), std::string(r, s.end()));

  s = "foo  ";
  r = util::lstripIter(s.begin(), s.end());
  REQUIRE_EQ(std::string("foo  "), std::string(r, s.end()));
}

TEST_CASE("UtilTest1.testLstripIter_char")
{
  std::string::iterator r;
  std::string s = "foo";
  r = util::lstripIter(s.begin(), s.end(), '$');
  REQUIRE_EQ(std::string("foo"), std::string(r, s.end()));

  s = "$$foo$bar$$";
  r = util::lstripIter(s.begin(), s.end(), '$');
  REQUIRE_EQ(std::string("foo$bar$$"), std::string(r, s.end()));

  s = "f";
  r = util::lstripIter(s.begin(), s.end(), '$');
  REQUIRE_EQ(std::string("f"), std::string(r, s.end()));

  s = "foo$$";
  r = util::lstripIter(s.begin(), s.end(), '$');
  REQUIRE_EQ(std::string("foo$$"), std::string(r, s.end()));
}

TEST_CASE("UtilTest1.testEndsWith")
{
  std::string target = "abcdefg";
  std::string part = "fg";
  REQUIRE(
      util::endsWith(target.begin(), target.end(), part.begin(), part.end()));

  target = "abdefg";
  part = "g";
  REQUIRE(
      util::endsWith(target.begin(), target.end(), part.begin(), part.end()));

  target = "abdefg";
  part = "eg";
  REQUIRE(
      !util::endsWith(target.begin(), target.end(), part.begin(), part.end()));

  target = "g";
  part = "eg";
  REQUIRE(
      !util::endsWith(target.begin(), target.end(), part.begin(), part.end()));

  target = "g";
  part = "g";
  REQUIRE(
      util::endsWith(target.begin(), target.end(), part.begin(), part.end()));

  target = "g";
  part = "";
  REQUIRE(
      util::endsWith(target.begin(), target.end(), part.begin(), part.end()));

  target = "";
  part = "";
  REQUIRE(
      util::endsWith(target.begin(), target.end(), part.begin(), part.end()));

  target = "";
  part = "g";
  REQUIRE(
      !util::endsWith(target.begin(), target.end(), part.begin(), part.end()));
}

TEST_CASE("UtilTest1.testIendsWith")
{
  std::string target = "abcdefg";
  std::string part = "Fg";
  REQUIRE(
      util::iendsWith(target.begin(), target.end(), part.begin(), part.end()));

  target = "abdefg";
  part = "ef";
  REQUIRE(
      !util::iendsWith(target.begin(), target.end(), part.begin(), part.end()));
}

TEST_CASE("UtilTest1.testStreq")
{
  std::string s1, s2;
  s1 = "foo";
  s2 = "foo";
  REQUIRE(util::streq(s1.begin(), s1.end(), s2.begin(), s2.end()));
  REQUIRE(util::streq(s1.begin(), s1.end(), s2.c_str()));

  s2 = "fooo";
  REQUIRE(!util::streq(s1.begin(), s1.end(), s2.begin(), s2.end()));
  REQUIRE(!util::streq(s1.begin(), s1.end(), s2.c_str()));

  s2 = "fo";
  REQUIRE(!util::streq(s1.begin(), s1.end(), s2.begin(), s2.end()));
  REQUIRE(!util::streq(s1.begin(), s1.end(), s2.c_str()));

  s2 = "";
  REQUIRE(!util::streq(s1.begin(), s1.end(), s2.begin(), s2.end()));
  REQUIRE(!util::streq(s1.begin(), s1.end(), s2.c_str()));

  s1 = "";
  REQUIRE(util::streq(s1.begin(), s1.end(), s2.begin(), s2.end()));
  REQUIRE(util::streq(s1.begin(), s1.end(), s2.c_str()));
}

TEST_CASE("UtilTest1.testStrieq")
{
  std::string s1, s2;
  s1 = "foo";
  s2 = "foo";
  REQUIRE(util::strieq(s1.begin(), s1.end(), s2.begin(), s2.end()));
  REQUIRE(util::strieq(s1.begin(), s1.end(), s2.c_str()));

  s1 = "FoO";
  s2 = "fOo";
  REQUIRE(util::strieq(s1.begin(), s1.end(), s2.begin(), s2.end()));
  REQUIRE(util::strieq(s1.begin(), s1.end(), s2.c_str()));

  s2 = "fooo";
  REQUIRE(!util::strieq(s1.begin(), s1.end(), s2.begin(), s2.end()));
  REQUIRE(!util::strieq(s1.begin(), s1.end(), s2.c_str()));

  s2 = "fo";
  REQUIRE(!util::strieq(s1.begin(), s1.end(), s2.begin(), s2.end()));
  REQUIRE(!util::strieq(s1.begin(), s1.end(), s2.c_str()));

  s2 = "";
  REQUIRE(!util::strieq(s1.begin(), s1.end(), s2.begin(), s2.end()));
  REQUIRE(!util::strieq(s1.begin(), s1.end(), s2.c_str()));

  s1 = "";
  REQUIRE(util::strieq(s1.begin(), s1.end(), s2.begin(), s2.end()));
  REQUIRE(util::strieq(s1.begin(), s1.end(), s2.c_str()));
}

TEST_CASE("UtilTest1.testStrifind")
{
  std::string s1, s2;
  s1 = "yamagakani mukashi wo toheba hARU no tuki";
  s2 = "HaRu";
  REQUIRE(util::strifind(s1.begin(), s1.end(), s2.begin(), s2.end()) !=
          s1.end());
  s2 = "aki";
  REQUIRE(util::strifind(s1.begin(), s1.end(), s2.begin(), s2.end()) ==
          s1.end());
  s1 = "h";
  s2 = "HH";
  REQUIRE(util::strifind(s1.begin(), s1.end(), s2.begin(), s2.end()) ==
          s1.end());
}

TEST_CASE("UtilTest1.testReplace")
{
  REQUIRE_EQ(std::string("abc\n"), util::replace("abc\r\n", "\r", ""));
  REQUIRE_EQ(std::string("abc"), util::replace("abc\r\n", "\r\n", ""));
  REQUIRE_EQ(std::string(""), util::replace("", "\r\n", ""));
  REQUIRE_EQ(std::string("abc"), util::replace("abc", "", "a"));
  REQUIRE_EQ(std::string("xbc"), util::replace("abc", "a", "x"));
}

TEST_CASE("UtilTest1.testStartsWith")
{
  std::string target;
  std::string part;

  target = "abcdefg";
  part = "abc";
  REQUIRE(
      util::startsWith(target.begin(), target.end(), part.begin(), part.end()));
  REQUIRE(util::startsWith(target.begin(), target.end(), part.c_str()));

  target = "abcdefg";
  part = "abx";
  REQUIRE(!util::startsWith(target.begin(), target.end(), part.begin(),
                            part.end()));
  REQUIRE(!util::startsWith(target.begin(), target.end(), part.c_str()));

  target = "abcdefg";
  part = "bcd";
  REQUIRE(!util::startsWith(target.begin(), target.end(), part.begin(),
                            part.end()));
  REQUIRE(!util::startsWith(target.begin(), target.end(), part.c_str()));

  target = "";
  part = "a";
  REQUIRE(!util::startsWith(target.begin(), target.end(), part.begin(),
                            part.end()));
  REQUIRE(!util::startsWith(target.begin(), target.end(), part.c_str()));

  target = "";
  part = "";
  REQUIRE(
      util::startsWith(target.begin(), target.end(), part.begin(), part.end()));
  REQUIRE(util::startsWith(target.begin(), target.end(), part.c_str()));

  target = "a";
  part = "";
  REQUIRE(
      util::startsWith(target.begin(), target.end(), part.begin(), part.end()));
  REQUIRE(util::startsWith(target.begin(), target.end(), part.c_str()));

  target = "a";
  part = "a";
  REQUIRE(
      util::startsWith(target.begin(), target.end(), part.begin(), part.end()));
  REQUIRE(util::startsWith(target.begin(), target.end(), part.c_str()));
}

TEST_CASE("UtilTest1.testIstartsWith")
{
  std::string target;
  std::string part;

  target = "abcdefg";
  part = "aBc";
  REQUIRE(util::istartsWith(target.begin(), target.end(), part.begin(),
                            part.end()));
  REQUIRE(util::istartsWith(target.begin(), target.end(), part.c_str()));

  target = "abcdefg";
  part = "abx";
  REQUIRE(!util::istartsWith(target.begin(), target.end(), part.begin(),
                             part.end()));
  REQUIRE(!util::istartsWith(target.begin(), target.end(), part.c_str()));
}

TEST_CASE("UtilTest2.testToUpper")
{
  std::string src = "608cabc0f2fa18c260cafd974516865c772363d5";
  std::string upp = "608CABC0F2FA18C260CAFD974516865C772363D5";

  REQUIRE_EQ(upp, util::toUpper(src));
}

TEST_CASE("UtilTest2.testToLower")
{
  std::string src = "608CABC0F2FA18C260CAFD974516865C772363D5";
  std::string upp = "608cabc0f2fa18c260cafd974516865c772363d5";

  REQUIRE_EQ(upp, util::toLower(src));
}

TEST_CASE("UtilTest2.testUppercase")
{
  std::string src = "608cabc0f2fa18c260cafd974516865c772363d5";
  std::string ans = "608CABC0F2FA18C260CAFD974516865C772363D5";
  util::uppercase(src);
  REQUIRE_EQ(ans, src);
}

TEST_CASE("UtilTest2.testLowercase")
{
  std::string src = "608CABC0F2FA18C260CAFD974516865C772363D5";
  std::string ans = "608cabc0f2fa18c260cafd974516865c772363d5";
  util::lowercase(src);
  REQUIRE_EQ(ans, src);
}

TEST_CASE("UtilTest2.testToStream")
{
  std::ostringstream os;
  std::shared_ptr<FileEntry> f1(new FileEntry("aria2.tar.bz2", 12300, 0));
  std::shared_ptr<FileEntry> f2(new FileEntry("aria2.txt", 556, 0));
  std::deque<std::shared_ptr<FileEntry>> entries;
  entries.push_back(f1);
  entries.push_back(f2);
  const char* filename = A2_TEST_OUT_DIR "/aria2_UtilTest2_testToStream";
  BufferedFile fp(filename, BufferedFile::WRITE);
  util::toStream(entries.begin(), entries.end(), fp);
  fp.close();
  REQUIRE_EQ(std::string("Files:\n"
                         "idx|path/length\n"
                         "===+======================================="
                         "====================================\n"
                         "  1|aria2.tar.bz2\n"
                         "   |12KiB (12,300)\n"
                         "---+---------------------------------------"
                         "------------------------------------\n"
                         "  2|aria2.txt\n"
                         "   |556B (556)\n"
                         "---+---------------------------------------"
                         "------------------------------------\n"),
             readFile(filename));
}

TEST_CASE("UtilTest2.testIsLowercase")
{
  std::string s = "alpha";
  REQUIRE_EQ(true, util::isLowercase(s.begin(), s.end()));
  s = "Alpha";
  REQUIRE_EQ(false, util::isLowercase(s.begin(), s.end()));
  s = "1alpha";
  REQUIRE_EQ(false, util::isLowercase(s.begin(), s.end()));
  s = "";
  REQUIRE_EQ(false, util::isLowercase(s.begin(), s.end()));
  s = " ";
  REQUIRE_EQ(false, util::isLowercase(s.begin(), s.end()));
}

TEST_CASE("UtilTest2.testIsUppercase")
{
  std::string s = "ALPHA";
  REQUIRE_EQ(true, util::isUppercase(s.begin(), s.end()));
  s = "Alpha";
  REQUIRE_EQ(false, util::isUppercase(s.begin(), s.end()));
  s = "1ALPHA";
  REQUIRE_EQ(false, util::isUppercase(s.begin(), s.end()));
  s = "";
  REQUIRE_EQ(false, util::isUppercase(s.begin(), s.end()));
  s = " ";
  REQUIRE_EQ(false, util::isUppercase(s.begin(), s.end()));
}

TEST_CASE("UtilTest2.testNextParam")
{
  std::string s1 = "    :a  :  b=c :d=b::::g::";
  std::pair<std::string::iterator, bool> r;
  std::string name, value;
  r = util::nextParam(name, value, s1.begin(), s1.end(), ':');
  REQUIRE(r.second);
  REQUIRE_EQ(std::string("a"), name);
  REQUIRE_EQ(std::string(), value);

  r = util::nextParam(name, value, r.first, s1.end(), ':');
  REQUIRE(r.second);
  REQUIRE_EQ(std::string("b"), name);
  REQUIRE_EQ(std::string("c"), value);

  r = util::nextParam(name, value, r.first, s1.end(), ':');
  REQUIRE(r.second);
  REQUIRE_EQ(std::string("d"), name);
  REQUIRE_EQ(std::string("b"), value);

  r = util::nextParam(name, value, r.first, s1.end(), ':');
  REQUIRE(r.second);
  REQUIRE_EQ(std::string("g"), name);
  REQUIRE_EQ(std::string(), value);

  std::string s2 = "";
  r = util::nextParam(name, value, s2.begin(), s2.end(), ':');
  REQUIRE(!r.second);

  std::string s3 = "   ";
  r = util::nextParam(name, value, s3.begin(), s3.end(), ':');
  REQUIRE(!r.second);

  std::string s4 = ":::";
  r = util::nextParam(name, value, s4.begin(), s4.end(), ':');
  REQUIRE(!r.second);
}

} // namespace aria2
