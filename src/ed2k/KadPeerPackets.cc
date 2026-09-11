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
#include "ed2k_kad.h"
#include "ed2k_link.h"
#include "ed2k_peer.h"
#include <cstdint>
#include <string>
#include "Ed2kKadCommand.h"
#include "ed2k_constants.h"
#include "ed2k/KadCommandSupport.h"
#include "DownloadContext.h"
#include "DownloadEngine.h"
#include "Ed2kAttribute.h"
#include "Ed2kCommand.h"
#include "Ed2kSession.h"
#include "Ed2kUploadQueue.h"
#include "Log.h"
#include "Option.h"
#include "RequestGroup.h"
#include "RequestGroupMan.h"
#include "a2functional.h"
#include "fmt.h"
#include "prefs.h"
#include <algorithm>

namespace aria2 {

using namespace kad_command;

namespace {
int64_t peerRetryWait(const DownloadEngine* e)
{
  return std::max<int64_t>(1, e->getOption()->getAsInt(PREF_RETRY_WAIT));
}

} // namespace

void Ed2kKadCommand::handleReaskAck(const ed2k::Endpoint& endpoint,
                                    const std::string& payload)
{
  ed2k::UdpReaskAck ack;
  auto group = findPeerGroup(endpoint);
  if (group && ed2k::parseUdpReaskAckPayload(ack, payload)) {
    markEd2kPeerUdpReaskAck(getEd2kAttrs(group->getDownloadContext()), endpoint,
                            ack.rank, ack.bitfield, nowSeconds());
  }
}

void Ed2kKadCommand::handleQueueFull(const ed2k::Endpoint& endpoint)
{
  auto group = findPeerGroup(endpoint);
  if (group) {
    markEd2kPeerQueueFull(getEd2kAttrs(group->getDownloadContext()), endpoint,
                          nowSeconds(), peerRetryWait(e_));
  }
}

void Ed2kKadCommand::handleFileNotFound(const ed2k::Endpoint& endpoint)
{
  auto group = findPeerGroup(endpoint);
  if (group) {
    markEd2kPeerDead(getEd2kAttrs(group->getDownloadContext()), endpoint,
                     nowSeconds(), peerRetryWait(e_));
  }
}

void Ed2kKadCommand::handleReaskPing(const ed2k::Endpoint& endpoint,
                                     const std::string& payload)
{
  const auto session = e_->getRequestGroupMan()->getEd2kSession();
  ed2k::UdpReask reask;
  if (!ed2k::parseUdpReaskFilePingPayload(reask, payload)) {
    return;
  }
  auto group =
      std::find_if(session->downloads().begin(), session->downloads().end(),
                   [&](RequestGroup* candidate) {
                     auto attrs = getEd2kAttrs(candidate->getDownloadContext());
                     return attrs && attrs->link.hash == reask.fileHash;
                   });
  if (group == session->downloads().end()) {
    queueEmuleUdpPacket(endpoint, ed2k::OP_FILENOTFOUND, std::string());
    return;
  }
  uint16_t rank = 0;
  auto rgman = e_->getRequestGroupMan().get();
  auto uploadQueue = rgman ? rgman->getEd2kUploadQueue() : nullptr;
  if (uploadQueue) {
    rank = uploadQueue->queueRank(endpoint);
  }
  if (rank == 0 && (!uploadQueue || !uploadQueue->isUploading(endpoint))) {
    queueEmuleUdpPacket(endpoint, ed2k::OP_QUEUEFULL, std::string());
    return;
  }
  const auto ackPayload =
      reask.partStatus.empty()
          ? ed2k::createUdpReaskAckPayload(rank)
          : ed2k::createUdpReaskAckPayload(localPartStatus(*group), rank);
  queueEmuleUdpPacket(endpoint, ed2k::OP_REASKACK, ackPayload);
}

void Ed2kKadCommand::handleDirectCallback(const ed2k::Endpoint& endpoint,
                                          const std::string& payload)
{
  ed2k::DirectCallbackRequest request;
  if (!ed2k::parseDirectCallbackRequestPayload(request, payload) ||
      request.tcpPort == 0 || request.userHash.empty()) {
    return;
  }
  ed2k::Endpoint peer;
  peer.host = endpoint.host;
  peer.port = request.tcpPort;
  peer.userHash = request.userHash;
  peer.cryptOptions = request.connectOptions;
  auto group = findPeerGroup(endpoint, request.userHash);
  if (!group) {
    return;
  }
  auto attrs = getEd2kAttrs(group->getDownloadContext());
  addEd2kPeer(attrs, peer, ed2k::PEER_SOURCE_INCOMING);
  e_->addCommand(
      make_unique<Ed2kCommand>(e_->newCUID(), group, e_, peer, false));
  A2_LOG_TRACE(fmt("Accepted ED2K direct UDP callback request from %s:%u "
                   "tcp=%u.",
                   endpoint.host.c_str(), endpoint.port, request.tcpPort));
}

} // namespace aria2
