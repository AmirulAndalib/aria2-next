/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
#include "Exception.h"
#include <sstream>
#include <utility>
#include <vector>
#include "support/Text.h"
#include "support/Encoding.h"
#include "support/ContentDisposition.h"
#include "a2functional.h"
#include "a2iterator.h"
#include <cmath>
#include <cstring>
#include <string>
#include <cassert>
#include <iostream>
#include "a2doctest.h"
#include "DlAbortEx.h"
#include "BitfieldMan.h"
#include "ByteArrayDiskWriter.h"
#include "FileEntry.h"
#include "File.h"
#include "array_fun.h"
#include "BufferedFile.h"
#include "TestUtil.h"
#include "SocketCore.h"
#include "support/Numbers.h"
#include "support/FilePath.h"
#include "support/Network.h"
#include "support/Storage.h"
#include "support/Random.h"
#include "support/ByteOrder.h"
#include <array>

namespace aria2 {

TEST_CASE("UtilTest2.testMkdirs")
{
  std::string dir = A2_TEST_OUT_DIR "/aria2-UtilTest2-testMkdirs";
  File d(dir);
  if (d.exists()) {
    REQUIRE(d.remove());
  }
  REQUIRE(!d.exists());
  util::mkdirs(dir);
  REQUIRE(d.isDir());

  std::string file = A2_TEST_DIR "/support/FilePathTest.cc";
  File f(file);
  REQUIRE(f.isFile());
  REQUIRE_THROWS_AS(util::mkdirs(file), DlAbortEx);
}

TEST_CASE("UtilTest2.testJoinPath")
{
  const std::string dir1dir2file[] = {"dir1", "dir2", "file"};
  REQUIRE_EQ(std::string("dir1/dir2/file"),
             util::joinPath(std::begin(dir1dir2file), std::end(dir1dir2file)));

  const std::string dirparentfile[] = {"dir", "..", "file"};
  REQUIRE_EQ(std::string("file"), util::joinPath(std::begin(dirparentfile),
                                                 std::end(dirparentfile)));

  const std::string dirparentparentfile[] = {"dir", "..", "..", "file"};
  REQUIRE_EQ(std::string("file"),
             util::joinPath(std::begin(dirparentparentfile),
                            std::end(dirparentparentfile)));

  const std::string dirdotfile[] = {"dir", ".", "file"};
  REQUIRE_EQ(std::string("dir/file"),
             util::joinPath(std::begin(dirdotfile), std::end(dirdotfile)));

  const std::string empty[] = {};
  REQUIRE_EQ(std::string(""), util::joinPath(&empty[0], &empty[0]));

  const std::string parentdot[] = {"..", "."};
  REQUIRE_EQ(std::string(""),
             util::joinPath(std::begin(parentdot), std::end(parentdot)));
}

TEST_CASE("UtilTest2.testParseIndexPath")
{
  std::pair<size_t, std::string> p = util::parseIndexPath("1=foo");
  REQUIRE_EQ((size_t)1, p.first);
  REQUIRE_EQ(std::string("foo"), p.second);
  try {
    util::parseIndexPath("1X=foo");
    FAIL("exception must be thrown.");
  }
  catch (Exception& e) {
    // success
  }
  try {
    util::parseIndexPath("1=");
    FAIL("exception must be thrown.");
  }
  catch (Exception& e) {
    // success
  }
}

TEST_CASE("UtilTest2.testCreateIndexPaths")
{
  std::stringstream in("1=/tmp/myfile\n"
                       "100=/myhome/mypicture.png\n");
  std::vector<std::pair<size_t, std::string>> m = util::createIndexPaths(in);
  REQUIRE_EQ((size_t)2, m.size());
  REQUIRE_EQ((size_t)1, m[0].first);
  REQUIRE_EQ(std::string("/tmp/myfile"), m[0].second);
  REQUIRE_EQ((size_t)100, m[1].first);
  REQUIRE_EQ(std::string("/myhome/mypicture.png"), m[1].second);
}

TEST_CASE("UtilTest2.testApplyDir")
{
  REQUIRE_EQ(std::string("./pred"), util::applyDir("", "pred"));
  REQUIRE_EQ(std::string("/pred"), util::applyDir("/", "pred"));
  REQUIRE_EQ(std::string("./pred"), util::applyDir(".", "pred"));
  REQUIRE_EQ(std::string("/dl/pred"), util::applyDir("/dl", "pred"));
#ifndef __MINGW32__
  REQUIRE_EQ(std::string("/dev/null"), util::applyDir(".", "/dev/null"));
#else  // __MINGW32__
  REQUIRE_EQ(std::string("C:/download/file"),
             util::applyDir("D:/aria2", "C:/download/file"));
#endif // __MINGW32__
}

TEST_CASE("UtilTest2.testFixTaintedBasename")
{
  REQUIRE_EQ(std::string("a%2Fb"), util::fixTaintedBasename("a/b"));
#ifdef __MINGW32__
  REQUIRE_EQ(std::string("a%5Cb"), util::fixTaintedBasename("a\\b"));
#else  // !__MINGW32__
  REQUIRE_EQ(std::string("a\\b"), util::fixTaintedBasename("a\\b"));
#endif // !__MINGW32__
}

TEST_CASE("UtilTest2.testDetectDirTraversal")
{
  REQUIRE(util::detectDirTraversal("/foo"));
  REQUIRE(util::detectDirTraversal("./foo"));
  REQUIRE(util::detectDirTraversal("../foo"));
  REQUIRE(util::detectDirTraversal("foo/../bar"));
  REQUIRE(util::detectDirTraversal("foo/./bar"));
  REQUIRE(util::detectDirTraversal("foo/."));
  REQUIRE(util::detectDirTraversal("foo/.."));
  REQUIRE(util::detectDirTraversal("."));
  REQUIRE(util::detectDirTraversal(".."));
  REQUIRE(util::detectDirTraversal("/"));
  REQUIRE(util::detectDirTraversal("foo/"));
  REQUIRE(util::detectDirTraversal("\t"));
  REQUIRE(!util::detectDirTraversal("foo/bar"));
  REQUIRE(!util::detectDirTraversal("foo"));
}

TEST_CASE("UtilTest2.testEscapePath")
{
  REQUIRE_EQ(std::string("foo%00bar%00%01"),
             util::escapePath(std::string("foo") + (char)0x00 +
                              std::string("bar") + (char)0x00 + (char)0x01));
#ifdef __MINGW32__
  REQUIRE_EQ(std::string("foo%5Cbar"), util::escapePath("foo\\bar"));
#else  // !__MINGW32__
  REQUIRE_EQ(std::string("foo\\bar"), util::escapePath("foo\\bar"));
#endif // !__MINGW32__
}

} // namespace aria2
