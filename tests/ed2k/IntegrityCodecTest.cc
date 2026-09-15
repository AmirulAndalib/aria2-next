/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
#include "RecoverableException.h"
#include <cstdint>
#include <string>
#include <vector>
#include "ed2k_aich.h"
#include "ed2k_hash.h"
#include "ed2k_packet.h"

#include "a2doctest.h"
#include <cstring>

#include "support/Encoding.h"

namespace aria2::ed2k {

TEST_CASE("Ed2kHelperTest.testAichPayloads")
{
  std::string fileHashHex("0123456789abcdef0123456789abcdef");
  auto fileHash = util::fromHex(fileHashHex.begin(), fileHashHex.end());
  std::string aichRootHex("1111111111111111111111111111111111111111");
  auto aichRoot = util::fromHex(aichRootHex.begin(), aichRootHex.end());

  auto fileHashRequest = createAichFileHashRequestPayload(fileHash);
  REQUIRE_EQ(fileHash, fileHashRequest);

  AichFileHashAnswer fileHashAnswer;
  REQUIRE(parseAichFileHashAnswerPayload(
      fileHashAnswer, createAichFileHashAnswerPayload(fileHash, aichRoot),
      fileHash));
  REQUIRE_EQ(fileHash, fileHashAnswer.fileHash);
  REQUIRE_EQ(aichRoot, fileHashAnswer.rootHash);

  auto request = createAichRequestPayload(fileHash, 7, aichRoot);
  AichRequest parsedRequest;
  REQUIRE(parseAichRequestPayload(parsedRequest, request, fileHash));
  REQUIRE_EQ(fileHash, parsedRequest.fileHash);
  REQUIRE_EQ((uint16_t)7, parsedRequest.partIndex);
  REQUIRE_EQ(aichRoot, parsedRequest.rootHash);

  std::string recovery;
  recovery += packUInt16(3);
  recovery += std::string(20, '\x22');
  auto answer = createAichAnswerPayload(fileHash, 7, aichRoot, recovery);
  AichAnswer parsedAnswer;
  REQUIRE(parseAichAnswerPayload(parsedAnswer, answer, fileHash));
  REQUIRE(!parsedAnswer.failed);
  REQUIRE_EQ(fileHash, parsedAnswer.fileHash);
  REQUIRE_EQ((uint16_t)7, parsedAnswer.partIndex);
  REQUIRE_EQ(aichRoot, parsedAnswer.rootHash);
  REQUIRE_EQ(recovery, parsedAnswer.recoveryData);

  REQUIRE(parseAichAnswerPayload(parsedAnswer, fileHash, fileHash));
  REQUIRE(parsedAnswer.failed);
  REQUIRE_EQ(fileHash, parsedAnswer.fileHash);
  REQUIRE(parsedAnswer.rootHash.empty());
  REQUIRE(parsedAnswer.recoveryData.empty());

  REQUIRE(
      !parseAichRequestPayload(parsedRequest, request, std::string(16, '\0')));
}

TEST_CASE("Ed2kHelperTest.testAichRecoveryData")
{
  std::string block0(EMBLOCK_LENGTH, 'a');
  std::string block1(EMBLOCK_LENGTH, 'b');
  std::string block2(100, 'c');
  const auto hash0 = aichHash(block0);
  const auto hash1 = aichHash(block1);
  const auto hash2 = aichHash(block2);
  const auto data = block0 + block1 + block2;
  const auto root = aichRootHash(data.data(), data.size());
  std::string recovery;
  recovery += packUInt16(3);
  recovery += packUInt16(7);
  recovery += hash0;
  recovery += packUInt16(6);
  recovery += hash1;
  recovery += packUInt16(2);
  recovery += hash2;
  recovery += packUInt16(0);

  AichRecoveryData parsed;
  REQUIRE(parseAichRecoveryData(
      parsed, recovery, block0.size() + block1.size() + block2.size(), false));
  REQUIRE(verifyAichRecoveryData(
      parsed, root, block0.size() + block1.size() + block2.size(), 0));
  AichRecoverySet recoverySet;
  REQUIRE(buildAichRecoverySet(recoverySet, parsed, root,
                               block0.size() + block1.size() + block2.size(),
                               0));
  REQUIRE_EQ((size_t)3, recoverySet.blocks.size());
  REQUIRE_EQ(hash1, recoverySet.blocks[1].hash);
  recovery[4] ^= 0x01;
  REQUIRE(parseAichRecoveryData(
      parsed, recovery, block0.size() + block1.size() + block2.size(), false));
  REQUIRE(!verifyAichRecoveryData(
      parsed, root, block0.size() + block1.size() + block2.size(), 0));
}

TEST_CASE("Ed2kHelperTest.testAichHashTree")
{
  std::string data;
  for (int i = 0; i < 450000; ++i) {
    data.push_back(static_cast<char>('a' + (i % 26)));
  }

  auto firstBlock = aichHash(data.data(), EMBLOCK_LENGTH);
  auto secondBlock = aichHash(data.data() + EMBLOCK_LENGTH, EMBLOCK_LENGTH);
  auto thirdBlock = aichHash(data.data() + EMBLOCK_LENGTH * 2,
                             data.size() - EMBLOCK_LENGTH * 2);
  REQUIRE_EQ((size_t)40, util::toHex(firstBlock).size());
  REQUIRE_EQ(firstBlock, aichRootHash(data.data(), EMBLOCK_LENGTH));
  auto left = aichHash(firstBlock + secondBlock);
  REQUIRE_EQ(aichHash(left + thirdBlock),
             aichRootHash(data.data(), data.size()));

  std::vector<std::string> leaves{firstBlock, secondBlock, thirdBlock};
  REQUIRE_EQ(aichHash(left + thirdBlock), aichRootHash(leaves));

  REQUIRE_THROWS_AS(
      aichRootHash(std::vector<std::string>{std::string(19, '\0')}),
      RecoverableException);
}

TEST_CASE("Ed2kHelperTest.testAichHashTreeKeepsPartLevel")
{
  std::string firstPart(PIECE_LENGTH, 'a');
  std::string secondPart(EMBLOCK_LENGTH, 'b');
  const auto expected =
      aichHash(aichRootHash(firstPart.data(), firstPart.size()) +
               aichRootHash(secondPart.data(), secondPart.size()));
  const auto data = firstPart + secondPart;
  const auto flatRoot = aichRootHash(data.data(), data.size());

  REQUIRE_EQ(expected, flatRoot);
}

TEST_CASE("Ed2kHelperTest.testMd4Digest")
{
  REQUIRE_EQ(std::string("31d6cfe0d16ae931b73c59d7e0c089c0"),
             util::toHex(md4Digest("")));
  REQUIRE_EQ(std::string("bde52cb31de33e46245e05fbdbd6fb24"),
             util::toHex(md4Digest("a")));
  REQUIRE_EQ(std::string("a448017aaf21d8525fc10ae87aa6729d"),
             util::toHex(md4Digest("abc")));
  REQUIRE_EQ(std::string("d9130a8164549fe818874806e1c7014b"),
             util::toHex(md4Digest("message digest")));
}

TEST_CASE("Ed2kHelperTest.testRootHash")
{
  std::vector<std::string> empty;
  REQUIRE_EQ(std::string("31d6cfe0d16ae931b73c59d7e0c089c0"),
             util::toHex(rootHash(empty)));

  std::vector<std::string> single{md4Digest("abc")};
  REQUIRE_EQ(util::toHex(md4Digest("abc")), util::toHex(rootHash(single)));

  std::vector<std::string> multiple{md4Digest("first part"),
                                    md4Digest("second part")};
  std::string concat = multiple[0] + multiple[1];
  REQUIRE_EQ(util::toHex(md4Digest(concat)), util::toHex(rootHash(multiple)));
}

TEST_CASE("Ed2kHelperTest.testHashSetPartCount")
{
  REQUIRE_EQ((size_t)0, hashSetPartCount(1));
  REQUIRE_EQ((size_t)0,
             hashSetPartCount(static_cast<int64_t>(PIECE_LENGTH) - 1));
  REQUIRE_EQ((size_t)2, hashSetPartCount(static_cast<int64_t>(PIECE_LENGTH)));
  REQUIRE_EQ((size_t)2,
             hashSetPartCount(static_cast<int64_t>(PIECE_LENGTH) + 1));
  REQUIRE_EQ((size_t)3,
             hashSetPartCount(static_cast<int64_t>(PIECE_LENGTH) * 2));
}

} // namespace aria2::ed2k
