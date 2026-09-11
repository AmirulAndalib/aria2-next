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
#include "Ed2kKadCommand.h"
#include "Command.h"
#include "a2netcompat.h"
#include "ed2k_kad.h"
#include "ed2k_link.h"
#include "ed2k_peer.h"
#include "ed2k_search.h"
#include <cstdint>
#include <ctime>
#include <memory>
#include <string>
#include "ed2k_constants.h"
#include "ed2k/KadCommandSupport.h"
#include "DlAbortEx.h"
#include "DownloadContext.h"
#include "DownloadEngine.h"
#include "Ed2kAttribute.h"
#include "Ed2kSession.h"
#include "Ed2kUploadQueue.h"
#include "Log.h"
#include "RequestGroup.h"
#include "RequestGroupMan.h"
#include "SocketCore.h"
#include "fmt.h"
#include "wallclock.h"
#include <algorithm>
#include <chrono>

namespace aria2 {

using namespace kad_command;

void Ed2kKadCommand::handlePacket(const ed2k::Endpoint& endpoint,
                                  const ed2k::KadObfuscatedDatagram* context,
                                  uint8_t opcode, const std::string& payload)
{
  auto attrs = getEd2kAttrs(requestGroup_->getDownloadContext());
  if (!attrs->kadRoutingTable) {
    return;
  }
  switch (opcode) {
  case ed2k::KAD_BOOTSTRAP_REQ:
    handleBootstrapRequest(*attrs, endpoint, context, payload);
    return;
  case ed2k::KAD_BOOTSTRAP_RES:
    handleBootstrapResponse(*attrs, endpoint, context, payload);
    return;
  case ed2k::KAD_HELLO_RES_ACK:
    handleHelloAck(*attrs, endpoint, context, payload);
    return;
  case ed2k::KAD_HELLO_REQ:
  case ed2k::KAD_HELLO_RES:
    handleHello(*attrs, endpoint, context, opcode, payload);
    return;
  case ed2k::KAD_REQ:
    handleNodeRequest(*attrs, endpoint, context, payload);
    return;
  case ed2k::KAD_RES:
    handleNodeResponse(*attrs, endpoint, payload);
    return;
  case ed2k::KAD_SEARCH_RES:
    handleSearchResponse(*attrs, endpoint, payload);
    return;
  case ed2k::KAD_FIREWALLED_RES:
    handleFirewallResponse(*attrs, endpoint, payload);
    return;
  case ed2k::KAD_PUBLISH_SOURCE_REQ:
    handlePublishRequest(*attrs, endpoint, context, payload);
    return;
  case ed2k::KAD_SEARCH_SOURCES_REQ:
    handleSourceRequest(*attrs, endpoint, context, payload);
    return;
  case ed2k::KAD_FIREWALLED_REQ:
    handleFirewallRequest(endpoint, context, payload);
    return;
  case ed2k::KAD_PING:
    handlePing(endpoint, context);
    return;
  default:
    return;
  }
}

void Ed2kKadCommand::handleEd2kUdpPacket(const ed2k::Endpoint& endpoint,
                                         uint8_t opcode,
                                         const std::string& payload)
{
  switch (opcode) {
  case ed2k::OP_REASKACK:
    handleReaskAck(endpoint, payload);
    return;
  case ed2k::OP_QUEUEFULL:
    handleQueueFull(endpoint);
    return;
  case ed2k::OP_FILENOTFOUND:
    handleFileNotFound(endpoint);
    return;
  case ed2k::OP_REASKFILEPING:
    handleReaskPing(endpoint, payload);
    return;
  case ed2k::OP_DIRECTCALLBACKREQ:
    handleDirectCallback(endpoint, payload);
    return;
  case ed2k::OP_GLOBFOUNDSOURCES:
    handleServerSources(endpoint, payload);
    return;
  case ed2k::OP_INVALID_LOWID:
    handleInvalidLowId(payload);
    return;
  case ed2k::OP_GLOBSERVSTATRES:
    handleServerStatus(endpoint, payload);
    return;
  default:
    return;
  }
}

Ed2kKadCommand::Ed2kKadCommand(cuid_t cuid, RequestGroup* requestGroup,
                               DownloadEngine* e)
    : Command(cuid),
      requestGroup_(requestGroup),
      e_(e),
      socket_(std::make_shared<SocketCore>(SOCK_DGRAM)),
      initialized_(false),
      lastServerStatusPoll_(0),
      bootstrapCursor_(0)
{
  setStatusRealtime();
  e_->getRequestGroupMan()->getEd2kSession()->registerDownload(requestGroup_);
  e_->setEd2kUdpActive(true);
}

Ed2kKadCommand::~Ed2kKadCommand()
{
  if (initialized_) {
    e_->deleteSocketForReadCheck(socket_, this);
  }
  e_->setEd2kUdpActive(false);
}

uint16_t Ed2kKadCommand::getLocalUdpPort() const
{
  return socket_->getAddrInfo().port;
}

bool Ed2kKadCommand::waitLocalUdpReadable(time_t timeout) const
{
  return socket_->isReadable(timeout);
}

int64_t Ed2kKadCommand::nowSeconds() const
{
  return std::chrono::duration_cast<std::chrono::seconds>(
             global::wallclock().getTime().time_since_epoch())
      .count();
}

RequestGroup*
Ed2kKadCommand::findKadTargetGroup(const std::string& targetId) const
{
  const auto session = e_->getRequestGroupMan()->getEd2kSession();
  for (auto group : session->downloads()) {
    auto attrs = getEd2kAttrs(group->getDownloadContext());
    if (!attrs || group->isHaltRequested()) {
      continue;
    }
    if (!attrs->link.hash.empty() &&
        ed2k::ed2kHashToKadId(attrs->link.hash) == targetId) {
      return group;
    }
    if (attrs->searchActive && !attrs->searchQuery.keyword.empty() &&
        ed2k::createKadKeywordTarget(attrs->searchQuery.keyword) == targetId) {
      return group;
    }
  }
  return nullptr;
}

RequestGroup* Ed2kKadCommand::findPeerGroup(const ed2k::Endpoint& endpoint,
                                            const std::string& userHash) const
{
  const auto session = e_->getRequestGroupMan()->getEd2kSession();
  for (auto group : session->downloads()) {
    auto attrs = getEd2kAttrs(group->getDownloadContext());
    if (!attrs || group->isHaltRequested()) {
      continue;
    }
    auto state = std::find_if(
        attrs->peerStates.begin(), attrs->peerStates.end(),
        [&](const ed2k::PeerState& peer) {
          const auto endpointMatches = peer.endpoint.host == endpoint.host &&
                                       (peer.endpoint.port == endpoint.port ||
                                        peer.udpPort == endpoint.port);
          const auto hashMatches =
              !userHash.empty() && peer.endpoint.userHash == userHash;
          return endpointMatches || hashMatches;
        });
    if (state != attrs->peerStates.end()) {
      return group;
    }
  }
  return nullptr;
}

void Ed2kKadCommand::init()
{
  socket_->bind(nullptr, localEd2kUdpPort(e_), AF_INET);
  socket_->setNonBlockingMode();
  e_->addSocketForReadCheck(socket_, this);
  initialized_ = true;
  A2_LOG_DEBUG(fmt("IPv4 ED2K Kad: listening on UDP port %u",
                   socket_->getAddrInfo().port));

  auto attrs = getEd2kAttrs(requestGroup_->getDownloadContext());
  if (attrs->kadUdpVerifyKey == 0) {
    attrs->kadUdpVerifyKey = createEd2kKadUdpVerifyKey();
  }
  if (!attrs->kadRoutingTable) {
    attrs->kadRoutingTable = std::make_shared<ed2k::KadRoutingTable>(
        ed2k::ed2kHashToKadId(attrs->clientHash));
  }
  queueBootstrap();
}

bool Ed2kKadCommand::execute()
{
  auto session = e_->getRequestGroupMan()->getEd2kSession();
  session->detachStoppedDownloads();
  if (e_->isHaltRequested()) {
    session->detachAllDownloads();
    return true;
  }
  if (session->empty()) {
    return true;
  }
  requestGroup_ = session->networkDownload();
  try {
    if (!initialized_) {
      init();
    }
    receivePackets();
    const auto downloads = session->downloads();
    for (auto group : downloads) {
      if (!group || group->isHaltRequested()) {
        continue;
      }
      requestGroup_ = group;
      auto attrs = getEd2kAttrs(group->getDownloadContext());
      expireTransactions(*attrs);
      schedulePendingEd2kServers(group, e_);
      queueDuePeerReasks(nowSeconds());
      queueDueKadCallbacks(nowSeconds());
      if (!group->downloadFinished()) {
        if (attrs->searchActive) {
          queueKeywordSearch();
        }
        else {
          queueSourceSearch();
        }
      }
    }

    requestGroup_ = session->networkDownload();
    if (auto uploadQueue = e_->getRequestGroupMan()->getEd2kUploadQueue()) {
      uploadQueue->maintain(nowSeconds(), e_->getRequestGroupMan().get());
    }
    queueServerStatusPoll();
    queueServerSourcePoll();
    queueBootstrap();
    queueRefresh();
    queueFirewalledCheck();
    queueSourcePublish();
    sendQueuedPackets();
    session->synchronizeNetworkState();
  }
  catch (DlAbortEx& e) {
    A2_LOG_DEBUG_EX("Exception thrown while handling ED2K Kad.", e);
  }
  e_->addRoutineCommand(std::unique_ptr<Command>(this));
  return false;
}

} // namespace aria2
