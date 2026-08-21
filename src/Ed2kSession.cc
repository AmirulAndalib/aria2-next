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
#include <array>
#include <cstdio>
#include <limits>

#include "BufferedFile.h"
#include "DownloadContext.h"
#include "Ed2kAttribute.h"
#include "Ed2kUploadQueue.h"
#include "File.h"
#include "Log.h"
#include "RequestGroup.h"
#include "ed2k_helper.h"
#include "fmt.h"
#include "util.h"

namespace aria2 {

namespace ed2k {

namespace {

constexpr std::array<char, 8> STATE_MAGIC = {
    'A', '2', 'E', 'D', '2', 'K', 2, 0};
constexpr size_t MAX_PERSISTED_SOURCES_PER_FILE = 10;

void appendBlob(std::string& out, const std::string& value)
{
  out += packUInt32(static_cast<uint32_t>(value.size()));
  out += value;
}

bool readBlob(std::string& value, const std::string& data, size_t& offset)
{
  if (data.size() - offset < 4) {
    return false;
  }
  const auto length = readUInt32(data.data() + offset);
  offset += 4;
  if (length > data.size() - offset) {
    return false;
  }
  value.assign(data.data() + offset, length);
  offset += length;
  return true;
}

void appendSource(std::string& out, const Endpoint& source)
{
  appendBlob(out, source.host);
  out += packUInt16(source.port);
  out += packUInt16(source.cryptOptions);
  appendBlob(out, source.userHash);
}

bool readSource(Endpoint& source, const std::string& data, size_t& offset)
{
  if (!readBlob(source.host, data, offset) || data.size() - offset < 4) {
    return false;
  }
  source.port = readUInt16(data.data() + offset);
  offset += 2;
  source.cryptOptions = readUInt16(data.data() + offset);
  offset += 2;
  return readBlob(source.userHash, data, offset) && !source.host.empty() &&
         source.port != 0 &&
         (source.userHash.empty() || source.userHash.size() == HASH_LENGTH);
}

} // namespace

Ed2kSession::Ed2kSession(UploadQueue* uploadQueue, std::string stateFile)
    : uploadQueue_(uploadQueue),
      stateFile_(std::move(stateFile))
{
  load();
}

Ed2kSession::~Ed2kSession()
{
  if (!downloads_.empty()) {
    for (auto group : downloads_) {
      captureNetworkState(group);
    }
    save();
  }
  for (auto group : downloads_) {
    group->decreaseNumCommand();
  }
}

void Ed2kSession::registerDownload(RequestGroup* group)
{
  if (!group ||
      std::find(downloads_.begin(), downloads_.end(), group) !=
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

void Ed2kSession::unregisterDownload(RequestGroup* group)
{
  auto i = std::find(downloads_.begin(), downloads_.end(), group);
  if (i == downloads_.end()) {
    return;
  }
  captureNetworkState(*i);
  (*i)->decreaseNumCommand();
  downloads_.erase(i);
  if (downloads_.empty()) {
    save();
  }
}

void Ed2kSession::unregisterStoppedDownloads()
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
    save();
  }
}

void Ed2kSession::unregisterAllDownloads()
{
  if (downloads_.empty()) {
    return;
  }
  for (auto group : downloads_) {
    captureNetworkState(group);
    group->decreaseNumCommand();
  }
  downloads_.clear();
  save();
}

RequestGroup* Ed2kSession::networkDownload() const
{
  return downloads_.empty() ? nullptr : downloads_.front();
}

void Ed2kSession::synchronizeNetworkState()
{
  auto primary = networkDownload();
  auto primaryAttrs = primary ? getEd2kAttrs(primary->getDownloadContext())
                              : nullptr;
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
  auto existing = std::find_if(
      fileSources_.begin(), fileSources_.end(),
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
    auto existing = std::find_if(
        attrs->serverStates.begin(), attrs->serverStates.end(),
        [&](const ServerState& item) {
          return item.endpoint.host == state.endpoint.host &&
                 item.endpoint.port == state.endpoint.port;
        });
    if (existing == attrs->serverStates.end()) {
      attrs->serverStates.push_back(state);
    }
    auto endpoint = std::find_if(
        attrs->servers.begin(), attrs->servers.end(),
        [&](const Endpoint& item) {
          return item.host == state.endpoint.host &&
                 item.port == state.endpoint.port;
        });
    if (endpoint == attrs->servers.end()) {
      attrs->servers.push_back(state.endpoint);
    }
  }
  auto persisted = std::find_if(
      fileSources_.begin(), fileSources_.end(),
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

void Ed2kSession::load()
{
  if (stateFile_.empty()) {
    return;
  }
  File file(stateFile_);
  if (!file.isFile() || file.size() <= 0 ||
      file.size() > 16 * 1024 * 1024) {
    return;
  }
  BufferedFile fp(stateFile_.c_str(), BufferedFile::READ);
  if (!fp) {
    return;
  }
  std::string data(static_cast<size_t>(file.size()), '\0');
  if (fp.read(&data[0], data.size()) != data.size()) {
    return;
  }
  size_t offset = 0;
  if (data.size() < STATE_MAGIC.size() ||
      !std::equal(STATE_MAGIC.begin(), STATE_MAGIC.end(), data.begin())) {
    return;
  }
  offset += STATE_MAGIC.size();

  std::string clientHash;
  std::string kadState;
  if (!readBlob(clientHash, data, offset) ||
      clientHash.size() != HASH_LENGTH || !readBlob(kadState, data, offset) ||
      data.size() - offset < 4) {
    return;
  }
  std::vector<ServerState> servers;
  const auto serverCount = readUInt32(data.data() + offset);
  offset += 4;
  for (uint32_t i = 0; i < serverCount; ++i) {
    std::string payload;
    ServerState state;
    if (!readBlob(payload, data, offset) ||
        !parseServerStatePayload(state, payload)) {
      return;
    }
    state.connecting = false;
    state.connected = false;
    state.handshakeCompleted = false;
    servers.push_back(std::move(state));
  }
  if (data.size() - offset < 4) {
    return;
  }
  std::vector<PersistedFileSources> fileSources;
  const auto fileCount = readUInt32(data.data() + offset);
  offset += 4;
  for (uint32_t i = 0; i < fileCount; ++i) {
    PersistedFileSources fileState;
    if (!readBlob(fileState.fileHash, data, offset) ||
        fileState.fileHash.size() != HASH_LENGTH ||
        data.size() - offset < 12) {
      return;
    }
    const auto fileSize = readUInt64(data.data() + offset);
    offset += 8;
    if (fileSize > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
      return;
    }
    fileState.fileSize = static_cast<int64_t>(fileSize);
    const auto sourceCount = readUInt32(data.data() + offset);
    offset += 4;
    if (sourceCount > MAX_PERSISTED_SOURCES_PER_FILE) {
      return;
    }
    for (uint32_t j = 0; j < sourceCount; ++j) {
      Endpoint source;
      if (!readSource(source, data, offset)) {
        return;
      }
      fileState.sources.push_back(std::move(source));
    }
    fileSources.push_back(std::move(fileState));
  }
  if (data.size() - offset < 4) {
    return;
  }
  std::vector<PeerCreditState> credits;
  const auto creditCount = readUInt32(data.data() + offset);
  offset += 4;
  for (uint32_t i = 0; i < creditCount; ++i) {
    std::string payload;
    PeerCreditState state;
    if (!readBlob(payload, data, offset) ||
        !parsePeerCreditStatePayload(state, payload)) {
      return;
    }
    credits.push_back(std::move(state));
  }
  if (offset != data.size()) {
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
  lastSavedData_ = data;
  A2_LOG_DEBUG(fmt("Loaded ED2K runtime state from %s.", stateFile_.c_str()));
}

void Ed2kSession::save() const
{
  if (stateFile_.empty() || clientHash_.size() != HASH_LENGTH) {
    return;
  }
  std::string data(STATE_MAGIC.begin(), STATE_MAGIC.end());
  appendBlob(data, clientHash_);
  appendBlob(data, hasKadSnapshot_ ? createKadRoutingStatePayload(kadSnapshot_)
                                  : std::string());
  data += packUInt32(static_cast<uint32_t>(serverStates_.size()));
  for (const auto& state : serverStates_) {
    appendBlob(data, createServerStatePayload(state));
  }
  data += packUInt32(static_cast<uint32_t>(fileSources_.size()));
  for (const auto& fileState : fileSources_) {
    appendBlob(data, fileState.fileHash);
    data += packUInt64(static_cast<uint64_t>(fileState.fileSize));
    data += packUInt32(static_cast<uint32_t>(fileState.sources.size()));
    for (const auto& source : fileState.sources) {
      appendSource(data, source);
    }
  }
  const auto& credits = uploadQueue_->credits().list();
  data += packUInt32(static_cast<uint32_t>(credits.size()));
  for (const auto& credit : credits) {
    appendBlob(data, createPeerCreditStatePayload(credit));
  }

  if (data == lastSavedData_) {
    return;
  }

  File directory(File(stateFile_).getDirname());
  if (!directory.isDir() && !directory.mkdirs()) {
    return;
  }
  const auto temporary = stateFile_ + "__temp";
  BufferedFile fp(temporary.c_str(), BufferedFile::WRITE);
  if (!fp || fp.write(data.data(), data.size()) != data.size() ||
      fp.close() == EOF || !File(temporary).renameTo(stateFile_)) {
    A2_LOG_ERROR(fmt("Failed to save ED2K runtime state to %s.",
                     stateFile_.c_str()));
    return;
  }
  lastSavedData_ = std::move(data);
  A2_LOG_DEBUG(fmt("Saved ED2K runtime state to %s.", stateFile_.c_str()));
}

} // namespace ed2k

} // namespace aria2
