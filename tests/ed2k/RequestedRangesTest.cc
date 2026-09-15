/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
#include "DiskAdaptor.h"
#include "File.h"
#include "ContextAttribute.h"
#include <cstdint>
#include <memory>
#include "download_helper.h"

#include <string>
#include <algorithm>
#include <cstdlib>
#include <vector>

#include "a2doctest.h"

#include "RequestGroup.h"
#include "DownloadEngine.h"
#include "DownloadContext.h"
#include "DefaultPieceStorage.h"
#include "DlRetryEx.h"
#include "Ed2kAttribute.h"
#include "Ed2kKadCommand.h"
#include "Ed2kPeerTransfer.h"
#include "Ed2kUploadQueue.h"
#include "Option.h"
#include "Piece.h"
#include "RequestGroupMan.h"
#include "Segment.h"
#include "SegmentMan.h"
#include "ed2k_aich.h"
#include "ed2k_endpoint.h"
#include "ed2k_hash.h"
#include "ed2k_link.h"
#include "ed2k_peer.h"
#include "prefs.h"
#include "TestUtil.h"
#ifdef ENABLE_BITTORRENT
#  include "BtDownload.h"
#endif // ENABLE_BITTORRENT

namespace aria2 {

TEST_CASE(
    "DownloadHelperTest.testEd2kPeerTransferRemovesCompletedRequestedRanges")
{
  Ed2kAttribute attrs;
  ed2k::Endpoint peer;
  peer.host = "203.0.113.10";
  peer.port = 4662;
  addEd2kPeer(&attrs, peer, ed2k::PEER_SOURCE_SERVER);

  std::vector<ed2k::PartRange> ranges;
  ed2k::PartRange range;
  range.begin = 0;
  range.end = 10;
  ranges.push_back(range);
  range.begin = 10;
  range.end = 20;
  ranges.push_back(range);
  REQUIRE(updateEd2kPeerRequestedParts(&attrs, peer, ranges, 100));

  REQUIRE_EQ((size_t)1,
             removeEd2kPeerCompletedRequestedRange(&attrs, peer, 0, 10, 120));
  auto state = getEd2kPeerState(&attrs, peer);
  REQUIRE(state);
  REQUIRE_EQ((size_t)1, state->requestedParts.size());
  REQUIRE_EQ((int64_t)10, state->requestedParts[0].begin);
  REQUIRE_EQ((int64_t)20, state->requestedParts[0].end);
  REQUIRE_EQ((int64_t)120, state->lastTransferProgressTime);

  REQUIRE_EQ((size_t)1,
             removeEd2kPeerCompletedRequestedRange(&attrs, peer, 10, 20, 125));
  REQUIRE(state->requestedParts.empty());
  REQUIRE_EQ((int64_t)125, state->lastTransferProgressTime);
}

TEST_CASE("Ed2kRequestedRanges.completeInteriorWithOtherRequests")
{
  Ed2kAttribute attrs;
  ed2k::Endpoint peer;
  peer.host = "203.0.113.10";
  peer.port = 4662;
  addEd2kPeer(&attrs, peer, ed2k::PEER_SOURCE_SERVER);
  REQUIRE(
      updateEd2kPeerRequestedParts(&attrs, peer, {{0, 100}, {200, 300}}, 100));
  auto state = getEd2kPeerState(&attrs, peer);
  REQUIRE(state);
  state->cancelTransferSent = true;

  REQUIRE(removeEd2kPeerCompletedRequestedRange(&attrs, peer, 20, 80, 120) ==
          1);
  REQUIRE(state->requestedParts.size() == 3);
  CHECK(state->requestedParts[0].begin == 0);
  CHECK(state->requestedParts[0].end == 20);
  CHECK(state->requestedParts[1].begin == 200);
  CHECK(state->requestedParts[1].end == 300);
  CHECK(state->requestedParts[2].begin == 80);
  CHECK(state->requestedParts[2].end == 100);
  CHECK(state->lastTransferProgressTime == 120);
  CHECK_FALSE(state->cancelTransferSent);
  REQUIRE(attrs.requestedPartRanges.size() == 3);
  for (const auto& pending : state->requestedParts) {
    CHECK(std::count_if(attrs.requestedPartRanges.begin(),
                        attrs.requestedPartRanges.end(),
                        [&](const ed2k::PartRange& tracked) {
                          return tracked.begin == pending.begin &&
                                 tracked.end == pending.end;
                        }) == 1);
  }

  REQUIRE(removeEd2kPeerCompletedRequestedRange(&attrs, peer, 0, 300, 130) ==
          3);
  CHECK(state->requestedParts.empty());
  CHECK(attrs.requestedPartRanges.empty());
  CHECK(state->lastTransferProgressTime == 130);
}

TEST_CASE("DownloadHelperTest.testEd2kPeerTransferExpiresStalledRequests")
{
  auto option = std::make_shared<Option>();
  auto dctx = std::make_shared<DownloadContext>(
      ed2k::PIECE_LENGTH, static_cast<int64_t>(ed2k::PIECE_LENGTH) * 2,
      "aria2-next-ed2k.bin");
  auto attrs = std::make_shared<Ed2kAttribute>();
  dctx->setAttribute(CTX_ATTR_ED2K, attrs);
  auto pieceStorage = std::make_shared<DefaultPieceStorage>(dctx, option.get());
  auto segmentMan = std::make_shared<SegmentMan>(dctx, pieceStorage);
  auto segment = segmentMan->getSegmentWithIndex(7, 0);
  REQUIRE(segment);

  ed2k::Endpoint peer;
  peer.host = "203.0.113.10";
  peer.port = 4662;
  addEd2kPeer(attrs.get(), peer, ed2k::PEER_SOURCE_SERVER);
  std::vector<ed2k::PartRange> ranges;
  ed2k::PartRange range;
  range.begin = 0;
  range.end = 10;
  ranges.push_back(range);
  REQUIRE(updateEd2kPeerRequestedParts(attrs.get(), peer, ranges, 100));
  auto state = getEd2kPeerState(attrs.get(), peer);
  state->accepted = true;

  REQUIRE(!expireEd2kStalledPeerTransfer(attrs.get(), segmentMan.get(), peer, 7,
                                         129, 30, 15));
  REQUIRE_EQ((size_t)1, state->requestedParts.size());

  REQUIRE(expireEd2kStalledPeerTransfer(attrs.get(), segmentMan.get(), peer, 7,
                                        130, 30, 15));
  REQUIRE(state->requestedParts.empty());
  REQUIRE(state->queued);
  REQUIRE(!state->accepted);
  REQUIRE(state->dead);
  REQUIRE_EQ((uint32_t)1, state->failCount);
  REQUIRE_EQ((int64_t)145, state->nextRetryTime);

  std::vector<std::shared_ptr<Segment>> inFlight;
  segmentMan->getInFlightSegment(inFlight, 7);
  REQUIRE(inFlight.empty());
}

TEST_CASE("DownloadHelperTest.testEd2kPeerTransferReclaimsStalledEndgameRange")
{
  Ed2kAttribute attrs;
  attrs.link.size = static_cast<int64_t>(ed2k::PIECE_LENGTH) * 2;

  ed2k::Endpoint slowPeer;
  slowPeer.host = "203.0.113.10";
  slowPeer.port = 4662;
  ed2k::Endpoint fastPeer;
  fastPeer.host = "203.0.113.11";
  fastPeer.port = 4662;

  addEd2kPeer(&attrs, slowPeer, ed2k::PEER_SOURCE_SERVER);
  addEd2kPeer(&attrs, fastPeer, ed2k::PEER_SOURCE_SERVER);

  std::vector<ed2k::PartRange> ranges;
  ed2k::PartRange range;
  range.begin = 0;
  range.end = Piece::BLOCK_LENGTH;
  ranges.push_back(range);
  REQUIRE(updateEd2kPeerRequestedParts(&attrs, slowPeer, ranges, 100));

  auto slowState = getEd2kPeerState(&attrs, slowPeer);
  auto fastState = getEd2kPeerState(&attrs, fastPeer);
  REQUIRE(slowState);
  REQUIRE(fastState);
  slowState->accepted = true;
  fastState->accepted = true;
  fastState->partStatus.push_back(true);
  fastState->partStatus.push_back(false);

  ed2k::PartRange reclaimed;
  REQUIRE(!reclaimEd2kStalledRequestedRange(
      &attrs, fastPeer, fastState->partStatus, 109, 10, reclaimed));
  REQUIRE_EQ((size_t)1, slowState->requestedParts.size());

  REQUIRE(reclaimEd2kStalledRequestedRange(
      &attrs, fastPeer, fastState->partStatus, 110, 10, reclaimed));
  REQUIRE_EQ((int64_t)0, reclaimed.begin);
  REQUIRE_EQ(static_cast<int64_t>(Piece::BLOCK_LENGTH), reclaimed.end);
  REQUIRE(slowState->requestedParts.empty());
  REQUIRE(slowState->cancelTransferSent);
  REQUIRE(attrs.requestedPartRanges.empty());

  REQUIRE(updateEd2kPeerRequestedParts(
      &attrs, fastPeer, std::vector<ed2k::PartRange>{reclaimed}, 110));
  REQUIRE_EQ((size_t)1, attrs.requestedPartRanges.size());
  REQUIRE_EQ((int64_t)0, attrs.requestedPartRanges[0].begin);
}

TEST_CASE("DownloadHelperTest.testEd2kPeerTransferActivelyReclaimsStalledRange")
{
  Ed2kAttribute attrs;
  attrs.link.size = static_cast<int64_t>(ed2k::PIECE_LENGTH) * 2;

  ed2k::Endpoint slowPeer;
  slowPeer.host = "203.0.113.10";
  slowPeer.port = 4662;
  ed2k::Endpoint fastPeer;
  fastPeer.host = "203.0.113.11";
  fastPeer.port = 4662;

  addEd2kPeer(&attrs, slowPeer, ed2k::PEER_SOURCE_SERVER);
  addEd2kPeer(&attrs, fastPeer, ed2k::PEER_SOURCE_SERVER);

  ed2k::PartRange range;
  range.begin = 0;
  range.end = Piece::BLOCK_LENGTH;
  REQUIRE(updateEd2kPeerRequestedParts(
      &attrs, slowPeer, std::vector<ed2k::PartRange>{range}, 100));

  auto slowState = getEd2kPeerState(&attrs, slowPeer);
  auto fastState = getEd2kPeerState(&attrs, fastPeer);
  REQUIRE(slowState);
  REQUIRE(fastState);
  slowState->accepted = true;
  fastState->accepted = true;
  fastState->partStatus.push_back(true);
  fastState->partStatus.push_back(false);

  ed2k::PartRange reclaimed;
  REQUIRE(!activelyReclaimEd2kStalledRequestedRange(
      &attrs, fastPeer, fastState->partStatus, 159, reclaimed));
  REQUIRE_EQ((size_t)1, slowState->requestedParts.size());

  REQUIRE(activelyReclaimEd2kStalledRequestedRange(
      &attrs, fastPeer, fastState->partStatus, 160, reclaimed));
  REQUIRE_EQ((int64_t)0, reclaimed.begin);
  REQUIRE_EQ(static_cast<int64_t>(Piece::BLOCK_LENGTH), reclaimed.end);
  REQUIRE(slowState->requestedParts.empty());
  REQUIRE(slowState->cancelTransferSent);
  REQUIRE(!slowState->dead);
  REQUIRE_EQ((size_t)0, attrs.requestedPartRanges.size());
}

TEST_CASE("DownloadHelperTest.testEd2kPeerTransferIgnoresDuplicateData")
{
  auto option = std::make_shared<Option>();
  const std::string outdir = A2_TEST_OUT_DIR "/ed2k-transfer-duplicate";
  const std::string outfile = outdir + "/aria2 next duplicate transfer.bin";
  File(outfile).remove();
  File(outdir).mkdirs();

  const std::string data = "verified ed2k data";
  auto dctx = std::make_shared<DownloadContext>(
      ed2k::PIECE_LENGTH, static_cast<int64_t>(data.size()), outfile);
  auto group = std::make_shared<RequestGroup>(GroupId::create(), option);
  group->setDownloadContext(dctx);
  auto attrs = std::make_shared<Ed2kAttribute>();
  attrs->link.hash = ed2k::md4Digest(data);
  attrs->pieceHashes.push_back(attrs->link.hash);
  dctx->setAttribute(CTX_ATTR_ED2K, attrs);
  option->put(PREF_FILE_ALLOCATION, V_NONE);
  auto pieceStorage = std::make_shared<DefaultPieceStorage>(dctx, option.get());
  pieceStorage->initStorage();
  pieceStorage->getDiskAdaptor()->openFile();
  auto segmentMan = std::make_shared<SegmentMan>(dctx, pieceStorage);
  REQUIRE(segmentMan->getSegmentWithIndex(1, 0));

  ed2k::PeerTransfer transfer(dctx.get(), pieceStorage.get(), segmentMan.get(),
                              1);
  REQUIRE(!transfer.writePartData(0, data.substr(0, 8)));
  REQUIRE(!transfer.writePartData(0, data.substr(0, 8)));
  auto completed = transfer.writePartData(0, data);
  REQUIRE(completed);
  REQUIRE(transfer.completeVerifiedSegment(completed));
  REQUIRE(!transfer.writePartData(0, data));
}

TEST_CASE("DownloadHelperTest.testEd2kPeerTransferAcceptsParallelPieceBlocks")
{
  auto option = std::make_shared<Option>();
  const std::string outdir = A2_TEST_OUT_DIR "/ed2k-transfer-parallel";
  const std::string outfile = outdir + "/aria2 next parallel transfer.bin";
  File(outfile).remove();
  File(outdir).mkdirs();

  const std::string first(Piece::BLOCK_LENGTH, 'a');
  const std::string second(Piece::BLOCK_LENGTH, 'b');
  const auto data = first + second;
  auto dctx = std::make_shared<DownloadContext>(
      ed2k::PIECE_LENGTH, static_cast<int64_t>(data.size()), outfile);
  auto group = std::make_shared<RequestGroup>(GroupId::create(), option);
  group->setDownloadContext(dctx);
  auto attrs = std::make_shared<Ed2kAttribute>();
  attrs->link.hash = ed2k::md4Digest(data);
  attrs->pieceHashes.push_back(attrs->link.hash);
  dctx->setAttribute(CTX_ATTR_ED2K, attrs);
  option->put(PREF_FILE_ALLOCATION, V_NONE);
  auto pieceStorage = std::make_shared<DefaultPieceStorage>(dctx, option.get());
  pieceStorage->initStorage();
  pieceStorage->getDiskAdaptor()->openFile();
  auto segmentMan = std::make_shared<SegmentMan>(dctx, pieceStorage);

  REQUIRE(segmentMan->getSegmentWithIndex(1, 0));
  ed2k::PeerTransfer firstPeer(dctx.get(), pieceStorage.get(), segmentMan.get(),
                               1);
  REQUIRE(!firstPeer.writePartData(0, first));

  ed2k::PeerTransfer secondPeer(dctx.get(), pieceStorage.get(),
                                segmentMan.get(), 2);
  auto completed = secondPeer.writePartData(first.size(), second);
  REQUIRE(completed);
  REQUIRE(secondPeer.completeVerifiedSegment(completed));
  REQUIRE(pieceStorage->hasPiece(0));
}

TEST_CASE("DownloadHelperTest."
          "testEd2kPeerTransferCancelsOwnerAfterParallelHashFailure")
{
  auto option = std::make_shared<Option>();
  const std::string outdir = A2_TEST_OUT_DIR "/ed2k-transfer-parallel-bad";
  const std::string outfile = outdir + "/aria2 next parallel bad transfer.bin";
  File(outfile).remove();
  File(outdir).mkdirs();

  const std::string first(Piece::BLOCK_LENGTH, 'a');
  const std::string second(Piece::BLOCK_LENGTH, 'b');
  const auto data = first + second;
  std::string corruptSecond = second;
  corruptSecond[0] = 'x';
  auto dctx = std::make_shared<DownloadContext>(
      ed2k::PIECE_LENGTH, static_cast<int64_t>(data.size()), outfile);
  auto group = std::make_shared<RequestGroup>(GroupId::create(), option);
  group->setDownloadContext(dctx);
  auto attrs = std::make_shared<Ed2kAttribute>();
  attrs->link.hash = ed2k::md4Digest(data);
  attrs->pieceHashes.push_back(attrs->link.hash);
  dctx->setAttribute(CTX_ATTR_ED2K, attrs);
  option->put(PREF_FILE_ALLOCATION, V_NONE);
  auto pieceStorage = std::make_shared<DefaultPieceStorage>(dctx, option.get());
  pieceStorage->initStorage();
  pieceStorage->getDiskAdaptor()->openFile();
  auto segmentMan = std::make_shared<SegmentMan>(dctx, pieceStorage);

  REQUIRE(segmentMan->getSegmentWithIndex(1, 0));
  ed2k::PeerTransfer firstPeer(dctx.get(), pieceStorage.get(), segmentMan.get(),
                               1);
  REQUIRE(!firstPeer.writePartData(0, first));

  ed2k::PeerTransfer secondPeer(dctx.get(), pieceStorage.get(),
                                segmentMan.get(), 2);
  REQUIRE_THROWS_AS(secondPeer.writePartData(first.size(), corruptSecond),
                    DlRetryEx);
  REQUIRE(!pieceStorage->isPieceUsed(0));
}

TEST_CASE("DownloadHelperTest.testEd2kPeerTransferAppliesAichRecoveryData")
{
  auto option = std::make_shared<Option>();
  const std::string outdir = A2_TEST_OUT_DIR "/ed2k-transfer-aich-recovery";
  const std::string outfile = outdir + "/aria2 next aich transfer.bin";
  File(outfile).remove();
  File(outdir).mkdirs();

  std::string block0(ed2k::EMBLOCK_LENGTH, 'a');
  std::string block1(ed2k::EMBLOCK_LENGTH, 'b');
  std::string block2(100, 'c');
  const auto data = block0 + block1 + block2;
  std::string corruptData = data;
  corruptData[block0.size()] = 'x';
  auto dctx = std::make_shared<DownloadContext>(
      ed2k::PIECE_LENGTH, static_cast<int64_t>(data.size()), outfile);
  auto attrs = std::make_shared<Ed2kAttribute>();
  attrs->link.hash = ed2k::md4Digest(data);
  attrs->aichRootHash = ed2k::aichRootHash(data.data(), data.size());
  attrs->pieceHashes.push_back(attrs->link.hash);
  ed2k::AichRecoverySet recoverySet;
  recoverySet.partIndex = 0;
  ed2k::AichRecoveryBlock recoveryBlock;
  recoveryBlock.offset = 0;
  recoveryBlock.length = block0.size();
  recoveryBlock.hash = ed2k::aichHash(block0);
  recoverySet.blocks.push_back(recoveryBlock);
  recoveryBlock.offset = block0.size();
  recoveryBlock.length = block1.size();
  recoveryBlock.hash = ed2k::aichHash(block1);
  recoverySet.blocks.push_back(recoveryBlock);
  recoveryBlock.offset = block0.size() + block1.size();
  recoveryBlock.length = block2.size();
  recoveryBlock.hash = ed2k::aichHash(block2);
  recoverySet.blocks.push_back(recoveryBlock);
  attrs->aichRecoverySets.push_back(recoverySet);
  dctx->setAttribute(CTX_ATTR_ED2K, attrs);
  option->put(PREF_FILE_ALLOCATION, V_NONE);
  auto group = std::make_shared<RequestGroup>(GroupId::create(), option);
  group->setDownloadContext(dctx);
  auto pieceStorage = std::make_shared<DefaultPieceStorage>(dctx, option.get());
  pieceStorage->initStorage();
  pieceStorage->getDiskAdaptor()->openFile();
  group->setPieceStorage(pieceStorage);
  auto segmentMan = std::make_shared<SegmentMan>(dctx, pieceStorage);
  auto segment = segmentMan->getSegmentWithIndex(1, 0);
  REQUIRE(segment);

  ed2k::PeerTransfer transfer(dctx.get(), pieceStorage.get(), segmentMan.get(),
                              1);
  REQUIRE_THROWS_AS(transfer.writePartData(0, corruptData), DlRetryEx);

  auto piece = pieceStorage->getPiece(0);
  REQUIRE(piece);
  REQUIRE(piece->hasBlock(0));
  REQUIRE(!piece->hasBlock(ed2k::EMBLOCK_LENGTH / piece->getBlockLength()));
  REQUIRE(!piece->pieceComplete());
}

} // namespace aria2
