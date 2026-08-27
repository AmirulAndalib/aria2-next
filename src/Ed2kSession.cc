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
#include "Ed2kSession.h"

#include <algorithm>
#include <limits>

#include "DownloadContext.h"
#include "Ed2kAttribute.h"
#include "Ed2kUploadQueue.h"
#include "File.h"
#include "GroupId.h"
#include "Log.h"
#include "Option.h"
#include "Piece.h"
#include "PieceStorage.h"
#include "RecoverableException.h"
#include "RequestGroup.h"
#include "download_helper.h"
#include "fmt.h"
#include "prefs.h"

namespace aria2 {

namespace ed2k {

namespace {

constexpr size_t MAX_PERSISTED_SOURCES_PER_FILE = 10;

} // namespace

Ed2kSession::Ed2kSession(UploadQueue* uploadQueue, std::string databasePath)
    : uploadQueue_(uploadQueue),
      databasePath_(std::move(databasePath)),
      store_(make_unique<Ed2kStore>(databasePath_))
{
  restoreRuntime();
}

Ed2kSession::~Ed2kSession()
{
  if (!downloads_.empty()) {
    for (auto group : downloads_) {
      captureNetworkState(group);
      checkpointDownload(group);
    }
    checkpointRuntime();
  }
  for (auto group : downloads_) {
    group->decreaseNumCommand();
  }
}

void Ed2kSession::registerDownload(RequestGroup* group)
{
  if (!group || std::find(downloads_.begin(), downloads_.end(), group) !=
                    downloads_.end()) {
    return;
  }
  auto attrs = getEd2kAttrs(group->getDownloadContext());
  if (!attrs) {
    return;
  }

  applyPersistedState(group);

  if (clientHash_.empty()) {
    clientHash_ = attrs->clientHash;
  }
  attrs->clientHash = clientHash_;

  if (!routingTable_) {
    routingTable_ = attrs->kadRoutingTable;
  }
  else if (attrs->kadRoutingTable && attrs->kadRoutingTable != routingTable_) {
    const auto snapshot = attrs->kadRoutingTable->snapshot();
    for (const auto& endpoint : snapshot.routerNodes) {
      routingTable_->addRouterNode(endpoint);
    }
    for (const auto& contact : snapshot.routerContacts) {
      routingTable_->addRouterNode(contact);
    }
    for (const auto& bucket : snapshot.buckets) {
      for (const auto& node : bucket.live) {
        if (node.confirmed) {
          routingTable_->nodeSeen(node.contact, node.lastSeen);
        }
        else {
          routingTable_->heardAbout(node.contact, node.lastSeen);
        }
      }
      for (const auto& node : bucket.replacements) {
        routingTable_->heardAbout(node.contact, node.lastSeen);
      }
    }
  }
  attrs->kadRoutingTable = routingTable_;

  if (kadUdpVerifyKey_ == 0) {
    kadUdpVerifyKey_ = attrs->kadUdpVerifyKey;
  }
  attrs->kadUdpVerifyKey = kadUdpVerifyKey_;

  downloads_.push_back(group);
  group->increaseNumCommand();
  synchronizeNetworkState();
}

void Ed2kSession::detachStoppedDownloads()
{
  bool removed = false;
  for (auto i = downloads_.begin(); i != downloads_.end();) {
    if (!(*i)->isHaltRequested()) {
      ++i;
      continue;
    }
    captureNetworkState(*i);
    (*i)->decreaseNumCommand();
    i = downloads_.erase(i);
    removed = true;
  }
  if (removed && downloads_.empty()) {
    checkpointRuntime();
  }
}

void Ed2kSession::detachAllDownloads()
{
  if (downloads_.empty()) {
    return;
  }
  for (auto group : downloads_) {
    captureNetworkState(group);
    group->decreaseNumCommand();
  }
  downloads_.clear();
  checkpointRuntime();
}

RequestGroup* Ed2kSession::networkDownload() const
{
  return downloads_.empty() ? nullptr : downloads_.front();
}

void Ed2kSession::synchronizeNetworkState()
{
  auto primary = networkDownload();
  auto primaryAttrs =
      primary ? getEd2kAttrs(primary->getDownloadContext()) : nullptr;
  if (!primaryAttrs) {
    return;
  }
  routingTable_ = primaryAttrs->kadRoutingTable;
  clientHash_ = primaryAttrs->clientHash;
  kadUdpVerifyKey_ = primaryAttrs->kadUdpVerifyKey;

  for (auto group : downloads_) {
    auto attrs = getEd2kAttrs(group->getDownloadContext());
    if (!attrs || attrs == primaryAttrs) {
      continue;
    }
    attrs->clientHash = clientHash_;
    attrs->kadRoutingTable = routingTable_;
    attrs->kadUdpVerifyKey = kadUdpVerifyKey_;
    attrs->kadObservedAddresses = primaryAttrs->kadObservedAddresses;
    attrs->kadFirewalled = primaryAttrs->kadFirewalled;
    attrs->lastKadFirewalledCheck = primaryAttrs->lastKadFirewalledCheck;
    attrs->lastKadSourcePublish = primaryAttrs->lastKadSourcePublish;
  }
  captureNetworkState(primary);
}

void Ed2kSession::captureNetworkState(RequestGroup* group)
{
  auto attrs = group ? getEd2kAttrs(group->getDownloadContext()) : nullptr;
  if (!attrs) {
    return;
  }
  clientHash_ = attrs->clientHash;
  kadUdpVerifyKey_ = attrs->kadUdpVerifyKey;
  if (attrs->kadRoutingTable) {
    kadSnapshot_ = createEd2kKadSnapshot(attrs);
    hasKadSnapshot_ = true;
  }
  serverStates_ = attrs->serverStates;

  if (attrs->link.hash.size() != HASH_LENGTH || attrs->link.size <= 0 ||
      group->downloadFinished()) {
    return;
  }
  PersistedFileSources persisted;
  persisted.fileHash = attrs->link.hash;
  persisted.fileSize = attrs->link.size;
  std::vector<const PeerState*> candidates;
  for (const auto& state : attrs->peerStates) {
    if (state.lowId || state.dead || state.noFile || state.cancelled ||
        state.endpoint.host.empty() || state.endpoint.port == 0) {
      continue;
    }
    candidates.push_back(&state);
  }
  std::stable_sort(candidates.begin(), candidates.end(),
                   [](const PeerState* lhs, const PeerState* rhs) {
                     if (lhs->accepted != rhs->accepted) {
                       return lhs->accepted;
                     }
                     if (lhs->queued != rhs->queued) {
                       return lhs->queued;
                     }
                     return lhs->failCount < rhs->failCount;
                   });
  for (const auto state : candidates) {
    persisted.sources.push_back(state->endpoint);
    if (persisted.sources.size() == MAX_PERSISTED_SOURCES_PER_FILE) {
      break;
    }
  }
  auto existing = std::find_if(fileSources_.begin(), fileSources_.end(),
                               [&](const PersistedFileSources& item) {
                                 return item.fileHash == persisted.fileHash &&
                                        item.fileSize == persisted.fileSize;
                               });
  if (existing == fileSources_.end()) {
    fileSources_.push_back(std::move(persisted));
  }
  else {
    *existing = std::move(persisted);
  }
}

void Ed2kSession::applyPersistedState(RequestGroup* group)
{
  auto attrs = group ? getEd2kAttrs(group->getDownloadContext()) : nullptr;
  if (!attrs) {
    return;
  }
  if (clientHash_.size() == HASH_LENGTH) {
    attrs->clientHash = clientHash_;
  }
  if (hasKadSnapshot_) {
    if (!attrs->kadRoutingTable) {
      attrs->kadRoutingTable =
          std::make_shared<KadRoutingTable>(kadSnapshot_.selfId);
    }
    attrs->kadRoutingTable->restore(kadSnapshot_);
    restoreEd2kKadOperationalState(attrs, kadSnapshot_);
  }
  for (auto state : serverStates_) {
    state.connecting = false;
    state.connected = false;
    state.handshakeCompleted = false;
    auto existing =
        std::find_if(attrs->serverStates.begin(), attrs->serverStates.end(),
                     [&](const ServerState& item) {
                       return item.endpoint.host == state.endpoint.host &&
                              item.endpoint.port == state.endpoint.port;
                     });
    if (existing == attrs->serverStates.end()) {
      attrs->serverStates.push_back(state);
    }
    auto endpoint = std::find_if(attrs->servers.begin(), attrs->servers.end(),
                                 [&](const Endpoint& item) {
                                   return item.host == state.endpoint.host &&
                                          item.port == state.endpoint.port;
                                 });
    if (endpoint == attrs->servers.end()) {
      attrs->servers.push_back(state.endpoint);
    }
  }
  auto persisted = std::find_if(fileSources_.begin(), fileSources_.end(),
                                [&](const PersistedFileSources& item) {
                                  return item.fileHash == attrs->link.hash &&
                                         item.fileSize == attrs->link.size;
                                });
  if (persisted != fileSources_.end()) {
    for (const auto& source : persisted->sources) {
      addEd2kPeer(attrs, source, PEER_SOURCE_PERSISTED);
    }
  }
}

DownloadStateLoadResult Ed2kSession::loadDownloadState(RequestGroup* group)
{
  if (!group || !group->getPieceStorage()) {
    return DownloadStateLoadResult::Error;
  }
  if (!store_ || !store_->available()) {
    return databasePath_.empty() ? DownloadStateLoadResult::Missing
                                 : DownloadStateLoadResult::Error;
  }
  PersistedDownloadState state;
  const auto result =
      store_->loadDownload(state, GroupId::toHex(group->getGID()));
  if (result != DownloadStateLoadResult::Loaded) {
    return result;
  }
  const auto dctx = group->getDownloadContext();
  const auto attrs = getEd2kAttrs(dctx);
  const auto storage = group->getPieceStorage();
  if (!attrs || state.fileHash != attrs->link.hash ||
      state.fileSize != dctx->getTotalLength() ||
      state.path != dctx->getBasePath() ||
      state.bitfield.size() != storage->getBitfieldLength()) {
    A2_LOG_WARN(fmt("Ignored mismatched ED2K database state for GID %s.",
                    state.gid.c_str()));
    return DownloadStateLoadResult::Error;
  }

  const auto hasPersistedProgress =
      std::any_of(state.bitfield.begin(), state.bitfield.end(),
                  [](char value) { return value != 0; }) ||
      !state.pieces.empty();
  File content(state.path);
  const auto contentSize = content.isFile() ? content.size() : -1;
  if (hasPersistedProgress &&
      (contentSize <= 0 || contentSize > state.fileSize ||
       (state.complete && contentSize != state.fileSize))) {
    A2_LOG_WARN(
        fmt("Ignored ED2K progress without matching payload for GID %s.",
            state.gid.c_str()));
    return DownloadStateLoadResult::Error;
  }

  std::vector<std::shared_ptr<Piece>> pieces;
  pieces.reserve(state.pieces.size());
  for (const auto& persisted : state.pieces) {
    if (persisted.index >= dctx->getNumPieces() || persisted.length <= 0 ||
        persisted.length != storage->getPieceLength(persisted.index)) {
      A2_LOG_WARN(fmt("Ignored invalid ED2K piece state for GID %s.",
                      state.gid.c_str()));
      return DownloadStateLoadResult::Error;
    }
    auto piece = std::make_shared<Piece>(persisted.index, persisted.length);
    if (persisted.bitfield.size() != piece->getBitfieldLength()) {
      A2_LOG_WARN(fmt("Ignored invalid ED2K block state for GID %s.",
                      state.gid.c_str()));
      return DownloadStateLoadResult::Error;
    }
    piece->setBitfield(
        reinterpret_cast<const unsigned char*>(persisted.bitfield.data()),
        persisted.bitfield.size());
    pieces.push_back(std::move(piece));
  }
  storage->setBitfield(
      reinterpret_cast<const unsigned char*>(state.bitfield.data()),
      state.bitfield.size());
  if (state.complete != storage->downloadFinished()) {
    const std::string emptyBitfield(state.bitfield.size(), '\0');
    storage->setBitfield(
        reinterpret_cast<const unsigned char*>(emptyBitfield.data()),
        emptyBitfield.size());
    A2_LOG_WARN(fmt("Ignored inconsistent ED2K completion state for GID %s.",
                    state.gid.c_str()));
    return DownloadStateLoadResult::Error;
  }
  storage->addInFlightPiece(pieces);
  attrs->sharingTime.restore(state.sharingTime);
  A2_LOG_TRACE(fmt("Loaded ED2K download state for GID %s from %s.",
                   state.gid.c_str(), databasePath_.c_str()));
  return DownloadStateLoadResult::Loaded;
}

bool Ed2kSession::checkpointDownload(RequestGroup* group)
{
  if (!store_ || !store_->available() || !group || !group->getPieceStorage()) {
    return false;
  }
  const auto dctx = group->getDownloadContext();
  const auto attrs = getEd2kAttrs(dctx);
  const auto storage = group->getPieceStorage();
  if (!attrs || attrs->link.hash.size() != HASH_LENGTH ||
      dctx->getTotalLength() <= 0) {
    return false;
  }

  PersistedDownloadState state;
  state.gid = GroupId::toHex(group->getGID());
  state.fileHash = attrs->link.hash;
  state.fileSize = dctx->getTotalLength();
  state.link = toFileLink(attrs->link);
  state.path = dctx->getBasePath();
  File content(state.path);
  if (content.isFile()) {
    state.modifiedTime = content.getModifiedTime().getTimeFromEpoch();
  }
  state.paused = group->isPauseRequested();
  state.complete = group->downloadFinished();
  state.sharingTime = group->getEd2kSharingTime();
  state.bitfield.assign(reinterpret_cast<const char*>(storage->getBitfield()),
                        storage->getBitfieldLength());

  std::vector<std::shared_ptr<Piece>> pieces;
  storage->getInFlightPieces(pieces);
  state.pieces.reserve(pieces.size());
  for (const auto& piece : pieces) {
    if (!piece || piece->getCompletedLength() == 0) {
      continue;
    }
    PersistedPieceState persisted;
    persisted.index = piece->getIndex();
    persisted.length = piece->getLength();
    persisted.bitfield.assign(
        reinterpret_cast<const char*>(piece->getBitfield()),
        piece->getBitfieldLength());
    state.pieces.push_back(std::move(persisted));
  }
  return store_->saveDownload(state);
}

bool Ed2kSession::discardDownload(RequestGroup* group)
{
  return store_ && store_->available() && group &&
         store_->removeDownload(GroupId::toHex(group->getGID()));
}

size_t Ed2kSession::restoreDownloads(
    const Option* option,
    std::vector<std::shared_ptr<RequestGroup>>& requestGroups)
{
  if (!store_ || !store_->available() || !option) {
    return 0;
  }
  std::vector<PersistedDownloadState> states;
  if (!store_->loadDownloads(states)) {
    A2_LOG_ERROR(fmt("Failed to enumerate ED2K downloads from %s.",
                     databasePath_.c_str()));
    return 0;
  }

  size_t restored = 0;
  for (const auto& state : states) {
    const auto duplicate = std::any_of(
        requestGroups.begin(), requestGroups.end(), [&](const auto& group) {
          if (!group) {
            return false;
          }
          if (GroupId::toHex(group->getGID()) == state.gid) {
            return true;
          }
          const auto dctx = group->getDownloadContext();
          return dctx && dctx->getBasePath() == state.path;
        });
    if (duplicate) {
      continue;
    }
    try {
      auto taskOption = std::make_shared<Option>(*option);
      File content(state.path);
      taskOption->put(PREF_DIR, content.getDirname());
      taskOption->put(PREF_OUT, content.getBasename());
      taskOption->put(PREF_GID, state.gid);
      taskOption->put(PREF_PAUSE, state.paused ? A2_V_TRUE : A2_V_FALSE);
      auto group = createEd2kFileRequestGroup(state.link, taskOption);
      group->initPieceStorage();
      if (loadDownloadState(group.get()) != DownloadStateLoadResult::Loaded) {
        A2_LOG_WARN(
            fmt("Restored ED2K task without persisted progress for GID %s.",
                state.gid.c_str()));
      }
      requestGroups.push_back(std::move(group));
      ++restored;
    }
    catch (const RecoverableException& ex) {
      A2_LOG_ERROR_EX("Failed to restore ED2K download", ex);
    }
  }
  if (restored != 0) {
    A2_LOG_INFO(fmt("Restored %lu ED2K download(s) from %s.",
                    static_cast<unsigned long>(restored),
                    databasePath_.c_str()));
  }
  return restored;
}

RequestGroup* Ed2kSession::findAlternativeDownload(RequestGroup* current,
                                                   const Endpoint& peer) const
{
  RequestGroup* selected = nullptr;
  size_t selectedSourceCount = std::numeric_limits<size_t>::max();
  for (auto group : downloads_) {
    if (!group || group == current || group->isHaltRequested() ||
        group->isPauseRequested() || group->downloadFinished() ||
        !group->getPieceStorage()) {
      continue;
    }
    auto attrs = getEd2kAttrs(group->getDownloadContext());
    auto state = attrs ? getEd2kPeerState(attrs, peer) : nullptr;
    if (!state || state->noFile || state->cancelled || state->dead) {
      continue;
    }
    bool needed = state->partStatus.empty();
    for (size_t i = 0; !needed && i < state->partStatus.size() &&
                       i < group->getDownloadContext()->getNumPieces();
         ++i) {
      needed = state->partStatus[i] && !group->getPieceStorage()->hasPiece(i);
    }
    if (!needed) {
      continue;
    }
    if (!selected || attrs->peerStates.size() < selectedSourceCount) {
      selected = group;
      selectedSourceCount = attrs->peerStates.size();
    }
  }
  return selected;
}

void Ed2kSession::restoreRuntime()
{
  if (!store_->open()) {
    return;
  }
  std::string clientHash;
  std::string kadState;
  std::vector<ServerState> servers;
  std::vector<PersistedFileSources> fileSources;
  std::vector<PeerCreditState> credits;
  if (!store_->loadIdentity(clientHash, kadState) ||
      !store_->loadServers(servers) || !store_->loadFileSources(fileSources) ||
      !store_->loadCredits(credits)) {
    A2_LOG_ERROR(fmt("Failed to load ED2K database state from %s.",
                     databasePath_.c_str()));
    return;
  }

  KadRoutingSnapshot snapshot;
  if (!kadState.empty() && !parseKadRoutingStatePayload(snapshot, kadState)) {
    return;
  }
  clientHash_ = std::move(clientHash);
  serverStates_ = std::move(servers);
  fileSources_ = std::move(fileSources);
  if (!kadState.empty()) {
    kadSnapshot_ = std::move(snapshot);
    hasKadSnapshot_ = true;
    kadUdpVerifyKey_ = kadSnapshot_.udpVerifyKey;
  }
  if (uploadQueue_) {
    uploadQueue_->credits().restore(credits);
  }
  A2_LOG_TRACE(
      fmt("Loaded ED2K runtime state from %s.", databasePath_.c_str()));
}

void Ed2kSession::checkpointRuntime()
{
  if (!store_ || !store_->available() || clientHash_.size() != HASH_LENGTH) {
    return;
  }
  const auto kadState = hasKadSnapshot_
                            ? createKadRoutingStatePayload(kadSnapshot_)
                            : std::string();
  const auto credits = uploadQueue_ ? uploadQueue_->credits().list()
                                    : std::vector<PeerCreditState>();
  if (!store_->saveRuntime(clientHash_, kadState, serverStates_, fileSources_,
                           credits)) {
    A2_LOG_ERROR(
        fmt("Failed to save ED2K runtime state to %s.", databasePath_.c_str()));
  }
  else {
    A2_LOG_TRACE(fmt("Saved ED2K runtime state to %s.", databasePath_.c_str()));
  }
}

} // namespace ed2k

} // namespace aria2
