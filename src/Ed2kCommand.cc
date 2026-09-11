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
#include "Ed2kCommand.h"
#include "AbstractCommand.h"
#include "Command.h"
#include "DlAbortEx.h"
#include "DlRetryEx.h"
#include "DownloadEngine.h"
#include "DownloadFailureException.h"
#include "Ed2kAttribute.h"
#include "GroupId.h"
#include "Log.h"
#include "RequestGroup.h"
#include "SocketCore.h"
#include "TimerA2.h"
#include "a2functional.h"
#include "ed2k_constants.h"
#include "ed2k_link.h"
#include "ed2k_peer.h"
#include "ed2k_policy.h"
#include "fmt.h"
#include "prefs.h"
#include <algorithm>
#include <chrono>
#include <cinttypes>
#include <cstdint>
#include <ctime>
#include <limits>
#include <memory>
#include <utility>
#include "ed2k/Ed2kCommandInternal.h"

namespace aria2 {
using namespace ed2k_command;

Ed2kCommand::Ed2kCommand(cuid_t cuid, RequestGroup* requestGroup,
                         DownloadEngine* e, ed2k::Endpoint endpoint,
                         bool serverMode, bool countAsDownloadCommand,
                         bool firewallCheck)
    : AbstractCommand(cuid, makeEd2kRequest(endpoint, serverMode),
                      requestGroup->getDownloadContext()->getFirstFileEntry(),
                      requestGroup, e, nullptr, nullptr, true,
                      countAsDownloadCommand),
      mode_(serverMode ? Mode::SERVER : Mode::PEER),
      endpoint_(std::move(endpoint)),
      state_(State::INIT),
      resolveRequestId_(0),
      connectedPort_(0),
      headerRead_(0),
      bodyRead_(0),
      peerFileStatusReceived_(false),
      peerFileRequestSent_(false),
      peerFileStatusRequested_(false),
      peerAccepted_(false),
      sourceExchangeRequested_(false),
      aichFileHashRequested_(false),
      use64BitOffsets_(requestGroup->getDownloadContext()->getTotalLength() >
                       std::numeric_limits<uint32_t>::max()),
      incoming_(false),
      countAsDownloadCommand_(countAsDownloadCommand),
      firewallCheck_(firewallCheck),
      closeAfterOutbox_(false),
      tailReclaimTimer_(Timer::zero()),
      localPeerInfo_(ed2k::createLocalEmulePeerInfo()),
      obfuscationWriteOffset_(0),
      obfuscationMagicRead_(0),
      obfuscationMethodRead_(0),
      incomingObfuscationMethodRead_(0),
      incomingObfuscationMarker_(0),
      obfuscationPaddingRead_(0),
      obfuscationEnabled_(false),
      serverObfuscation_(false)
{
  if (mode_ == Mode::SERVER) {
    e->addEd2kServerConnection();
    auto rgman = e->getRequestGroupMan().get();
    if (rgman) {
      rgman->getEd2kSession()->registerDownload(requestGroup);
    }
  }
  localPeerInfo_.udpPort = localEd2kUdpPort(e);
  localPeerInfo_.miscOptions.udpVersion = localPeerInfo_.udpPort == 0 ? 0 : 4;
  const auto configuredTimeout = getOption()->getAsInt(PREF_CONNECT_TIMEOUT);
  setTimeout(std::chrono::seconds(mode_ == Mode::SERVER
                                      ? std::min<int64_t>(configuredTimeout, 25)
                                      : configuredTimeout));
  if (!firewallCheck_ && getSegmentMan()) {
    peerStat_ = getRequest()->initPeerStat();
    peerStat_->downloadStart();
    getSegmentMan()->registerPeerStat(peerStat_);
  }
  disableReadCheckSocket();
  disableWriteCheckSocket();
  if (mode_ == Mode::PEER && !firewallCheck_) {
    markEd2kPeerConnecting(getEd2kAttrs(getDownloadContext()), endpoint_);
  }
}

Ed2kCommand::Ed2kCommand(cuid_t cuid, RequestGroup* requestGroup,
                         DownloadEngine* e, ed2k::Endpoint endpoint,
                         const std::shared_ptr<SocketCore>& socket)
    : AbstractCommand(cuid, makeEd2kRequest(endpoint, false),
                      requestGroup->getDownloadContext()->getFirstFileEntry(),
                      requestGroup, e, socket, nullptr, true, true),
      mode_(Mode::PEER),
      endpoint_(std::move(endpoint)),
      state_(State::INCOMING_READ_MARKER),
      resolveRequestId_(0),
      connectedPort_(endpoint_.port),
      headerRead_(0),
      bodyRead_(0),
      peerFileStatusReceived_(false),
      peerFileRequestSent_(false),
      peerFileStatusRequested_(false),
      peerAccepted_(false),
      sourceExchangeRequested_(false),
      aichFileHashRequested_(false),
      use64BitOffsets_(requestGroup->getDownloadContext()->getTotalLength() >
                       std::numeric_limits<uint32_t>::max()),
      incoming_(true),
      countAsDownloadCommand_(true),
      firewallCheck_(false),
      closeAfterOutbox_(false),
      tailReclaimTimer_(Timer::zero()),
      localPeerInfo_(ed2k::createLocalEmulePeerInfo()),
      obfuscationWriteOffset_(0),
      obfuscationMagicRead_(0),
      obfuscationMethodRead_(0),
      incomingObfuscationMethodRead_(0),
      incomingObfuscationMarker_(0),
      obfuscationPaddingRead_(0),
      obfuscationEnabled_(false),
      serverObfuscation_(false)
{
  localPeerInfo_.udpPort = localEd2kUdpPort(e);
  localPeerInfo_.miscOptions.udpVersion = localPeerInfo_.udpPort == 0 ? 0 : 4;
  setTimeout(std::chrono::seconds(getOption()->getAsInt(PREF_CONNECT_TIMEOUT)));
  if (getSegmentMan()) {
    peerStat_ = getRequest()->initPeerStat();
    peerStat_->downloadStart();
    getSegmentMan()->registerPeerStat(peerStat_);
  }
  markEd2kPeerConnecting(getEd2kAttrs(getDownloadContext()), endpoint_);
}

Ed2kCommand::~Ed2kCommand()
{
  if (resolveRequestId_ != 0 && getDownloadEngine()) {
    getDownloadEngine()->getSystemResolver()->cancel(resolveRequestId_);
    resolveRequestId_ = 0;
  }
  if (mode_ == Mode::SERVER && getDownloadEngine()) {
    getDownloadEngine()->removeEd2kServerConnection();
    auto rgman = getDownloadEngine()->getRequestGroupMan().get();
    if (rgman) {
      auto session = rgman->getEd2kSession();
      for (auto group : session->downloads()) {
        auto state = getEd2kServerState(
            getEd2kAttrs(group->getDownloadContext()), endpoint_);
        if (state) {
          state->connected = false;
          state->connecting = false;
          state->handshakeCompleted = false;
        }
      }
    }
  }
  resetCompressedPartInflaters();
  if (mode_ == Mode::PEER && !firewallCheck_ && getDownloadContext()) {
    markEd2kPeerDisconnected(getEd2kAttrs(getDownloadContext()), endpoint_);
  }
  if (mode_ == Mode::PEER && !firewallCheck_ && getDownloadEngine() &&
      getDownloadEngine()->getRequestGroupMan() &&
      getDownloadEngine()->getRequestGroupMan()->getEd2kUploadQueue()) {
    getDownloadEngine()->getRequestGroupMan()->getEd2kUploadQueue()->disconnect(
        endpoint_);
  }
}

bool Ed2kCommand::protocolDeadlineActive() const
{
  if (getTimeout().count() <= 0) {
    return false;
  }
  if (mode_ == Mode::SERVER) {
    const auto state =
        getEd2kServerState(getEd2kAttrs(getDownloadContext()), endpoint_);
    return !state || !state->handshakeCompleted;
  }
  return !peerAccepted_;
}

void Ed2kCommand::noteProtocolActivity()
{
  protocolActivity_ = global::wallclock();
}

bool Ed2kCommand::execute()
{
  try {
    if (getRequestGroup()->isHaltRequested() ||
        (getRequestGroup()->downloadFinished() && mode_ == Mode::PEER &&
         !incoming_ && outbox_.empty())) {
      return true;
    }
    if (mode_ == Mode::SERVER) {
      const auto attrs = getEd2kAttrs(getDownloadContext());
      const auto otherConnected =
          std::find_if(attrs->serverStates.begin(), attrs->serverStates.end(),
                       [&](const ed2k::ServerState& server) {
                         return server.handshakeCompleted &&
                                (server.endpoint.host != endpoint_.host ||
                                 server.endpoint.port != endpoint_.port);
                       });
      if (otherConnected != attrs->serverStates.end()) {
        return true;
      }
    }
    const auto bufferedInput =
        getSocket() && getSocket()->getRecvBufferedLength() != 0;
    const auto socketReadable = getSocket() && getSocket()->isReadable(0);
    if (protocolDeadlineActive() &&
        protocolActivity_.difference(global::wallclock()) >= getTimeout() &&
        !readEventEnabled() && !writeEventEnabled() && !errorEventEnabled() &&
        !hupEventEnabled() && !bufferedInput && !socketReadable) {
      throw DL_RETRY_EX("ED2K protocol handshake timed out.");
    }
    if (downloadRateLimited()) {
      addCommandSelf();
      disableReadCheckSocket();
      disableWriteCheckSocket();
      return false;
    }
    queueDueServerRequest();
    return executeInternal();
  }
  catch (DlAbortEx& err) {
    getRequestGroup()->setLastErrorCode(err.getErrorCode(), err.what());
    A2_LOG_ERROR(
        fmt("component=ed2k event=connection_failed gid=%s cuid=%" PRId64
            " role=%s endpoint=%s:%u error_code=%d message=%s",
            GroupId::toHex(getRequestGroup()->getGID()).c_str(), getCuid(),
            mode_ == Mode::SERVER ? "server" : "peer",
            logging::sanitizeText(endpoint_.host).c_str(), endpoint_.port,
            static_cast<int>(err.getErrorCode()),
            logging::sanitizeText(err.what()).c_str()));
    return true;
  }
  catch (DlRetryEx& err) {
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                         global::wallclock().getTime().time_since_epoch())
                         .count();
    const auto retryWait =
        std::max<int64_t>(1, getOption()->getAsInt(PREF_RETRY_WAIT));
    if (mode_ == Mode::SERVER) {
      auto session =
          getDownloadEngine()->getRequestGroupMan()->getEd2kSession();
      for (auto group : session->downloads()) {
        auto attrs = getEd2kAttrs(group->getDownloadContext());
        auto state = getEd2kServerState(attrs, endpoint_);
        if (serverObfuscation_ && state) {
          state->tcpObfuscationFailed = true;
        }
        updateEd2kServerFailure(attrs, endpoint_, now, retryWait);
      }
    }
    else if (!firewallCheck_) {
      markEd2kPeerFailure(getEd2kAttrs(getDownloadContext()), endpoint_, now,
                          retryWait);
      scheduleEd2kPeerCheck(getRequestGroup(), getDownloadEngine());
    }
    A2_LOG_DEBUG(
        fmt("component=ed2k event=connection_retry gid=%s cuid=%" PRId64
            " role=%s endpoint=%s:%u message=%s",
            GroupId::toHex(getRequestGroup()->getGID()).c_str(), getCuid(),
            mode_ == Mode::SERVER ? "server" : "peer",
            logging::sanitizeText(endpoint_.host).c_str(), endpoint_.port,
            logging::sanitizeText(err.what()).c_str()));
    return true;
  }
  catch (DownloadFailureException& err) {
    getRequestGroup()->setLastErrorCode(err.getErrorCode(), err.what());
    getRequestGroup()->setHaltRequested(true);
    A2_LOG_ERROR(fmt("component=ed2k event=task_failed gid=%s cuid=%" PRId64
                     " endpoint=%s:%u error_code=%d message=%s",
                     GroupId::toHex(getRequestGroup()->getGID()).c_str(),
                     getCuid(), logging::sanitizeText(endpoint_.host).c_str(),
                     endpoint_.port, static_cast<int>(err.getErrorCode()),
                     logging::sanitizeText(err.what()).c_str()));
    return true;
  }
}

