/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
#include "DiskWriter.h"
#include <algorithm>
#include <cstdint>
#include <memory>
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

TEST_CASE("UtilTest2.testPercentDecode")
{
  std::string src = "http://aria2.sourceforge.net/aria2%200.7.0%20docs.html";
  REQUIRE_EQ(std::string("http://aria2.sourceforge.net/aria2 0.7.0 docs.html"),
             util::percentDecode(src.begin(), src.end()));

  std::string src2 = "aria2+aria2";
  REQUIRE_EQ(std::string("aria2+aria2"),
             util::percentDecode(src2.begin(), src2.end()));

  std::string src3 = "%5t%20";
  REQUIRE_EQ(std::string("%5t "),
             util::percentDecode(src3.begin(), src3.end()));

  std::string src4 = "%";
  REQUIRE_EQ(std::string("%"), util::percentDecode(src4.begin(), src4.end()));

  std::string src5 = "%3";
  REQUIRE_EQ(std::string("%3"), util::percentDecode(src5.begin(), src5.end()));

  std::string src6 = "%2f";
  REQUIRE_EQ(std::string("/"), util::percentDecode(src6.begin(), src6.end()));
}

TEST_CASE("UtilTest2.testToString_binaryStream")
{
  std::shared_ptr<DiskWriter> dw(new ByteArrayDiskWriter());
  std::string data(16_k + 256, 'a');
  dw->initAndOpenFile();
  dw->writeData((const unsigned char*)data.c_str(), data.size(), 0);

  std::string readData = util::toString(dw);

  REQUIRE_EQ(data, readData);
}

TEST_CASE("UtilTest2.testNtoh64")
{
  uint64_t x = 0xff00ff00ee00ee00LL;
#ifdef WORDS_BIGENDIAN
  REQUIRE_EQ(x, ntoh64(x));
  REQUIRE_EQ(x, hton64(x));
#else  // !WORDS_BIGENDIAN
  uint64_t y = 0x00ee00ee00ff00ffLL;
  REQUIRE_EQ(y, ntoh64(x));
  REQUIRE_EQ(x, hton64(y));
#endif // !WORDS_BIGENDIAN
}

TEST_CASE("UtilTest2.testPercentEncode")
{
  REQUIRE_EQ(
      std::string("%3A%2F%3F%23%5B%5D%40%21%25%26%27%28%29%2A%2B%2C%3B%3D"),
      util::percentEncode(":/?#[]@!%&'()*+,;="));

  std::string unreserved = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                           "abcdefghijklmnopqrstuvwxyz"
                           "0123456789"
                           "-._~";
  REQUIRE_EQ(unreserved, util::percentEncode(unreserved));

  REQUIRE_EQ(std::string("1%5EA%20"), util::percentEncode("1^A "));
}

TEST_CASE("UtilTest2.testPercentEncodeMini")
{
  REQUIRE_EQ(std::string("%80"), util::percentEncodeMini({(char)0x80}));
}

TEST_CASE("UtilTest2.testHtmlEscape")
{
  REQUIRE_EQ(std::string("aria2&lt;&gt;&quot;&#39;util"),
             util::htmlEscape("aria2<>\"'util"));
}

TEST_CASE("UtilTest2.testGenerateRandomData")
{
  std::array<unsigned char, 66> data;
  data.fill(0xa5);
  util::generateRandomData(data.data() + 1, data.size() - 2);
  CHECK_EQ(0xa5, data.front());
  CHECK_EQ(0xa5, data.back());
  CHECK(std::any_of(data.begin() + 1, data.end() - 1,
                    [](unsigned char value) { return value != 0xa5; }));
  const auto before = data;
  util::generateRandomData(data.data() + 1, 0);
  CHECK(data == before);
}

TEST_CASE("UtilTest2.testFromHex")
{
  std::string src;
  std::string dest;

  src = "0011fF";
  dest = util::fromHex(src.begin(), src.end());
  REQUIRE_EQ((size_t)3, dest.size());
  REQUIRE_EQ((char)0x00, dest[0]);
  REQUIRE_EQ((char)0x11, dest[1]);
  REQUIRE_EQ((char)0xff, dest[2]);

  src = "0011f";
  REQUIRE(util::fromHex(src.begin(), src.end()).empty());

  src = "001g";
  REQUIRE(util::fromHex(src.begin(), src.end()).empty());
}

TEST_CASE("UtilTest2.testIsUtf8String")
{
  REQUIRE(util::isUtf8("ascii"));
  // "Hello World" in Japanese UTF-8
  REQUIRE(util::isUtf8(fromHex("e38193e38293e381abe381a1e381afe4b896e7958c")));
  // "World" in Shift_JIS
  REQUIRE(!util::isUtf8(fromHex("90a28a") + "E"));
  // UTF8-2
  REQUIRE(util::isUtf8(fromHex("c280")));
  REQUIRE(util::isUtf8(fromHex("dfbf")));
  // UTF8-3
  REQUIRE(util::isUtf8(fromHex("e0a080")));
  REQUIRE(util::isUtf8(fromHex("e0bf80")));
  REQUIRE(util::isUtf8(fromHex("e18080")));
  REQUIRE(util::isUtf8(fromHex("ec8080")));
  REQUIRE(util::isUtf8(fromHex("ed8080")));
  REQUIRE(util::isUtf8(fromHex("ed9f80")));
  REQUIRE(util::isUtf8(fromHex("ee8080")));
  REQUIRE(util::isUtf8(fromHex("ef8080")));
  // UTF8-4
  REQUIRE(util::isUtf8(fromHex("f0908080")));
  REQUIRE(util::isUtf8(fromHex("f0bf8080")));
  REQUIRE(util::isUtf8(fromHex("f1808080")));
  REQUIRE(util::isUtf8(fromHex("f3808080")));
  REQUIRE(util::isUtf8(fromHex("f4808080")));
  REQUIRE(util::isUtf8(fromHex("f48f8080")));

  REQUIRE(util::isUtf8(""));
  REQUIRE(!util::isUtf8(fromHex("00")));
}

} // namespace aria2
