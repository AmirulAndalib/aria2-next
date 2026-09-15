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
#include "RequestGroupMan.h"
#include "ServerStat.h"
#include "DownloadEngine.h"
#include "Option.h"
#include "SocketCore.h"
#include "SystemResolver.h"
#include "DlAbortEx.h"
#include "DlRetryEx.h"
#include "Ed2kAttribute.h"
#include "Ed2kCommand.h"
#include "Log.h"
#include "a2functional.h"
#include "ed2k_compression.h"
#include "ed2k_constants.h"
#include "ed2k_packet.h"
#include "ed2k_peer.h"
#include "error_code.h"
#include "fmt.h"
#include "message.h"
#include "prefs.h"
#include <chrono>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <vector>
#include "support/Network.h"

namespace aria2 {

void Ed2kCommand::startResolve()
{
  uint16_t port = endpoint_.port;
  if (shouldObfuscateServerConnection()) {
    const auto state =
        getEd2kServerState(getEd2kAttrs(getDownloadContext()), endpoint_);
    port = state->tcpObfuscationPort;
    serverObfuscation_ = true;
  }
  auto* engine = getDownloadEngine();
  if (resolveRequestId_ == 0) {
    resolvedAddresses_.clear();
    if (util::isNumericHost(endpoint_.host)) {
      resolvedAddresses_.push_back(endpoint_.host);
    }
    else {
      engine->findAllCachedIPAddresses(std::back_inserter(resolvedAddresses_),
                                       endpoint_.host, port);
    }
    if (resolvedAddresses_.empty()) {
      resolveRequestId_ = engine->getSystemResolver()->resolve(
          endpoint_.host, port, !getOption()->getAsBool(PREF_DISABLE_IPV6),
          getTimeout());
      A2_LOG_DEBUG(
          fmt(MSG_RESOLVING_HOSTNAME, getCuid(), endpoint_.host.c_str()));
      state_ = State::RESOLVING;
      engine->setNoWait(true);
      engine->setRefreshInterval(std::chrono::milliseconds(0));
      addCommandSelf();
      return;
    }
  }
  else {
    std::string error;
    const auto status = engine->getSystemResolver()->take(
        resolveRequestId_, resolvedAddresses_, error);
    if (status == SystemResolver::Status::Pending) {
      addCommandSelf();
      return;
    }
    resolveRequestId_ = 0;
    if (status == SystemResolver::Status::Error) {
      engine->getRequestGroupMan()
          ->getOrCreateServerStat(endpoint_.host, mode_ == Mode::SERVER
                                                      ? "ed2k-server"
                                                      : "ed2k-peer")
          ->setError();
      throw DL_ABORT_EX2(fmt(MSG_NAME_RESOLUTION_FAILED, getCuid(),
                             endpoint_.host.c_str(), error.c_str()),
                         error_code::NAME_RESOLVE_ERROR);
    }
  }

  for (const auto& address : resolvedAddresses_) {
    engine->cacheIPAddress(endpoint_.host, address, port);
  }
  const auto& ipaddr = engine->findCachedIPAddress(endpoint_.host, port);
  if (ipaddr.empty()) {
    throw DL_ABORT_EX2(fmt(MSG_NAME_RESOLUTION_FAILED, getCuid(),
                           endpoint_.host.c_str(), "No address returned"),
                       error_code::NAME_RESOLVE_ERROR);
  }
  A2_LOG_DEBUG(
      fmt(MSG_NAME_RESOLUTION_COMPLETE, getCuid(), endpoint_.host.c_str(),
          strjoin(resolvedAddresses_.begin(), resolvedAddresses_.end(), ", ")
              .c_str()));
  connectedHostname_ = endpoint_.host;
  connectedAddr_ = ipaddr;
  connectedPort_ = port;
  startConnect();
}

void Ed2kCommand::startConnect()
{
  A2_LOG_DEBUG(fmt("CUID#%" PRId64 " - Connecting to ED2K %s %s:%u.", getCuid(),
                   mode_ == Mode::SERVER ? "server" : "peer",
                   connectedAddr_.c_str(), connectedPort_));
  createSocket();
  getSocket()->establishConnection(connectedAddr_, connectedPort_);
  setWriteCheckSocket(getSocket());
  state_ = State::CONNECTING;
  addCommandSelf();
}

void Ed2kCommand::queuePacket(uint8_t protocol, uint8_t opcode,
                              const std::string& payload)
{
  outbox_.push_back(ed2k::createPacket(protocol, opcode, payload));
  outboxEncrypted_.push_back(false);
  outboxTransferData_.push_back(opcode == ed2k::OP_SENDINGPART ||
                                opcode == ed2k::OP_SENDINGPART_I64 ||
                                opcode == ed2k::OP_COMPRESSEDPART ||
                                opcode == ed2k::OP_COMPRESSEDPART_I64);
}

bool Ed2kCommand::flushOutbox()
{
  while (!outbox_.empty()) {
    if (uploadRateLimited()) {
      addCommandSelf();
      disableReadCheckSocket();
      disableWriteCheckSocket();
      return false;
    }
    auto& data = outbox_.front();
    auto& encrypted = outboxEncrypted_.front();
    if (!encrypted) {
      encryptPacket(data);
      encrypted = true;
    }
    auto written = getSocket()->writeData(data.data(), data.size());
    if (written == 0) {
      setWriteCheckSocketIf(getSocket(), getSocket()->wantWrite());
      setReadCheckSocketIf(getSocket(), getSocket()->wantRead());
      addCommandSelf();
      return false;
    }
    noteProtocolActivity();
    data.erase(0, static_cast<size_t>(written));
    if (!data.empty()) {
      setWriteCheckSocket(getSocket());
      addCommandSelf();
      return false;
    }
    outbox_.pop_front();
    outboxEncrypted_.pop_front();
    outboxTransferData_.pop_front();
  }
  disableWriteCheckSocket();
  if (closeAfterOutbox_) {
    state_ = State::DONE;
    return true;
  }
  setReadCheckSocket(getSocket());
  state_ = State::READ_HEADER;
  return true;
}

bool Ed2kCommand::readHeader()
{
  while (headerRead_ < headerBuf_.size()) {
    size_t len = headerBuf_.size() - headerRead_;
    getSocket()->readData(headerBuf_.data() + headerRead_, len);
    if (len == 0) {
      if (!getSocket()->wantRead() && !getSocket()->wantWrite()) {
        throw DL_RETRY_EX("ED2K connection closed.");
      }
      setReadCheckSocketIf(getSocket(), getSocket()->wantRead());
      setWriteCheckSocketIf(getSocket(), getSocket()->wantWrite());
      addCommandSelf();
      return false;
    }
    decryptData(headerBuf_.data() + headerRead_, len);
    noteProtocolActivity();
    headerRead_ += len;
  }
  if (!ed2k::readPacketHeader(currentHeader_, headerBuf_.data(),
                              headerBuf_.size())) {
    throw DL_RETRY_EX("Bad ED2K packet header.");
  }
  if (currentHeader_.protocol != ed2k::PROTO_EDONKEY &&
      currentHeader_.protocol != ed2k::PROTO_PACKED &&
      currentHeader_.protocol != ed2k::PROTO_EMULE &&
      currentHeader_.protocol != ed2k::KAD_PROTOCOL) {
    throw DL_RETRY_EX(fmt("Unsupported ED2K packet protocol 0x%02x.",
                          currentHeader_.protocol));
  }
  if (currentHeader_.payloadSize() > 8_m) {
    throw DL_RETRY_EX("ED2K packet is too large.");
  }
  body_.assign(currentHeader_.payloadSize(), '\0');
  bodyRead_ = 0;
  headerRead_ = 0;
  state_ = State::READ_BODY;
  return true;
}

bool Ed2kCommand::readBody()
{
  while (bodyRead_ < body_.size()) {
    size_t len = body_.size() - bodyRead_;
    getSocket()->readData(&body_[bodyRead_], len);
    if (len == 0) {
      if (!getSocket()->wantRead() && !getSocket()->wantWrite()) {
        throw DL_RETRY_EX("ED2K connection closed.");
      }
      setReadCheckSocketIf(getSocket(), getSocket()->wantRead());
      setWriteCheckSocketIf(getSocket(), getSocket()->wantWrite());
      addCommandSelf();
      return false;
    }
    decryptData(&body_[bodyRead_], len);
    noteProtocolActivity();
    bodyRead_ += len;
  }
  if (currentHeader_.protocol == ed2k::PROTO_PACKED) {
    std::string inflated;
    if (!ed2k::inflatePackedPacketPayload(inflated, body_, 250_k)) {
      throw DL_RETRY_EX("Bad packed ED2K packet.");
    }
    A2_LOG_TRACE(fmt("CUID#%" PRId64 " - Unpacked ED2K packet opcode=0x%02x, "
                     "compressed=%zu, inflated=%zu.",
                     getCuid(), currentHeader_.opcode, body_.size(),
                     inflated.size()));
    body_.swap(inflated);
    currentHeader_.protocol =
        mode_ == Mode::SERVER ? ed2k::PROTO_EDONKEY : ed2k::PROTO_EMULE;
    currentHeader_.size = static_cast<uint32_t>(body_.size() + 1);
  }
  handlePacket();
  body_.clear();
  bodyRead_ = 0;
  if (state_ == State::READ_BODY) {
    state_ = State::READ_HEADER;
  }
  return true;
}

void Ed2kCommand::addPeer(const ed2k::Endpoint& peer)
{
  addEd2kPeer(getEd2kAttrs(getDownloadContext()), peer,
              ed2k::PEER_SOURCE_SERVER);
}

void Ed2kCommand::addPeers(const std::vector<ed2k::Endpoint>& peers)
{
  for (const auto& peer : peers) {
    addPeer(peer);
  }
}

void Ed2kCommand::schedulePendingPeers()
{
  schedulePendingEd2kPeers(getRequestGroup(), getDownloadEngine());
}

} // namespace aria2
