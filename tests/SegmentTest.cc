#include "a2functional.h"
#include <cstdint>
#include <memory>
#include "PiecedSegment.h"

#include "a2doctest.h"

#include "Piece.h"

namespace aria2 {

TEST_CASE("SegmentTest.testUpdateWrittenLength")
{
  std::shared_ptr<Piece> p(new Piece(0, 160_k));
  PiecedSegment s(160_k, p);
  REQUIRE_EQ((int64_t)0, s.getWrittenLength());

  s.updateWrittenLength(16_k);
  REQUIRE(p->hasBlock(0));
  REQUIRE(!p->hasBlock(1));

  s.updateWrittenLength(16_k * 9);
  REQUIRE(p->pieceComplete());
}

TEST_CASE("SegmentTest.testUpdateWrittenLength_lastPiece")
{
  std::shared_ptr<Piece> p(new Piece(0, 16_k * 9 + 1));
  PiecedSegment s(160_k, p);

  s.updateWrittenLength(p->getLength());
  REQUIRE(p->pieceComplete());
}

TEST_CASE("SegmentTest.testUpdateWrittenLength_incompleteLastPiece")
{
  std::shared_ptr<Piece> p(new Piece(0, 16_k * 9 + 2));
  PiecedSegment s(160_k, p);

  s.updateWrittenLength(16_k * 9 + 1);
  REQUIRE(!p->pieceComplete());
  s.updateWrittenLength(1);
  REQUIRE(p->pieceComplete());
}

TEST_CASE("SegmentTest.testClear")
{
  std::shared_ptr<Piece> p(new Piece(0, 160_k));
  PiecedSegment s(160_k, p);
  s.updateWrittenLength(160_k);
  REQUIRE_EQ((int64_t)160_k, s.getWrittenLength());
  s.clear(nullptr);
  REQUIRE_EQ((int64_t)0, s.getWrittenLength());
}

} // namespace aria2
