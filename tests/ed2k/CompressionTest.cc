/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
#include <cstdint>
#include <string>
#include "ed2k_aich.h"
#include "ed2k_compression.h"
#include "ed2k_packet.h"

#include "a2doctest.h"
#include <cstring>
#include <zlib.h>

#include "support/Encoding.h"

namespace aria2::ed2k {

TEST_CASE("Ed2kHelperTest.testCompressedPartPayloads")
{
  std::string fileHashHex("0123456789abcdef0123456789abcdef");
  auto fileHash = util::fromHex(fileHashHex.begin(), fileHashHex.end());

  auto payload32 = fileHash + packUInt32(184320) + packUInt32(4) + "data";
  CompressedPartHeader header;
  std::string compressedData;
  REQUIRE(parseCompressedPartPayload(header, compressedData, payload32,
                                     fileHash, false));
  REQUIRE_EQ((int64_t)184320, header.begin);
  REQUIRE_EQ((uint32_t)4, header.totalCompressedLength);
  REQUIRE_EQ(std::string("data"), compressedData);

  auto payload64 = fileHash + packUInt64(0x100000001LL) + packUInt32(7) + "xy";
  REQUIRE(parseCompressedPartPayload(header, compressedData, payload64,
                                     fileHash, true));
  REQUIRE_EQ((int64_t)0x100000001LL, header.begin);
  REQUIRE_EQ((uint32_t)7, header.totalCompressedLength);
  REQUIRE_EQ(std::string("xy"), compressedData);

  auto bad = fileHash + packUInt32(0) + packUInt32(3) + "tiny";
  REQUIRE(!parseCompressedPartPayload(header, compressedData, bad, fileHash,
                                      false));
}

TEST_CASE("Ed2kHelperTest.testInflateCompressedPartData")
{
  std::string input;
  for (int i = 0; i < 8192; ++i) {
    input.push_back(static_cast<char>('A' + (i % 23)));
  }

  z_stream strm;
  memset(&strm, 0, sizeof(strm));
  REQUIRE_EQ(Z_OK, deflateInit(&strm, Z_DEFAULT_COMPRESSION));
  strm.avail_in = input.size();
  strm.next_in = reinterpret_cast<unsigned char*>(&input[0]);
  std::string compressed(compressBound(input.size()), '\0');
  strm.avail_out = compressed.size();
  strm.next_out = reinterpret_cast<unsigned char*>(&compressed[0]);
  REQUIRE_EQ(Z_STREAM_END, deflate(&strm, Z_FINISH));
  compressed.resize(compressed.size() - strm.avail_out);
  REQUIRE_EQ(Z_OK, deflateEnd(&strm));

  std::string inflated;
  REQUIRE(inflateCompressedPartData(inflated, compressed, input.size()));
  REQUIRE_EQ(input, inflated);

  REQUIRE(!inflateCompressedPartData(inflated, compressed, input.size() - 1));
  REQUIRE(!inflateCompressedPartData(inflated, "not zlib", input.size()));
}

TEST_CASE(
    "Ed2kHelperTest.testCompressedPartInflaterKeepsBlockOwnerAcrossChunks")
{
  std::string input;
  for (int i = 0; i < 220000; ++i) {
    input.push_back(static_cast<char>('A' + (i % 5)));
  }

  z_stream strm;
  memset(&strm, 0, sizeof(strm));
  REQUIRE_EQ(Z_OK, deflateInit(&strm, Z_DEFAULT_COMPRESSION));
  strm.avail_in = 98304;
  strm.next_in = reinterpret_cast<unsigned char*>(&input[0]);
  std::string compressed(compressBound(input.size()), '\0');
  strm.avail_out = compressed.size();
  strm.next_out = reinterpret_cast<unsigned char*>(&compressed[0]);
  REQUIRE_EQ(Z_OK, deflate(&strm, Z_SYNC_FLUSH));
  const auto firstCompressedLength = compressed.size() - strm.avail_out;
  strm.avail_in = input.size() - 98304;
  strm.next_in = reinterpret_cast<unsigned char*>(&input[98304]);
  REQUIRE_EQ(Z_STREAM_END, deflate(&strm, Z_FINISH));
  compressed.resize(compressed.size() - strm.avail_out);
  REQUIRE_EQ(Z_OK, deflateEnd(&strm));

  CompressedPartInflater inflater;
  std::string first;
  REQUIRE(inflater.inflateChunk(
      first, compressed.substr(0, firstCompressedLength), 0, 98304));
  REQUIRE(inflater.active());
  REQUIRE_EQ(static_cast<int64_t>(0), inflater.blockBegin());
  REQUIRE_EQ(static_cast<int64_t>(first.size()), inflater.inflatedLength());
  REQUIRE_EQ(input.substr(0, 98304), first);

  std::string second;
  REQUIRE(inflater.inflateChunk(second,
                                compressed.substr(firstCompressedLength), 0,
                                input.size() - first.size()));
  REQUIRE(!inflater.active());
  REQUIRE_EQ(input, first + second);
}

TEST_CASE("Ed2kHelperTest.testInflatePackedPacketPayload")
{
  std::string input;
  for (int i = 0; i < 2048; ++i) {
    input.push_back(static_cast<char>('a' + (i % 7)));
  }

  z_stream strm;
  memset(&strm, 0, sizeof(strm));
  REQUIRE_EQ(Z_OK, deflateInit(&strm, Z_DEFAULT_COMPRESSION));
  strm.avail_in = input.size();
  strm.next_in = reinterpret_cast<unsigned char*>(&input[0]);
  std::string compressed(compressBound(input.size()), '\0');
  strm.avail_out = compressed.size();
  strm.next_out = reinterpret_cast<unsigned char*>(&compressed[0]);
  REQUIRE_EQ(Z_STREAM_END, deflate(&strm, Z_FINISH));
  compressed.resize(compressed.size() - strm.avail_out);
  REQUIRE_EQ(Z_OK, deflateEnd(&strm));

  std::string inflated;
  REQUIRE(inflatePackedPacketPayload(inflated, compressed, input.size() + 100));
  REQUIRE_EQ(input, inflated);

  REQUIRE(!inflatePackedPacketPayload(inflated, compressed, input.size() - 1));
  REQUIRE(!inflatePackedPacketPayload(inflated, "not zlib", input.size()));
}

} // namespace aria2::ed2k