void Ed2kCommand::handlePacket()
{
  if (mode_ == Mode::SERVER) {
    handleServerPacket();
  }
  else {
    handlePeerPacket();
  }
}

bool Ed2kCommand::executeInternal()
{
  while (true) {
    switch (state_) {
    case State::INIT:
      if (incoming_) {
        state_ = State::READ_HEADER;
        break;
      }
      if (mode_ == Mode::SERVER) {
        queueServerLogin();
      }
      else {
        queuePeerHello();
      }
      startResolve();
      return false;
    case State::RESOLVING:
      startResolve();
      return false;
    case State::CONNECTING:
      if (!getSocket()->isWritable(0)) {
        setWriteCheckSocket(getSocket());
        addCommandSelf();
        return false;
      }
      if (!checkIfConnectionEstablished(getSocket(), connectedHostname_,
                                        connectedAddr_, connectedPort_)) {
        return true;
      }
      noteProtocolActivity();
      if (mode_ == Mode::SERVER) {
        updateEd2kServerConnected(getEd2kAttrs(getDownloadContext()),
                                  endpoint_);
      }
      if (serverObfuscation_) {
        initServerObfuscation();
        state_ = State::SERVER_OBFUSCATION_WRITE;
      }
      else if (shouldObfuscatePeerConnection()) {
        initPeerObfuscation();
        state_ = State::OBFUSCATION_WRITE;
      }
      else {
        state_ = State::WRITE;
      }
      break;
    case State::INCOMING_READ_MARKER:
      if (!readIncomingObfuscationMarker()) {
        return false;
      }
      break;
    case State::INCOMING_READ_RANDOM:
      if (!readIncomingObfuscationRandom()) {
        return false;
      }
      break;
    case State::INCOMING_READ_MAGIC:
      if (!readIncomingObfuscationMagic()) {
        return false;
      }
      break;
    case State::INCOMING_READ_METHOD:
      if (!readIncomingObfuscationMethod()) {
        return false;
      }
      break;
    case State::INCOMING_READ_PADDING:
      if (!readIncomingObfuscationPadding()) {
        return false;
      }
      break;
    case State::INCOMING_WRITE_RESPONSE:
      if (!flushIncomingObfuscationResponse()) {
        return false;
      }
      break;
    case State::OBFUSCATION_WRITE:
      if (!flushObfuscationHandshake()) {
        return false;
      }
      break;
    case State::OBFUSCATION_READ_MAGIC:
      if (!readObfuscationMagic()) {
        return false;
      }
      break;
    case State::OBFUSCATION_READ_METHOD:
      if (!readObfuscationMethod()) {
        return false;
      }
      break;
    case State::OBFUSCATION_READ_PADDING:
      if (!readObfuscationPadding()) {
        return false;
      }
      break;
    case State::SERVER_OBFUSCATION_WRITE:
      if (!flushServerObfuscationRequest()) {
        return false;
      }
      break;
    case State::SERVER_OBFUSCATION_READ_KEY:
      if (!readServerObfuscationKey()) {
        return false;
      }
      break;
    case State::SERVER_OBFUSCATION_READ_MAGIC:
      if (!readServerObfuscationMagic()) {
        return false;
      }
      break;
    case State::SERVER_OBFUSCATION_READ_METHOD:
      if (!readServerObfuscationMethod()) {
        return false;
      }
      break;
    case State::SERVER_OBFUSCATION_READ_PADDING:
      if (!readServerObfuscationPadding()) {
        return false;
      }
      break;
    case State::SERVER_OBFUSCATION_WRITE_RESPONSE:
      if (!flushServerObfuscationResponse()) {
        return false;
      }
      break;
    case State::WRITE:
      if (!flushOutbox()) {
        return false;
      }
      break;
    case State::READ_HEADER:
      if (sendPendingCancelTransfer()) {
        break;
      }
      if (expireStalledTransfer()) {
        break;
      }
      if (queueActivePeerPartReclaim()) {
        break;
      }
      if (!readHeader()) {
        return false;
      }
      break;
    case State::READ_BODY:
      if (sendPendingCancelTransfer()) {
        break;
      }
      if (expireStalledTransfer()) {
        break;
      }
      if (queueActivePeerPartReclaim()) {
        break;
      }
      if (!readBody()) {
        return false;
      }
      break;
    case State::DONE:
      return true;
    }
    if (state_ != State::DONE && state_ != State::OBFUSCATION_WRITE &&
        state_ != State::OBFUSCATION_READ_MAGIC &&
        state_ != State::OBFUSCATION_READ_METHOD &&
        state_ != State::OBFUSCATION_READ_PADDING &&
        state_ != State::SERVER_OBFUSCATION_WRITE &&
        state_ != State::SERVER_OBFUSCATION_READ_KEY &&
        state_ != State::SERVER_OBFUSCATION_READ_MAGIC &&
        state_ != State::SERVER_OBFUSCATION_READ_METHOD &&
        state_ != State::SERVER_OBFUSCATION_READ_PADDING &&
        state_ != State::SERVER_OBFUSCATION_WRITE_RESPONSE &&
        !outbox_.empty()) {
      state_ = State::WRITE;
    }
  }
}

