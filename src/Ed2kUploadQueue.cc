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
#include "Ed2kUploadQueue.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include "RequestGroupMan.h"
#include "ed2k_hash.h"
#include "util.h"

namespace aria2 {

namespace ed2k {

namespace {

bool sameEndpoint(const Endpoint& lhs, const Endpoint& rhs)
{
  return lhs.host == rhs.host && lhs.port == rhs.port;
}

bool parseUint64(uint64_t& result, const std::string& value)
{
  char* end = nullptr;
  errno = 0;
  auto parsed = strtoull(value.c_str(), &end, 10);
  if (errno != 0 || end != value.c_str() + value.size()) {
    return false;
  }
  result = parsed;
  return true;
}

} // namespace

PeerCreditState* PeerCreditStore::getOrCreate(const std::string& userHash)
{
  if (userHash.size() != HASH_LENGTH) {
    return nullptr;
  }
  auto i = std::find_if(credits_.begin(), credits_.end(),
                        [&](const PeerCreditState& state) {
                          return state.userHash == userHash;
                        });
  if (i != credits_.end()) {
    return &*i;
  }
  PeerCreditState state;
  state.userHash = userHash;
  credits_.push_back(state);
  return &credits_.back();
}

void PeerCreditStore::addUploaded(const std::string& userHash, uint64_t bytes)
{
  if (bytes == 0) {
    return;
  }
  auto credit = getOrCreate(userHash);
  if (credit) {
    credit->uploaded += bytes;
  }
}

void PeerCreditStore::addDownloaded(const std::string& userHash, uint64_t bytes)
{
  if (bytes == 0) {
    return;
  }
  auto credit = getOrCreate(userHash);
  if (credit) {
    credit->downloaded += bytes;
  }
}

double PeerCreditStore::scoreRatio(const std::string& userHash)
{
  auto credit = getOrCreate(userHash);
  if (!credit || credit->downloaded < 1000000) {
    return 1.0;
  }
  double result = 10.0;
  if (credit->uploaded > 0) {
    result = static_cast<double>(credit->downloaded) * 2.0 /
             static_cast<double>(credit->uploaded);
  }
  const auto limit =
      std::sqrt(static_cast<double>(credit->downloaded) / 1048576.0 + 2.0);
  result = std::min(result, limit);
  return std::max(1.0, std::min(10.0, result));
}

void PeerCreditStore::restore(const std::vector<PeerCreditState>& credits)
{
  credits_.clear();
  for (const auto& credit : credits) {
    const auto duplicate =
        std::find_if(credits_.begin(), credits_.end(),
                     [&](const PeerCreditState& state) {
                       return state.userHash == credit.userHash;
                     }) != credits_.end();
    if (credit.userHash.size() != HASH_LENGTH || duplicate) {
      continue;
    }
    credits_.push_back(credit);
  }
}

UploadQueue::UploadQueue(size_t maxSlots)
    : maxSlots_(maxSlots == 0 ? 3 : maxSlots)
{
}

UploadPeer* UploadQueue::findPeer(const Endpoint& endpoint)
{
  auto i = std::find_if(peers_.begin(), peers_.end(),
                        [&](const UploadPeer& peer) {
                          return sameEndpoint(peer.endpoint, endpoint);
                        });
  return i == peers_.end() ? nullptr : &*i;
}

const UploadPeer* UploadQueue::findPeerByUserHash(
    const std::string& userHash) const
{
  if (userHash.size() != HASH_LENGTH) {
    return nullptr;
  }
  auto i = std::find_if(peers_.begin(), peers_.end(),
                        [&](const UploadPeer& peer) {
                          return peer.userHash == userHash;
                        });
  return i == peers_.end() ? nullptr : &*i;
}

bool UploadQueue::canOpenSlot(RequestGroupMan* rgman) const
{
  if (uploadingCount() >= maxSlots_) {
    return false;
  }
  return !rgman || !rgman->doesOverallUploadSpeedExceed();
}

bool UploadQueue::requestUpload(const Endpoint& endpoint,
                                const std::string& userHash,
                                const std::string& fileHash, int64_t now,
                                RequestGroupMan* rgman)
{
  if (endpoint.host.empty() || endpoint.port == 0 ||
      fileHash.size() != HASH_LENGTH) {
    return false;
  }
  auto peer = findPeer(endpoint);
  if (!peer && findPeerByUserHash(userHash)) {
    return false;
  }
  if (!peer) {
    UploadPeer created;
    created.endpoint = endpoint;
    created.userHash = userHash;
    created.fileHash = fileHash;
    created.waitStartTime = now;
    created.lastRequestTime = now;
    created.connected = true;
    peers_.push_back(created);
    peer = &peers_.back();
  }
  else {
    if (!userHash.empty() && peer->userHash.empty()) {
      peer->userHash = userHash;
    }
    peer->fileHash = fileHash;
    peer->lastRequestTime = now;
    peer->connected = true;
  }

  if (peer->uploading) {
    return true;
  }
  if (canOpenSlot(rgman)) {
    peer->uploading = true;
    peer->rank = 0;
    peer->uploadStartTime = now;
    return true;
  }
  if (peer->waitStartTime == 0) {
    peer->waitStartTime = now;
  }
  sortWaiting();
  return false;
}

bool UploadQueue::isUploading(const Endpoint& endpoint) const
{
  auto i = std::find_if(peers_.begin(), peers_.end(),
                        [&](const UploadPeer& peer) {
                          return sameEndpoint(peer.endpoint, endpoint);
                        });
  return i != peers_.end() && i->uploading;
}

uint16_t UploadQueue::queueRank(const Endpoint& endpoint) const
{
  auto i = std::find_if(peers_.begin(), peers_.end(),
                        [&](const UploadPeer& peer) {
                          return sameEndpoint(peer.endpoint, endpoint);
                        });
  return i == peers_.end() ? 0 : i->rank;
}

bool UploadQueue::remove(const Endpoint& endpoint)
{
  auto i = std::find_if(peers_.begin(), peers_.end(),
                        [&](const UploadPeer& peer) {
                          return sameEndpoint(peer.endpoint, endpoint);
                        });
  if (i == peers_.end()) {
    return false;
  }
  peers_.erase(i);
  recomputeRanks();
  return true;
}

void UploadQueue::disconnect(const Endpoint& endpoint)
{
  auto peer = findPeer(endpoint);
  if (!peer) {
    return;
  }
  peer->connected = false;
  if (!peer->uploading) {
    return;
  }
  peer->uploading = false;
  peer->uploadStartTime = 0;
  sortWaiting();
}

void UploadQueue::noteUploaded(const Endpoint& endpoint, uint64_t bytes)
{
  auto peer = findPeer(endpoint);
  if (!peer || bytes == 0) {
    return;
  }
  peer->sessionUploaded += bytes;
  credits_.addUploaded(peer->userHash, bytes);
}

void UploadQueue::noteDownloaded(const std::string& userHash, uint64_t bytes)
{
  credits_.addDownloaded(userHash, bytes);
}

size_t UploadQueue::maintain(int64_t now, RequestGroupMan* rgman)
{
  constexpr int64_t WAITING_PEER_RETENTION_SECONDS = 3600;
  constexpr int64_t UPLOAD_SLOT_ROTATION_SECONDS = 3600;
  constexpr uint64_t UPLOAD_SLOT_BYTE_LIMIT = 10 * 1024 * 1024;
  size_t changed = 0;
  for (auto& peer : peers_) {
    if (!peer.uploading ||
        ((peer.uploadStartTime == 0 ||
          now - peer.uploadStartTime < UPLOAD_SLOT_ROTATION_SECONDS) &&
         peer.sessionUploaded <= UPLOAD_SLOT_BYTE_LIMIT)) {
      continue;
    }
    peer.uploading = false;
    peer.uploadStartTime = 0;
    peer.waitStartTime = now;
    peer.sessionUploaded = 0;
    ++changed;
  }
  const auto oldSize = peers_.size();
  peers_.erase(
      std::remove_if(peers_.begin(), peers_.end(), [&](const UploadPeer& peer) {
        return !peer.uploading && !peer.connected && peer.lastRequestTime != 0 &&
               now - peer.lastRequestTime >= WAITING_PEER_RETENTION_SECONDS;
      }),
      peers_.end());
  changed += oldSize - peers_.size();
  sortWaiting();
  for (auto& peer : peers_) {
    if (!canOpenSlot(rgman)) {
      break;
    }
    if (peer.uploading || !peer.connected) {
      continue;
    }
    peer.uploading = true;
    peer.rank = 0;
    peer.uploadStartTime = now;
    ++changed;
  }
  sortWaiting();
  return changed;
}

size_t UploadQueue::uploadingCount() const
{
  return static_cast<size_t>(std::count_if(
      peers_.begin(), peers_.end(),
      [](const UploadPeer& peer) { return peer.uploading; }));
}

size_t UploadQueue::waitingCount() const
{
  return static_cast<size_t>(std::count_if(
      peers_.begin(), peers_.end(),
      [](const UploadPeer& peer) { return !peer.uploading; }));
}

void UploadQueue::sortWaiting()
{
  std::stable_sort(peers_.begin(), peers_.end(),
                   [&](const UploadPeer& lhs, const UploadPeer& rhs) {
                     if (lhs.uploading != rhs.uploading) {
                       return lhs.uploading;
                     }
                     if (lhs.uploading) {
                       return lhs.uploadStartTime < rhs.uploadStartTime;
                     }
                     const auto lhsScore = credits_.scoreRatio(lhs.userHash);
                     const auto rhsScore = credits_.scoreRatio(rhs.userHash);
                     if (lhsScore != rhsScore) {
                       return lhsScore > rhsScore;
                     }
                     if (lhs.waitStartTime != rhs.waitStartTime) {
                       return lhs.waitStartTime < rhs.waitStartTime;
                     }
                     if (lhs.endpoint.host != rhs.endpoint.host) {
                       return lhs.endpoint.host < rhs.endpoint.host;
                     }
                     return lhs.endpoint.port < rhs.endpoint.port;
                   });
  recomputeRanks();
}

void UploadQueue::recomputeRanks()
{
  uint16_t rank = 1;
  for (auto& peer : peers_) {
    peer.rank = peer.uploading ? 0 : rank++;
  }
}

std::string createPeerCreditStatePayload(const PeerCreditState& state)
{
  if (state.userHash.size() != HASH_LENGTH) {
    return "";
  }
  std::string payload = "v=1";
  payload += "|hash=" + util::toHex(state.userHash);
  payload += "|uploaded=" + util::uitos(state.uploaded);
  payload += "|downloaded=" + util::uitos(state.downloaded);
  return payload;
}

bool parsePeerCreditStatePayload(PeerCreditState& state,
                                 const std::string& payload)
{
  PeerCreditState parsed;
  std::vector<std::string> fields;
  util::split(payload.begin(), payload.end(), std::back_inserter(fields), '|',
              true);
  for (const auto& field : fields) {
    auto divided = util::divide(field.begin(), field.end(), '=');
    const std::string key(divided.first.first, divided.first.second);
    const std::string value(divided.second.first, divided.second.second);
    if (key == "v" && value != "1") {
      return false;
    }
    if (key == "hash") {
      parsed.userHash = util::fromHex(value.begin(), value.end());
    }
    else if (key == "uploaded") {
      if (!parseUint64(parsed.uploaded, value)) {
        return false;
      }
    }
    else if (key == "downloaded") {
      if (!parseUint64(parsed.downloaded, value)) {
        return false;
      }
    }
  }
  if (parsed.userHash.size() != HASH_LENGTH) {
    return false;
  }
  state = parsed;
  return true;
}

} // namespace ed2k

} // namespace aria2
