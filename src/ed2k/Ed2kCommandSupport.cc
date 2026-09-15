/* <!-- copyright */
/*
 * aria2 - The high speed download utility
 *
 * Copyright (C) 2026 aria2-next contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
/* copyright --> */
#include "ARC4Encryptor.h"
#include "DlAbortEx.h"
#include "DownloadContext.h"
#include "DownloadEngine.h"
#include "Ed2kAttribute.h"
#include "PieceStorage.h"
#include "Request.h"
#include "a2functional.h"
#include "ed2k_aich.h"
#include "ed2k_constants.h"
#include "ed2k_hash.h"
#include "ed2k_link.h"
#include "ed2k_packet.h"
#include "ed2k_peer.h"
#include "fmt.h"
#include "prefs.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>
#include "ed2k/Ed2kCommandInternal.h"

namespace aria2::ed2k_command {

std::shared_ptr<Request> makeEd2kRequest(const ed2k::Endpoint& endpoint,
                                         bool serverMode)
{
  auto req = std::make_shared<Request>();
  req->setUri(fmt("ed2k-peer://%s:%u/", endpoint.host.c_str(), endpoint.port));
  if (serverMode) {
    req->setUri(
        fmt("ed2k-server://%s:%u/", endpoint.host.c_str(), endpoint.port));
  }
  return req;
}

bool rangesOverlap(const ed2k::PartRange& lhs, const ed2k::PartRange& rhs)
{
  return lhs.begin < rhs.end && rhs.begin < lhs.end;
}

bool blockRangeAvailable(Ed2kAttribute* attrs, const ed2k::PartRange& range)
{
  if (!attrs) {
    return false;
  }
  return std::none_of(attrs->requestedPartRanges.begin(),
                      attrs->requestedPartRanges.end(),
                      [&](const ed2k::PartRange& existing) {
                        return rangesOverlap(existing, range);
                      });
}

bool blockRangeAvailable(const std::vector<ed2k::PartRange>& ranges,
                         const ed2k::PartRange& range)
{
  return std::none_of(ranges.begin(), ranges.end(),
                      [&](const ed2k::PartRange& existing) {
                        return rangesOverlap(existing, range);
                      });
}

uint16_t localEd2kTcpPort(const DownloadEngine* e)
{
  auto port = e->getEd2kTcpPort();
  if (port != 0) {
    return port;
  }
  auto configured = e->getOption()->getAsInt(PREF_ED2K_LISTEN_PORT);
  if (configured > 0 &&
      configured <= static_cast<int>(std::numeric_limits<uint16_t>::max())) {
    return static_cast<uint16_t>(configured);
  }
  return 0;
}

uint16_t localEd2kUdpPort(const DownloadEngine* e)
{
  auto configured = e->getOption()->getAsInt(PREF_ED2K_UDP_LISTEN_PORT);
  if (configured > 0 &&
      configured <= static_cast<int>(std::numeric_limits<uint16_t>::max())) {
    return static_cast<uint16_t>(configured);
  }
  return 0;
}

std::string createPeerFileRequestPayload(const DownloadContext* dctx,
                                         PieceStorage* pieceStorage,
                                         const std::string& fileHash,
                                         uint8_t extendedRequestsVersion)
{
  auto payload = fileHash;
  if (extendedRequestsVersion == 0) {
    return payload;
  }

  const auto partCount = dctx->getNumPieces();
  if (partCount > std::numeric_limits<uint16_t>::max()) {
    throw DL_ABORT_EX("ED2K file has too many parts.");
  }
  payload += ed2k::packUInt16(static_cast<uint16_t>(partCount));

  for (size_t index = 0; index < partCount;) {
    uint8_t bits = 0;
    for (size_t bit = 0; bit < 8 && index < partCount; ++bit, ++index) {
      if (pieceStorage && pieceStorage->hasPiece(index)) {
        bits |= 1u << bit;
      }
    }
    payload.push_back(static_cast<char>(bits));
  }

  if (extendedRequestsVersion > 1) {
    payload += ed2k::packUInt16(0);
  }
  return payload;
}

bool isFileRequestPayloadForHash(const std::string& payload,
                                 const std::string& expectedHash)
{
  return payload.size() >= ed2k::HASH_LENGTH &&
         payload.compare(0, ed2k::HASH_LENGTH, expectedHash) == 0;
}

std::vector<bool> createLocalPartStatus(const DownloadContext* dctx,
                                        PieceStorage* pieceStorage)
{
  std::vector<bool> status(dctx->getNumPieces(), false);
  for (size_t index = 0; index < status.size(); ++index) {
    status[index] = pieceStorage && pieceStorage->hasPiece(index);
  }
  return status;
}

int64_t nowSeconds()
{
  return std::chrono::duration_cast<std::chrono::seconds>(
             global::wallclock().getTime().time_since_epoch())
      .count();
}

void storeAichRecoverySet(Ed2kAttribute* attrs,
                          const ed2k::AichRecoverySet& recoverySet)
{
  auto existing = std::find_if(attrs->aichRecoverySets.begin(),
                               attrs->aichRecoverySets.end(),
                               [&](const ed2k::AichRecoverySet& item) {
                                 return item.partIndex == recoverySet.partIndex;
                               });
  if (existing == attrs->aichRecoverySets.end()) {
    attrs->aichRecoverySets.push_back(recoverySet);
  }
  else {
    *existing = recoverySet;
  }
}

void discardArc4Prefix(ARC4Encryptor& rc4)
{
  std::array<unsigned char, 1_k> garbage;
  rc4.encrypt(garbage.size(), garbage.data(), garbage.data());
}

bool isEd2kProtocolMarker(uint8_t value)
{
  return value == ed2k::PROTO_EDONKEY || value == ed2k::PROTO_PACKED ||
         value == ed2k::PROTO_EMULE || value == ed2k::KAD_PROTOCOL;
}

void updatePeerUdpMetadata(Ed2kAttribute* attrs, const ed2k::Endpoint& endpoint,
                           const ed2k::EmulePeerInfo& info)
{
  auto state = getEd2kPeerState(attrs, endpoint);
  if (!state) {
    return;
  }
  if (info.udpPort != 0) {
    state->udpPort = info.udpPort;
  }
  if (info.miscOptions.udpVersion != 0) {
    state->udpVersion = info.miscOptions.udpVersion;
  }
  state->endpoint.cryptOptions = 0;
  if (info.miscOptions2.supportsCryptLayer) {
    state->endpoint.cryptOptions |= ed2k::SOURCE_CRYPT_SUPPORT;
  }
  if (info.miscOptions2.requestsCryptLayer) {
    state->endpoint.cryptOptions |= ed2k::SOURCE_CRYPT_REQUEST;
  }
  if (info.miscOptions2.requiresCryptLayer) {
    state->endpoint.cryptOptions |= ed2k::SOURCE_CRYPT_REQUIRE;
  }
}

} // namespace aria2::ed2k_command