bool Ed2kCommand::prepareForRetry(time_t)
{
  if (incoming_ || getRequestGroup()->isHaltRequested()) {
    return true;
  }
  getDownloadEngine()->addCommand(make_unique<Ed2kCommand>(
      getDownloadEngine()->newCUID(), getRequestGroup(), getDownloadEngine(),
      endpoint_, mode_ == Mode::SERVER, countAsDownloadCommand_,
      firewallCheck_));
  getDownloadEngine()->setNoWait(true);
  return true;
}

bool Ed2kCommand::noCheck() const
{
  if (protocolDeadlineActive() &&
      protocolActivity_.difference(global::wallclock()) >= getTimeout()) {
    return true;
  }
  if (mode_ != Mode::PEER || incoming_ || !peerAccepted_ ||
      (state_ != State::READ_HEADER && state_ != State::READ_BODY)) {
    return false;
  }
  if (tailReclaimTimer_.difference(global::wallclock()) < 10_s) {
    return false;
  }
  const auto attrs = getEd2kAttrs(getDownloadContext());
  const auto state = getEd2kPeerState(attrs, endpoint_);
  return state && state->accepted && !state->dead && !state->cancelled &&
         !state->noFile && !state->outOfParts && !state->remoteQueueFull &&
         !state->udpReaskPending && state->requestedParts.size() < 3 &&
         canReclaimEd2kStalledRequestedRange(
             attrs, endpoint_, state->partStatus, nowSeconds(),
             ed2k::ACTIVE_ENDGAME_RECLAIM_STALL_SECONDS);
}

void Ed2kCommand::handlePeerPacket()
{
  routeIncomingFileRequest();
  auto* attrs = getEd2kAttrs(getDownloadContext());
  if (currentHeader_.protocol == ed2k::PROTO_EMULE &&
      currentHeader_.opcode != ed2k::OP_COMPRESSEDPART &&
      currentHeader_.opcode != ed2k::OP_COMPRESSEDPART_I64) {
    handleEmulePacket(attrs);
    return;
  }
  handleEdonkeyPacket(attrs);
}

} // namespace aria2
