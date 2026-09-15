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

#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <limits>

#include "ARC4Encryptor.h"
#include "DlAbortEx.h"
#include "DlRetryEx.h"
#include "DiskAdaptor.h"
#include "DownloadContext.h"
#include "DownloadEngine.h"
#include "DownloadFailureException.h"
#include "Ed2kAttribute.h"
#include "Ed2kPeerTransfer.h"
#include "Ed2kShareIndex.h"
#include "Ed2kSharedResponder.h"
#include "Ed2kSession.h"
#include "Ed2kUploadQueue.h"
#include "FileEntry.h"
#include "GroupId.h"
#include "Log.h"
#include "message.h"
#include "Option.h"
#include "PeerStat.h"
#include "Piece.h"
#include "PieceStorage.h"
#include "Request.h"
#include "RequestGroup.h"
#include "RequestGroupMan.h"
#include "Segment.h"
#include "SegmentMan.h"
#include "ServerStat.h"
#include "SimpleRandomizer.h"
#include "SocketCore.h"
#include "SystemResolver.h"
#include "ed2k_aich.h"
#include "ed2k_compression.h"
#include "ed2k_constants.h"
#include "ed2k_crypto.h"
#include "ed2k_hash.h"
#include "ed2k_peer.h"
#include "ed2k_policy.h"
#include "ed2k_search.h"
#include "ed2k_server.h"
#include "error_code.h"
#include "fmt.h"
#include "prefs.h"
#include "util.h"
#include "wallclock.h"

namespace aria2 {

namespace {
constexpr uint8_t ED2K_OBFUSCATION_MAGIC_REQUESTER = 34;
constexpr uint8_t ED2K_OBFUSCATION_MAGIC_SERVER = 203;
constexpr uint32_t ED2K_OBFUSCATION_SYNC = 0x835e6fc4;
constexpr uint8_t ED2K_OBFUSCATION_METHOD = 0;

std::shared_ptr<Request> makeEd2kRequest(const ed2k::Endpoint& endpoint,
                                         bool serverMode)
{
  auto req = std::make_shared<Request>();
  req->setUri(fmt("ed2k-peer://%s:%u/", endpoint.host.c_str(), endpoint.port));
  if (serverMode) {
    req->setUri(fmt("ed2k-server://%s:%u/", endpoint.host.c_str(),
                    endpoint.port));
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
  auto existing = std::find_if(
      attrs->aichRecoverySets.begin(), attrs->aichRecoverySets.end(),
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
} // namespace

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
  setTimeout(std::chrono::seconds(
      mode_ == Mode::SERVER ? std::min<int64_t>(configuredTimeout, 25)
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
        auto state =
            getEd2kServerState(getEd2kAttrs(group->getDownloadContext()),
                               endpoint_);
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

bool Ed2kCommand::queueDueServerRequest()
{
  if (mode_ != Mode::SERVER || state_ != State::READ_HEADER ||
      !outbox_.empty()) {
    return false;
  }
  auto attrs = getEd2kAttrs(getDownloadContext());
  auto state = getEd2kServerState(attrs, endpoint_);
  if (!state || !state->connected || attrs->searchActive ||
      !ed2k::serverTcpSourceRequestDue(*state, attrs->link.size,
                                       nowSeconds())) {
    return false;
  }
  if (!queueAllServerSourceRequests()) {
    markEd2kServerTcpSourceRequestSent(attrs, endpoint_, nowSeconds());
    return false;
  }
  state_ = State::WRITE;
  return true;
}

bool Ed2kCommand::protocolDeadlineActive() const
{
  if (getTimeout().count() <= 0) {
    return false;
  }
  if (mode_ == Mode::SERVER) {
    const auto state = getEd2kServerState(
        getEd2kAttrs(getDownloadContext()), endpoint_);
    return !state || !state->handshakeCompleted;
  }
  return !peerAccepted_;
}

void Ed2kCommand::noteProtocolActivity()
{
  protocolActivity_ = global::wallclock();
}

bool Ed2kCommand::downloadRateLimited() const
{
  if (mode_ != Mode::PEER || state_ != State::READ_BODY ||
      (currentHeader_.opcode != ed2k::OP_SENDINGPART &&
       currentHeader_.opcode != ed2k::OP_SENDINGPART_I64 &&
       currentHeader_.opcode != ed2k::OP_COMPRESSEDPART &&
       currentHeader_.opcode != ed2k::OP_COMPRESSEDPART_I64)) {
    return false;
  }
  const auto rgman = getDownloadEngine()->getRequestGroupMan().get();
  return (rgman && rgman->doesOverallDownloadSpeedExceed()) ||
         getRequestGroup()->doesDownloadSpeedExceed();
}

bool Ed2kCommand::uploadRateLimited() const
{
  if (outboxTransferData_.empty() || !outboxTransferData_.front()) {
    return false;
  }
  const auto rgman = getDownloadEngine()->getRequestGroupMan().get();
  return (rgman && rgman->doesOverallUploadSpeedExceed()) ||
         getRequestGroup()->doesUploadSpeedExceed();
}

bool Ed2kCommand::shouldObfuscatePeerConnection() const
{
  if (mode_ != Mode::PEER || incoming_ ||
      endpoint_.userHash.size() != ed2k::HASH_LENGTH) {
    return false;
  }
  const bool peerSupports =
      (endpoint_.cryptOptions &
       (ed2k::SOURCE_CRYPT_SUPPORT | ed2k::SOURCE_CRYPT_REQUIRE)) != 0;
  const bool peerWants =
      (endpoint_.cryptOptions &
       (ed2k::SOURCE_CRYPT_REQUEST | ed2k::SOURCE_CRYPT_REQUIRE)) != 0;
  return peerSupports && peerWants;
}

void Ed2kCommand::initPeerObfuscation()
{
  uint32_t randomKeyPart = 0;
  util::generateRandomData(reinterpret_cast<unsigned char*>(&randomKeyPart),
                           sizeof(randomKeyPart));

  auto sendKey = ed2k::createTcpObfuscationKey(
      endpoint_.userHash, ED2K_OBFUSCATION_MAGIC_REQUESTER, randomKeyPart);
  auto receiveKey = ed2k::createTcpObfuscationKey(
      endpoint_.userHash, ED2K_OBFUSCATION_MAGIC_SERVER, randomKeyPart);

  obfuscationEncryptor_ = make_unique<ARC4Encryptor>();
  obfuscationEncryptor_->init(
      reinterpret_cast<const unsigned char*>(sendKey.data()), sendKey.size());
  discardArc4Prefix(*obfuscationEncryptor_);

  obfuscationDecryptor_ = make_unique<ARC4Encryptor>();
  obfuscationDecryptor_->init(
      reinterpret_cast<const unsigned char*>(receiveKey.data()),
      receiveKey.size());
  discardArc4Prefix(*obfuscationDecryptor_);

  unsigned char randomBytes[18];
  util::generateRandomData(randomBytes, sizeof(randomBytes));
  uint8_t marker = 1;
  for (auto randomByte : randomBytes) {
    if (!isEd2kProtocolMarker(randomByte)) {
      marker = randomByte;
      break;
    }
  }
  const uint8_t paddingLength = randomBytes[1] % 16;

  std::string plain;
  plain.push_back(static_cast<char>(marker));
  plain += ed2k::packUInt32(randomKeyPart);
  plain += ed2k::packUInt32(ED2K_OBFUSCATION_SYNC);
  plain.push_back(static_cast<char>(ED2K_OBFUSCATION_METHOD));
  plain.push_back(static_cast<char>(ED2K_OBFUSCATION_METHOD));
  plain.push_back(static_cast<char>(paddingLength));
  plain.append(reinterpret_cast<const char*>(randomBytes + 2), paddingLength);

  obfuscationWriteBuf_ = plain;
  obfuscationEncryptor_->encrypt(
      obfuscationWriteBuf_.size() - 5,
      reinterpret_cast<unsigned char*>(&obfuscationWriteBuf_[5]),
      reinterpret_cast<const unsigned char*>(plain.data() + 5));
  obfuscationWriteOffset_ = 0;
  obfuscationMagicRead_ = 0;
  obfuscationMethodRead_ = 0;
  obfuscationPaddingRead_ = 0;
  obfuscationPaddingBuf_.clear();
  A2_LOG_TRACE(fmt("CUID#%" PRId64
                   " - Starting obfuscated ED2K peer handshake with %s:%u.",
                   getCuid(), endpoint_.host.c_str(), endpoint_.port));
}

bool Ed2kCommand::flushObfuscationHandshake()
{
  while (obfuscationWriteOffset_ < obfuscationWriteBuf_.size()) {
    auto written = getSocket()->writeData(
        obfuscationWriteBuf_.data() + obfuscationWriteOffset_,
        obfuscationWriteBuf_.size() - obfuscationWriteOffset_);
    if (written == 0) {
      setWriteCheckSocketIf(getSocket(), getSocket()->wantWrite());
      setReadCheckSocketIf(getSocket(), getSocket()->wantRead());
      addCommandSelf();
      return false;
    }
    obfuscationWriteOffset_ += static_cast<size_t>(written);
  }
  disableWriteCheckSocket();
  setReadCheckSocket(getSocket());
  state_ = State::OBFUSCATION_READ_MAGIC;
  return true;
}

bool Ed2kCommand::readObfuscationMagic()
{
  while (obfuscationMagicRead_ < obfuscationMagicBuf_.size()) {
    size_t len = obfuscationMagicBuf_.size() - obfuscationMagicRead_;
    getSocket()->readData(obfuscationMagicBuf_.data() + obfuscationMagicRead_,
                          len);
    if (len == 0) {
      if (!getSocket()->wantRead() && !getSocket()->wantWrite()) {
        throw DL_RETRY_EX("ED2K obfuscation handshake closed.");
      }
      setReadCheckSocketIf(getSocket(), getSocket()->wantRead());
      setWriteCheckSocketIf(getSocket(), getSocket()->wantWrite());
      addCommandSelf();
      return false;
    }
    decryptData(obfuscationMagicBuf_.data() + obfuscationMagicRead_, len);
    obfuscationMagicRead_ += len;
  }
  if (ed2k::readUInt32(obfuscationMagicBuf_.data()) !=
      ED2K_OBFUSCATION_SYNC) {
    throw DL_RETRY_EX("Bad ED2K obfuscation magic.");
  }
  obfuscationMethodRead_ = 0;
  state_ = State::OBFUSCATION_READ_METHOD;
  return true;
}

bool Ed2kCommand::readObfuscationMethod()
{
  while (obfuscationMethodRead_ < obfuscationMethodBuf_.size()) {
    size_t len = obfuscationMethodBuf_.size() - obfuscationMethodRead_;
    getSocket()->readData(
        obfuscationMethodBuf_.data() + obfuscationMethodRead_, len);
    if (len == 0) {
      if (!getSocket()->wantRead() && !getSocket()->wantWrite()) {
        throw DL_RETRY_EX("ED2K obfuscation handshake closed.");
      }
      setReadCheckSocketIf(getSocket(), getSocket()->wantRead());
      setWriteCheckSocketIf(getSocket(), getSocket()->wantWrite());
      addCommandSelf();
      return false;
    }
    decryptData(obfuscationMethodBuf_.data() + obfuscationMethodRead_, len);
    obfuscationMethodRead_ += len;
  }
  if (static_cast<uint8_t>(obfuscationMethodBuf_[0]) !=
      ED2K_OBFUSCATION_METHOD) {
    throw DL_RETRY_EX("Unsupported ED2K obfuscation method.");
  }
  obfuscationPaddingBuf_.assign(
      static_cast<uint8_t>(obfuscationMethodBuf_[1]), '\0');
  obfuscationPaddingRead_ = 0;
  state_ = State::OBFUSCATION_READ_PADDING;
  return true;
}

bool Ed2kCommand::readObfuscationPadding()
{
  while (obfuscationPaddingRead_ < obfuscationPaddingBuf_.size()) {
    size_t len = obfuscationPaddingBuf_.size() - obfuscationPaddingRead_;
    getSocket()->readData(&obfuscationPaddingBuf_[obfuscationPaddingRead_],
                          len);
    if (len == 0) {
      if (!getSocket()->wantRead() && !getSocket()->wantWrite()) {
        throw DL_RETRY_EX("ED2K obfuscation handshake closed.");
      }
      setReadCheckSocketIf(getSocket(), getSocket()->wantRead());
      setWriteCheckSocketIf(getSocket(), getSocket()->wantWrite());
      addCommandSelf();
      return false;
    }
    decryptData(&obfuscationPaddingBuf_[obfuscationPaddingRead_], len);
    obfuscationPaddingRead_ += len;
  }
  obfuscationEnabled_ = true;
  A2_LOG_TRACE(fmt("CUID#%" PRId64
                   " - ED2K peer obfuscation handshake completed with %s:%u.",
                   getCuid(), endpoint_.host.c_str(), endpoint_.port));
  state_ = State::WRITE;
  return true;
}

bool Ed2kCommand::readIncomingObfuscationMarker()
{
  size_t len = 1;
  getSocket()->readData(&incomingObfuscationMarker_, len);
  if (len == 0) {
    if (!getSocket()->wantRead() && !getSocket()->wantWrite()) {
      throw DL_RETRY_EX("ED2K incoming handshake closed.");
    }
    setReadCheckSocketIf(getSocket(), getSocket()->wantRead());
    setWriteCheckSocketIf(getSocket(), getSocket()->wantWrite());
    addCommandSelf();
    return false;
  }
  if (isEd2kProtocolMarker(
          static_cast<uint8_t>(incomingObfuscationMarker_))) {
    headerBuf_[0] = incomingObfuscationMarker_;
    headerRead_ = 1;
    state_ = State::READ_HEADER;
    return true;
  }
  obfuscationMagicRead_ = 0;
  state_ = State::INCOMING_READ_RANDOM;
  return true;
}

bool Ed2kCommand::readIncomingObfuscationRandom()
{
  while (obfuscationMagicRead_ < obfuscationMagicBuf_.size()) {
    size_t len = obfuscationMagicBuf_.size() - obfuscationMagicRead_;
    getSocket()->readData(obfuscationMagicBuf_.data() + obfuscationMagicRead_,
                          len);
    if (len == 0) {
      if (!getSocket()->wantRead() && !getSocket()->wantWrite()) {
        throw DL_RETRY_EX("ED2K incoming obfuscation handshake closed.");
      }
      setReadCheckSocketIf(getSocket(), getSocket()->wantRead());
      setWriteCheckSocketIf(getSocket(), getSocket()->wantWrite());
      addCommandSelf();
      return false;
    }
    obfuscationMagicRead_ += len;
  }

  const auto randomKeyPart = ed2k::readUInt32(obfuscationMagicBuf_.data());
  const auto userHash = getEd2kAttrs(getDownloadContext())->clientHash;
  auto receiveKey = ed2k::createTcpObfuscationKey(
      userHash, ED2K_OBFUSCATION_MAGIC_REQUESTER, randomKeyPart);
  auto sendKey = ed2k::createTcpObfuscationKey(
      userHash, ED2K_OBFUSCATION_MAGIC_SERVER, randomKeyPart);
  obfuscationDecryptor_ = make_unique<ARC4Encryptor>();
  obfuscationDecryptor_->init(
      reinterpret_cast<const unsigned char*>(receiveKey.data()),
      receiveKey.size());
  discardArc4Prefix(*obfuscationDecryptor_);
  obfuscationEncryptor_ = make_unique<ARC4Encryptor>();
  obfuscationEncryptor_->init(
      reinterpret_cast<const unsigned char*>(sendKey.data()), sendKey.size());
  discardArc4Prefix(*obfuscationEncryptor_);

  obfuscationMagicRead_ = 0;
  state_ = State::INCOMING_READ_MAGIC;
  return true;
}

bool Ed2kCommand::readIncomingObfuscationMagic()
{
  while (obfuscationMagicRead_ < obfuscationMagicBuf_.size()) {
    size_t len = obfuscationMagicBuf_.size() - obfuscationMagicRead_;
    getSocket()->readData(obfuscationMagicBuf_.data() + obfuscationMagicRead_,
                          len);
    if (len == 0) {
      if (!getSocket()->wantRead() && !getSocket()->wantWrite()) {
        throw DL_RETRY_EX("ED2K incoming obfuscation handshake closed.");
      }
      setReadCheckSocketIf(getSocket(), getSocket()->wantRead());
      setWriteCheckSocketIf(getSocket(), getSocket()->wantWrite());
      addCommandSelf();
      return false;
    }
    decryptData(obfuscationMagicBuf_.data() + obfuscationMagicRead_, len);
    obfuscationMagicRead_ += len;
  }
  if (ed2k::readUInt32(obfuscationMagicBuf_.data()) !=
      ED2K_OBFUSCATION_SYNC) {
    throw DL_RETRY_EX("Bad incoming ED2K obfuscation magic.");
  }
  incomingObfuscationMethodRead_ = 0;
  state_ = State::INCOMING_READ_METHOD;
  return true;
}

bool Ed2kCommand::readIncomingObfuscationMethod()
{
  while (incomingObfuscationMethodRead_ <
         incomingObfuscationMethodBuf_.size()) {
    size_t len = incomingObfuscationMethodBuf_.size() -
                 incomingObfuscationMethodRead_;
    getSocket()->readData(
        incomingObfuscationMethodBuf_.data() + incomingObfuscationMethodRead_,
        len);
    if (len == 0) {
      if (!getSocket()->wantRead() && !getSocket()->wantWrite()) {
        throw DL_RETRY_EX("ED2K incoming obfuscation handshake closed.");
      }
      setReadCheckSocketIf(getSocket(), getSocket()->wantRead());
      setWriteCheckSocketIf(getSocket(), getSocket()->wantWrite());
      addCommandSelf();
      return false;
    }
    decryptData(incomingObfuscationMethodBuf_.data() +
                    incomingObfuscationMethodRead_,
                len);
    incomingObfuscationMethodRead_ += len;
  }
  obfuscationPaddingBuf_.assign(
      static_cast<uint8_t>(incomingObfuscationMethodBuf_[2]), '\0');
  obfuscationPaddingRead_ = 0;
  state_ = State::INCOMING_READ_PADDING;
  return true;
}

bool Ed2kCommand::readIncomingObfuscationPadding()
{
  while (obfuscationPaddingRead_ < obfuscationPaddingBuf_.size()) {
    size_t len = obfuscationPaddingBuf_.size() - obfuscationPaddingRead_;
    getSocket()->readData(&obfuscationPaddingBuf_[obfuscationPaddingRead_],
                          len);
    if (len == 0) {
      if (!getSocket()->wantRead() && !getSocket()->wantWrite()) {
        throw DL_RETRY_EX("ED2K incoming obfuscation handshake closed.");
      }
      setReadCheckSocketIf(getSocket(), getSocket()->wantRead());
      setWriteCheckSocketIf(getSocket(), getSocket()->wantWrite());
      addCommandSelf();
      return false;
    }
    decryptData(&obfuscationPaddingBuf_[obfuscationPaddingRead_], len);
    obfuscationPaddingRead_ += len;
  }

  obfuscationWriteBuf_ = ed2k::packUInt32(ED2K_OBFUSCATION_SYNC);
  obfuscationWriteBuf_.push_back(
      static_cast<char>(ED2K_OBFUSCATION_METHOD));
  obfuscationWriteBuf_.push_back('\0');
  obfuscationEncryptor_->encrypt(
      obfuscationWriteBuf_.size(),
      reinterpret_cast<unsigned char*>(&obfuscationWriteBuf_[0]),
      reinterpret_cast<const unsigned char*>(obfuscationWriteBuf_.data()));
  obfuscationWriteOffset_ = 0;
  state_ = State::INCOMING_WRITE_RESPONSE;
  return true;
}

bool Ed2kCommand::flushIncomingObfuscationResponse()
{
  while (obfuscationWriteOffset_ < obfuscationWriteBuf_.size()) {
    auto written = getSocket()->writeData(
        obfuscationWriteBuf_.data() + obfuscationWriteOffset_,
        obfuscationWriteBuf_.size() - obfuscationWriteOffset_);
    if (written == 0) {
      setWriteCheckSocketIf(getSocket(), getSocket()->wantWrite());
      setReadCheckSocketIf(getSocket(), getSocket()->wantRead());
      addCommandSelf();
      return false;
    }
    obfuscationWriteOffset_ += static_cast<size_t>(written);
  }
  disableWriteCheckSocket();
  setReadCheckSocket(getSocket());
  obfuscationEnabled_ = true;
  A2_LOG_TRACE(fmt("CUID#%" PRId64
                   " - Incoming ED2K peer obfuscation completed with %s:%u.",
                   getCuid(), endpoint_.host.c_str(), endpoint_.port));
  state_ = State::READ_HEADER;
  return true;
}

bool Ed2kCommand::shouldObfuscateServerConnection() const
{
  if (mode_ != Mode::SERVER || incoming_) {
    return false;
  }
  const auto state = getEd2kServerState(
      getEd2kAttrs(getDownloadContext()), endpoint_);
  return state && state->tcpObfuscationPort != 0 &&
         !state->tcpObfuscationFailed;
}

void Ed2kCommand::initServerObfuscation()
{
  MSEDHPrivateKey privateKey{};
  util::generateRandomData(privateKey.data() + 4, privateKey.size() - 4);
  if (std::all_of(privateKey.begin(), privateKey.end(),
                  [](unsigned char value) { return value == 0; })) {
    privateKey.back() = 1;
  }
  serverDh_ =
      make_unique<DHKeyExchange>(privateKey, ed2k::SERVER_DH_PRIME_HEX);

  std::array<unsigned char, 17> randomBytes;
  util::generateRandomData(randomBytes.data(), randomBytes.size());
  uint8_t marker = 1;
  for (auto value : randomBytes) {
    if (!isEd2kProtocolMarker(value)) {
      marker = value;
      break;
    }
  }
  const auto paddingLength = static_cast<uint8_t>(randomBytes[0] % 16);
  obfuscationWriteBuf_.assign(1, static_cast<char>(marker));
  const auto& publicKey = serverDh_->getPublicKey();
  obfuscationWriteBuf_.append(
      reinterpret_cast<const char*>(publicKey.data()), publicKey.size());
  obfuscationWriteBuf_.push_back(static_cast<char>(paddingLength));
  obfuscationWriteBuf_.append(
      reinterpret_cast<const char*>(randomBytes.data() + 1), paddingLength);
  obfuscationWriteOffset_ = 0;
  serverDhPeerKeyRead_ = 0;
  A2_LOG_TRACE(fmt("CUID#%" PRId64
                   " - Starting obfuscated ED2K server handshake with %s:%u.",
                   getCuid(), endpoint_.host.c_str(), connectedPort_));
}

bool Ed2kCommand::flushServerObfuscationRequest()
{
  while (obfuscationWriteOffset_ < obfuscationWriteBuf_.size()) {
    const auto written = getSocket()->writeData(
        obfuscationWriteBuf_.data() + obfuscationWriteOffset_,
        obfuscationWriteBuf_.size() - obfuscationWriteOffset_);
    if (written == 0) {
      setWriteCheckSocketIf(getSocket(), getSocket()->wantWrite());
      setReadCheckSocketIf(getSocket(), getSocket()->wantRead());
      addCommandSelf();
      return false;
    }
    obfuscationWriteOffset_ += static_cast<size_t>(written);
  }
  disableWriteCheckSocket();
  setReadCheckSocket(getSocket());
  state_ = State::SERVER_OBFUSCATION_READ_KEY;
  return true;
}

bool Ed2kCommand::readServerObfuscationKey()
{
  while (serverDhPeerKeyRead_ < serverDhPeerKey_.size()) {
    size_t length = serverDhPeerKey_.size() - serverDhPeerKeyRead_;
    getSocket()->readData(
        reinterpret_cast<char*>(serverDhPeerKey_.data()) +
            serverDhPeerKeyRead_,
        length);
    if (length == 0) {
      if (!getSocket()->wantRead() && !getSocket()->wantWrite()) {
        throw DL_RETRY_EX("ED2K server obfuscation handshake closed.");
      }
      setReadCheckSocketIf(getSocket(), getSocket()->wantRead());
      setWriteCheckSocketIf(getSocket(), getSocket()->wantWrite());
      addCommandSelf();
      return false;
    }
    serverDhPeerKeyRead_ += length;
  }

  const auto secret = serverDh_->computeSecret(serverDhPeerKey_);
  serverDh_.reset();
  const std::string sharedSecret(
      reinterpret_cast<const char*>(secret.data()), secret.size());
  const auto sendKey = ed2k::createServerTcpObfuscationKey(
      sharedSecret, ED2K_OBFUSCATION_MAGIC_REQUESTER);
  const auto receiveKey = ed2k::createServerTcpObfuscationKey(
      sharedSecret, ED2K_OBFUSCATION_MAGIC_SERVER);
  obfuscationEncryptor_ = make_unique<ARC4Encryptor>();
  obfuscationEncryptor_->init(
      reinterpret_cast<const unsigned char*>(sendKey.data()), sendKey.size());
  discardArc4Prefix(*obfuscationEncryptor_);
  obfuscationDecryptor_ = make_unique<ARC4Encryptor>();
  obfuscationDecryptor_->init(
      reinterpret_cast<const unsigned char*>(receiveKey.data()),
      receiveKey.size());
  discardArc4Prefix(*obfuscationDecryptor_);
  obfuscationMagicRead_ = 0;
  state_ = State::SERVER_OBFUSCATION_READ_MAGIC;
  return true;
}

bool Ed2kCommand::readServerObfuscationMagic()
{
  while (obfuscationMagicRead_ < obfuscationMagicBuf_.size()) {
    size_t length = obfuscationMagicBuf_.size() - obfuscationMagicRead_;
    getSocket()->readData(obfuscationMagicBuf_.data() + obfuscationMagicRead_,
                          length);
    if (length == 0) {
      if (!getSocket()->wantRead() && !getSocket()->wantWrite()) {
        throw DL_RETRY_EX("ED2K server obfuscation handshake closed.");
      }
      setReadCheckSocketIf(getSocket(), getSocket()->wantRead());
      setWriteCheckSocketIf(getSocket(), getSocket()->wantWrite());
      addCommandSelf();
      return false;
    }
    decryptData(obfuscationMagicBuf_.data() + obfuscationMagicRead_, length);
    obfuscationMagicRead_ += length;
  }
  if (ed2k::readUInt32(obfuscationMagicBuf_.data()) !=
      ED2K_OBFUSCATION_SYNC) {
    throw DL_RETRY_EX("Bad ED2K server obfuscation magic.");
  }
  incomingObfuscationMethodRead_ = 0;
  state_ = State::SERVER_OBFUSCATION_READ_METHOD;
  return true;
}

bool Ed2kCommand::readServerObfuscationMethod()
{
  while (incomingObfuscationMethodRead_ <
         incomingObfuscationMethodBuf_.size()) {
    size_t length = incomingObfuscationMethodBuf_.size() -
                    incomingObfuscationMethodRead_;
    getSocket()->readData(
        incomingObfuscationMethodBuf_.data() + incomingObfuscationMethodRead_,
        length);
    if (length == 0) {
      if (!getSocket()->wantRead() && !getSocket()->wantWrite()) {
        throw DL_RETRY_EX("ED2K server obfuscation handshake closed.");
      }
      setReadCheckSocketIf(getSocket(), getSocket()->wantRead());
      setWriteCheckSocketIf(getSocket(), getSocket()->wantWrite());
      addCommandSelf();
      return false;
    }
    decryptData(incomingObfuscationMethodBuf_.data() +
                    incomingObfuscationMethodRead_,
                length);
    incomingObfuscationMethodRead_ += length;
  }
  if (static_cast<uint8_t>(incomingObfuscationMethodBuf_[1]) !=
      ED2K_OBFUSCATION_METHOD) {
    throw DL_RETRY_EX("Unsupported ED2K server obfuscation method.");
  }
  obfuscationPaddingBuf_.assign(
      static_cast<uint8_t>(incomingObfuscationMethodBuf_[2]), '\0');
  obfuscationPaddingRead_ = 0;
  state_ = State::SERVER_OBFUSCATION_READ_PADDING;
  return true;
}

bool Ed2kCommand::readServerObfuscationPadding()
{
  while (obfuscationPaddingRead_ < obfuscationPaddingBuf_.size()) {
    size_t length = obfuscationPaddingBuf_.size() - obfuscationPaddingRead_;
    getSocket()->readData(&obfuscationPaddingBuf_[obfuscationPaddingRead_],
                          length);
    if (length == 0) {
      if (!getSocket()->wantRead() && !getSocket()->wantWrite()) {
        throw DL_RETRY_EX("ED2K server obfuscation handshake closed.");
      }
      setReadCheckSocketIf(getSocket(), getSocket()->wantRead());
      setWriteCheckSocketIf(getSocket(), getSocket()->wantWrite());
      addCommandSelf();
      return false;
    }
    decryptData(&obfuscationPaddingBuf_[obfuscationPaddingRead_], length);
    obfuscationPaddingRead_ += length;
  }

  obfuscationWriteBuf_ = ed2k::packUInt32(ED2K_OBFUSCATION_SYNC);
  obfuscationWriteBuf_.push_back(
      static_cast<char>(ED2K_OBFUSCATION_METHOD));
  obfuscationWriteBuf_.push_back('\0');
  obfuscationEncryptor_->encrypt(
      obfuscationWriteBuf_.size(),
      reinterpret_cast<unsigned char*>(&obfuscationWriteBuf_[0]),
      reinterpret_cast<const unsigned char*>(obfuscationWriteBuf_.data()));
  obfuscationWriteOffset_ = 0;
  state_ = State::SERVER_OBFUSCATION_WRITE_RESPONSE;
  return true;
}

bool Ed2kCommand::flushServerObfuscationResponse()
{
  while (obfuscationWriteOffset_ < obfuscationWriteBuf_.size()) {
    const auto written = getSocket()->writeData(
        obfuscationWriteBuf_.data() + obfuscationWriteOffset_,
        obfuscationWriteBuf_.size() - obfuscationWriteOffset_);
    if (written == 0) {
      setWriteCheckSocketIf(getSocket(), getSocket()->wantWrite());
      setReadCheckSocketIf(getSocket(), getSocket()->wantRead());
      addCommandSelf();
      return false;
    }
    obfuscationWriteOffset_ += static_cast<size_t>(written);
  }
  obfuscationEnabled_ = true;
  A2_LOG_TRACE(fmt("CUID#%" PRId64
                   " - ED2K server obfuscation completed with %s:%u.",
                   getCuid(), endpoint_.host.c_str(), connectedPort_));
  state_ = State::WRITE;
  return true;
}

void Ed2kCommand::encryptPacket(std::string& data)
{
  if (!obfuscationEnabled_ || !obfuscationEncryptor_ || data.empty()) {
    return;
  }
  obfuscationEncryptor_->encrypt(
      data.size(), reinterpret_cast<unsigned char*>(&data[0]),
      reinterpret_cast<const unsigned char*>(data.data()));
}

void Ed2kCommand::decryptData(char* data, size_t length)
{
  if (!obfuscationDecryptor_ || length == 0) {
    return;
  }
  obfuscationDecryptor_->encrypt(
      length, reinterpret_cast<unsigned char*>(data),
      reinterpret_cast<const unsigned char*>(data));
}

void Ed2kCommand::resetCompressedPartInflaters()
{
  compressedPartStates_.clear();
}

Ed2kCommand::CompressedPartState*
Ed2kCommand::findCompressedPartState(int64_t begin)
{
  for (const auto& state : compressedPartStates_) {
    if (state && state->block.begin <= begin && begin < state->block.end) {
      return state.get();
    }
  }
  return nullptr;
}

Ed2kCommand::CompressedPartState*
Ed2kCommand::getOrCreateCompressedPartState(const ed2k::PartRange& block)
{
  if (block.end <= block.begin) {
    return nullptr;
  }
  if (auto state = findCompressedPartState(block.begin)) {
    return state;
  }
  auto state = make_unique<CompressedPartState>();
  state->block = block;
  compressedPartStates_.push_back(std::move(state));
  return compressedPartStates_.back().get();
}

void Ed2kCommand::releaseCompletedCompressedPartState(
    const ed2k::PartRange& block)
{
  compressedPartStates_.erase(
      std::remove_if(compressedPartStates_.begin(), compressedPartStates_.end(),
                     [&](const std::unique_ptr<CompressedPartState>& state) {
                       return !state || (state->block.begin == block.begin &&
                                         state->block.end == block.end);
                     }),
      compressedPartStates_.end());
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
      const auto otherConnected = std::find_if(
          attrs->serverStates.begin(), attrs->serverStates.end(),
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

void Ed2kCommand::queuePacket(uint8_t protocol, uint8_t opcode,
                              const std::string& payload)
{
  outbox_.push_back(ed2k::createPacket(protocol, opcode, payload));
  outboxEncrypted_.push_back(false);
  outboxTransferData_.push_back(
      opcode == ed2k::OP_SENDINGPART || opcode == ed2k::OP_SENDINGPART_I64 ||
      opcode == ed2k::OP_COMPRESSEDPART ||
      opcode == ed2k::OP_COMPRESSEDPART_I64);
}

void Ed2kCommand::queueServerLogin()
{
  auto attrs = getEd2kAttrs(getDownloadContext());
  queuePacket(ed2k::PROTO_EDONKEY, ed2k::OP_LOGINREQUEST,
              ed2k::createLoginRequestPayload(attrs->clientHash,
                                              0,
                                              localEd2kTcpPort(getDownloadEngine()),
                                              "aria2-next"));
}

void Ed2kCommand::queueServerOfferFiles()
{
  auto rgman = getDownloadEngine()->getRequestGroupMan().get();
  auto state = getEd2kServerState(getEd2kAttrs(getDownloadContext()),
                                  endpoint_);
  if (!rgman || !state || !state->handshakeCompleted) {
    return;
  }
  auto sources = ed2k::listSharedSources(rgman);
  std::string payload;
  const auto limit = state->softFiles == 0 ? static_cast<size_t>(200)
                                           : std::min<size_t>(state->softFiles,
                                                              200);
  const bool supportsLarge =
      (state->tcpFlags & ed2k::SRV_TCPFLG_LARGEFILES) != 0;
  if (!ed2k::createOfferFilesPayload(
          payload, sources, supportsLarge, limit,
          state->highId ? state->clientId : 0,
          state->highId ? localEd2kTcpPort(getDownloadEngine()) : 0)) {
    return;
  }
  queuePacket(ed2k::PROTO_EDONKEY, ed2k::OP_OFFERFILES, payload);
}

bool Ed2kCommand::queueGetSources(RequestGroup* group)
{
  group = group ? group : getRequestGroup();
  const auto attrs = getEd2kAttrs(group->getDownloadContext());
  const auto state = getEd2kServerState(attrs, endpoint_);
  if (attrs->link.size > std::numeric_limits<uint32_t>::max() &&
      state && (state->tcpFlags & ed2k::SRV_TCPFLG_LARGEFILES) == 0) {
    A2_LOG_DEBUG(fmt("CUID#%" PRId64
                    " - ED2K server %s:%u does not advertise large-file "
                    "source requests for %s.",
                    getCuid(), endpoint_.host.c_str(), endpoint_.port,
                    util::toHex(attrs->link.hash).c_str()));
    return false;
  }
  const bool requestObfuSources =
      state && (state->tcpFlags & ed2k::SRV_TCPFLG_TCPOBFUSCATION) != 0;
  queuePacket(ed2k::PROTO_EDONKEY,
              requestObfuSources ? ed2k::OP_GETSOURCES_OBFU
                                 : ed2k::OP_GETSOURCES,
              ed2k::createGetSourcesPayload(attrs->link.hash,
                                            attrs->link.size));
  return true;
}

bool Ed2kCommand::queueAllServerSourceRequests()
{
  bool queued = false;
  auto rgman = getDownloadEngine()->getRequestGroupMan().get();
  if (!rgman) {
    return queueGetSources();
  }
  auto session = rgman->getEd2kSession();
  auto primaryState =
      getEd2kServerState(getEd2kAttrs(getDownloadContext()), endpoint_);
  for (auto group : session->downloads()) {
    auto attrs = getEd2kAttrs(group->getDownloadContext());
    if (!attrs || attrs->searchActive || group->downloadFinished()) {
      continue;
    }
    auto state = getEd2kServerState(attrs, endpoint_);
    if (primaryState && state != primaryState) {
      state->connected = primaryState->connected;
      state->connecting = primaryState->connecting;
      state->handshakeCompleted = primaryState->handshakeCompleted;
      state->clientId = primaryState->clientId;
      state->highId = primaryState->highId;
      state->ipAddress = primaryState->ipAddress;
      state->tcpFlags = primaryState->tcpFlags;
      state->tcpObfuscationPort = primaryState->tcpObfuscationPort;
    }
    if (queueGetSources(group)) {
      queued = true;
    }
    markEd2kServerTcpSourceRequestSent(attrs, endpoint_, nowSeconds());
  }
  return queued;
}

void Ed2kCommand::queueSearchRequest()
{
  const auto attrs = getEd2kAttrs(getDownloadContext());
  if (!attrs->searchActive) {
    return;
  }
  queuePacket(ed2k::PROTO_EDONKEY, ed2k::OP_SEARCHREQUEST,
              ed2k::createSearchRequestPayload(
                  attrs->searchQuery,
                  attrs->link.size >
                      static_cast<int64_t>(std::numeric_limits<uint32_t>::max())));
}

void Ed2kCommand::queueCallbackRequest(RequestGroup* group, uint32_t clientId)
{
  pendingCallbacks_.push_back({group, clientId});
  constexpr int64_t CALLBACK_TIMEOUT = 45;
  markEd2kCallbackRequestSent(getEd2kAttrs(group->getDownloadContext()), clientId,
                              nowSeconds(), CALLBACK_TIMEOUT);
  queuePacket(ed2k::PROTO_EDONKEY, ed2k::OP_CALLBACKREQUEST,
              ed2k::createCallbackRequestPayload(clientId));
}

uint32_t Ed2kCommand::localEd2kClientId() const
{
  auto attrs = getEd2kAttrs(getDownloadContext());
  if (!attrs) {
    return 0;
  }
  for (const auto& state : attrs->serverStates) {
    if (state.handshakeCompleted && state.clientId != 0) {
      return state.clientId;
    }
  }
  return 0;
}

ed2k::Endpoint Ed2kCommand::localEd2kServerEndpoint() const
{
  auto attrs = getEd2kAttrs(getDownloadContext());
  if (!attrs) {
    return ed2k::Endpoint();
  }
  for (const auto& state : attrs->serverStates) {
    if (state.handshakeCompleted && state.clientId != 0) {
      return state.endpoint;
    }
  }
  return ed2k::Endpoint();
}

void Ed2kCommand::queuePeerHello()
{
  auto attrs = getEd2kAttrs(getDownloadContext());
  auto payload = ed2k::createPeerHelloPayload(
      attrs->clientHash, localEd2kClientId(),
      localEd2kTcpPort(getDownloadEngine()), localEd2kServerEndpoint(),
      "aria2-next", localPeerInfo_, true);
  queuePacket(ed2k::PROTO_EDONKEY, ed2k::OP_HELLO, payload);
}

void Ed2kCommand::queuePeerHelloAnswer()
{
  auto attrs = getEd2kAttrs(getDownloadContext());
  auto payload = ed2k::createPeerHelloPayload(
      attrs->clientHash, localEd2kClientId(),
      localEd2kTcpPort(getDownloadEngine()), localEd2kServerEndpoint(),
      "aria2-next", localPeerInfo_, false);
  queuePacket(ed2k::PROTO_EDONKEY, ed2k::OP_HELLOANSWER, payload);
}

void Ed2kCommand::queueEmuleInfo(bool answer)
{
  queuePacket(ed2k::PROTO_EMULE,
              answer ? ed2k::OP_EMULEINFOANSWER : ed2k::OP_EMULEINFO,
              ed2k::createEmuleInfoPayload(localPeerInfo_));
}

void Ed2kCommand::queuePeerFileRequest()
{
  if (getRequestGroup()->downloadFinished()) {
    return;
  }
  if (peerFileRequestSent_) {
    return;
  }
  peerFileRequestSent_ = true;
  auto attrs = getEd2kAttrs(getDownloadContext());
  const auto localPartStatus =
      createLocalPartStatus(getDownloadContext().get(), getPieceStorage().get());

  if (remotePeerInfo_.miscOptions.multiPacket) {
    const bool extendedMultipacket =
        remotePeerInfo_.miscOptions2.supportsExtendedMultipacket;
    queuePacket(ed2k::PROTO_EMULE,
                extendedMultipacket ? ed2k::OP_MULTIPACKET_EXT
                                    : ed2k::OP_MULTIPACKET,
                ed2k::createMultipacketFileRequestPayload(
                    attrs->link.hash, getDownloadContext()->getTotalLength(),
                    localPartStatus, remotePeerInfo_, extendedMultipacket));
    if (localPartStatus.size() > 1) {
      peerFileStatusRequested_ = true;
    }
    if (remotePeerInfo_.miscOptions2.supportsSourceExchange2 ||
        remotePeerInfo_.miscOptions.sourceExchange1Version > 1) {
      sourceExchangeRequested_ = true;
    }
    if (remotePeerInfo_.miscOptions.aichVersion > 0 &&
        attrs->aichRootHash.empty()) {
      aichFileHashRequested_ = true;
    }
    return;
  }

  queuePacket(ed2k::PROTO_EDONKEY, ed2k::OP_REQUESTFILENAME,
              createPeerFileRequestPayload(
                  getDownloadContext().get(), getPieceStorage().get(),
                  attrs->link.hash, remotePeerInfo_.miscOptions
                                        .extendedRequestsVersion));
  if (localPartStatus.size() > 1) {
    queuePeerFileStatusRequest();
  }
  queueSourceExchangeRequest();
  queueAichFileHashRequest();
}

void Ed2kCommand::queuePeerFileStatusRequest()
{
  if (peerFileStatusRequested_) {
    return;
  }
  peerFileStatusRequested_ = true;
  queuePacket(ed2k::PROTO_EDONKEY, ed2k::OP_SETREQFILEID,
              getEd2kAttrs(getDownloadContext())->link.hash);
}

void Ed2kCommand::queuePeerHashSetRequest()
{
  queuePacket(ed2k::PROTO_EDONKEY, ed2k::OP_HASHSETREQUEST,
              getEd2kAttrs(getDownloadContext())->link.hash);
}

void Ed2kCommand::queuePeerPostFileStatusRequests()
{
  auto attrs = getEd2kAttrs(getDownloadContext());
  if (ed2k::hashSetPartCount(getDownloadContext()->getTotalLength()) > 0 &&
      attrs->pieceHashes.empty()) {
    queuePeerHashSetRequest();
  }
  else {
    queuePeerStartUpload();
  }
}

void Ed2kCommand::queueAichFileHashRequest()
{
  auto attrs = getEd2kAttrs(getDownloadContext());
  if (aichFileHashRequested_ || remotePeerInfo_.miscOptions.aichVersion == 0 ||
      !attrs->aichRootHash.empty()) {
    return;
  }
  aichFileHashRequested_ = true;
  queuePacket(ed2k::PROTO_EMULE, ed2k::OP_AICHFILEHASHREQ,
              ed2k::createAichFileHashRequestPayload(attrs->link.hash));
}

void Ed2kCommand::queueAichRecoveryRequest(size_t pieceIndex)
{
  auto attrs = getEd2kAttrs(getDownloadContext());
  if (remotePeerInfo_.miscOptions.aichVersion == 0 ||
      attrs->aichRootHash.empty() || !attrs->aichRootTrusted ||
      pieceIndex > std::numeric_limits<uint16_t>::max()) {
    return;
  }
  queuePacket(ed2k::PROTO_EMULE, ed2k::OP_AICHREQUEST,
              ed2k::createAichRequestPayload(
                  attrs->link.hash, static_cast<uint16_t>(pieceIndex),
                  attrs->aichRootHash));
}

void Ed2kCommand::queueSourceExchangeRequest()
{
  if (sourceExchangeRequested_ ||
      (remotePeerInfo_.miscOptions.sourceExchange1Version == 0 &&
       !remotePeerInfo_.miscOptions2.supportsSourceExchange2)) {
    return;
  }
  sourceExchangeRequested_ = true;
  const auto request = ed2k::createRequestSourcesPayload(
      getEd2kAttrs(getDownloadContext())->link.hash, remotePeerInfo_);
  if (request.opcode != 0) {
    queuePacket(ed2k::PROTO_EMULE, request.opcode, request.payload);
  }
}

void Ed2kCommand::queueSourceExchangeAnswer(uint8_t version)
{
  auto attrs = getEd2kAttrs(getDownloadContext());
  std::vector<ed2k::SourceExchangeEntry> entries;
  entries.reserve(std::min<size_t>(attrs->peers.size(), 500));
  for (const auto& peer : attrs->peers) {
    if (entries.size() >= 500) {
      break;
    }
    if (peer.host.empty() || peer.port == 0 ||
        (peer.host == endpoint_.host && peer.port == endpoint_.port)) {
      continue;
    }
    ed2k::SourceExchangeEntry entry;
    entry.endpoint = peer;
    entry.server.host = "0.0.0.0";
    entry.server.port = 0;
    entry.userHash = peer.userHash.empty()
                         ? std::string(ed2k::HASH_LENGTH, '\0')
                         : peer.userHash;
    entry.cryptOptions = static_cast<uint8_t>(peer.cryptOptions & 0xff);
    entries.push_back(std::move(entry));
  }
  if (entries.empty()) {
    return;
  }
  if (version >= 2) {
    queuePacket(ed2k::PROTO_EMULE, ed2k::OP_ANSWERSOURCES2,
                ed2k::createAnswerSources2Payload(attrs->link.hash, version,
                                                  entries));
  }
  else {
    queuePacket(ed2k::PROTO_EMULE, ed2k::OP_ANSWERSOURCES,
                ed2k::createAnswerSourcesPayload(attrs->link.hash, version,
                                                 entries));
  }
}

void Ed2kCommand::queuePeerStartUpload()
{
  queuePacket(ed2k::PROTO_EDONKEY, ed2k::OP_STARTUPLOADREQ,
              getEd2kAttrs(getDownloadContext())->link.hash);
}

void Ed2kCommand::queuePeerPartRequest()
{
  if (getRequestGroup()->downloadFinished()) {
    closeAfterOutbox_ = true;
    return;
  }
  std::vector<ed2k::PartRange> ranges;
  const auto attrs = getEd2kAttrs(getDownloadContext());
  auto state = getEd2kPeerState(attrs, endpoint_);
  const std::vector<ed2k::PartRange> outstanding =
      state ? state->requestedParts : std::vector<ed2k::PartRange>();

  if (outstanding.size() >= 3) {
    return;
  }
  const auto maxNewRanges = 3 - outstanding.size();

  std::vector<ed2k::PieceSelectionCandidate> candidates;
  const auto pieceLength = getDownloadContext()->getPieceLength();
  const auto continuingPiece =
      outstanding.empty()
          ? getDownloadContext()->getNumPieces()
          : static_cast<size_t>(outstanding.front().begin / pieceLength);
  for (size_t index = 0; index < getDownloadContext()->getNumPieces(); ++index) {
    if (getPieceStorage()->hasPiece(index)) {
      continue;
    }
    if (state && !state->partStatus.empty() &&
        (index >= state->partStatus.size() || !state->partStatus[index])) {
      continue;
    }

    size_t frequency = 0;
    for (const auto& peer : attrs->peerStates) {
      if (index < peer.partStatus.size() && peer.partStatus[index]) {
        ++frequency;
      }
    }
    auto localPiece = getPieceStorage()->getPiece(index);
    size_t completedBlocks = 0;
    if (localPiece) {
      for (size_t block = 0; block < localPiece->countBlock(); ++block) {
        completedBlocks += localPiece->hasBlock(block) ? 1 : 0;
      }
    }
    const auto pieceBegin = static_cast<int64_t>(index) * pieceLength;
    const auto pieceEnd = std::min(pieceBegin + static_cast<int64_t>(pieceLength),
                                   getDownloadContext()->getTotalLength());
    const auto requested = std::any_of(
        attrs->requestedPartRanges.begin(), attrs->requestedPartRanges.end(),
        [&](const ed2k::PartRange& range) {
          return range.begin < pieceEnd && range.end > pieceBegin;
        });
    bool preview = false;
    if (getRequestGroup()->getOption()->getAsBool(PREF_ED2K_PREVIEW_PRIORITY)) {
      const auto pieceCount = getDownloadContext()->getNumPieces();
      preview = index == 0 || index + 1 == pieceCount;
      if (!preview && index + 2 == pieceCount && pieceCount > 1) {
        const auto lastBegin =
            static_cast<int64_t>(pieceCount - 1) * pieceLength;
        preview = getDownloadContext()->getTotalLength() - lastBegin <
                  pieceLength / 3;
      }
    }
    ed2k::PieceSelectionCandidate candidate;
    candidate.index = index;
    candidate.frequency = frequency;
    candidate.completedBlocks = completedBlocks;
    candidate.totalBlocks = localPiece ? localPiece->countBlock()
                                       : static_cast<size_t>(
                                             (pieceEnd - pieceBegin +
                                              Piece::BLOCK_LENGTH - 1) /
                                             Piece::BLOCK_LENGTH);
    candidate.requested = requested;
    candidate.preview = preview;
    candidate.continuing = index == continuingPiece;
    candidates.push_back(candidate);
  }
  std::shuffle(candidates.begin(), candidates.end(),
               *SimpleRandomizer::getInstance());
  const auto sourceCount = attrs->peerStates.size();
  std::stable_sort(
      candidates.begin(), candidates.end(),
      [&](const auto& lhs, const auto& rhs) {
        return ed2k::rankPieceSelection(lhs, sourceCount) <
               ed2k::rankPieceSelection(rhs, sourceCount);
      });

  for (const auto& candidate : candidates) {
    if (ranges.size() >= maxNewRanges) {
      break;
    }
    const auto index = candidate.index;

    const int64_t pieceBegin =
        static_cast<int64_t>(index) * getDownloadContext()->getPieceLength();
    const int64_t pieceEnd =
        std::min(pieceBegin +
                     static_cast<int64_t>(getDownloadContext()->getPieceLength()),
                 getDownloadContext()->getTotalLength());
    auto piece = getPieceStorage()->getPiece(index);
    const auto pieceBlockLength = static_cast<int64_t>(Piece::BLOCK_LENGTH);
    const auto requestBlockLength =
        static_cast<int64_t>(ed2k::BLOCK_LENGTH / Piece::BLOCK_LENGTH) *
        pieceBlockLength;
    for (int64_t begin = pieceBegin;
         ranges.size() < maxNewRanges && begin < pieceEnd;) {
      const auto block =
          static_cast<size_t>((begin - pieceBegin) / pieceBlockLength);
      if (piece && piece->hasBlock(block)) {
        begin += pieceBlockLength;
        continue;
      }
      auto end = std::min(begin + requestBlockLength, pieceEnd);
      if (piece) {
        for (auto next = begin + pieceBlockLength; next < end;
             next += pieceBlockLength) {
          const auto nextBlock =
              static_cast<size_t>((next - pieceBegin) / pieceBlockLength);
          if (piece->hasBlock(nextBlock)) {
            end = next;
            break;
          }
        }
      }
      ed2k::PartRange range;
      range.begin = begin;
      range.end = end;
      if (!blockRangeAvailable(attrs, range) ||
          !blockRangeAvailable(ranges, range)) {
        begin += pieceBlockLength;
        continue;
      }
      ranges.push_back(range);
      begin = end;
    }
  }

  if (ranges.empty() && maxNewRanges > 0) {
    ed2k::PartRange reclaimed;
    const auto requesterPartStatus =
        state ? state->partStatus : std::vector<bool>();
    if (reclaimEd2kStalledRequestedRange(
            attrs, endpoint_, requesterPartStatus, nowSeconds(),
            ed2k::ENDGAME_RECLAIM_STALL_SECONDS, reclaimed) &&
        blockRangeAvailable(ranges, reclaimed)) {
      ranges.push_back(reclaimed);
      A2_LOG_TRACE(fmt("CUID#%" PRId64
                       " - Reclaimed stalled ED2K part request begin=%" PRId64
                       " end=%" PRId64 ".",
                       getCuid(), reclaimed.begin, reclaimed.end));
    }
  }

  if (ranges.empty()) {
    return;
  }

  for (const auto& range : ranges) {
    A2_LOG_TRACE(fmt("CUID#%" PRId64
                     " - Queue ED2K part request begin=%" PRId64
                     " end=%" PRId64 ".",
                     getCuid(), range.begin, range.end));
  }
  if (!state || state->requestedParts.empty()) {
    getSegmentMan()->getSegmentWithIndex(
        getCuid(), static_cast<size_t>(ranges.front().begin /
                                      getDownloadContext()->getPieceLength()));
  }
  auto requested = outstanding;
  requested.insert(requested.end(), ranges.begin(), ranges.end());
  updateEd2kPeerRequestedParts(attrs, endpoint_, requested, nowSeconds());
  const auto protocol =
      use64BitOffsets_ ? ed2k::PROTO_EMULE : ed2k::PROTO_EDONKEY;
  queuePacket(protocol, use64BitOffsets_ ? ed2k::OP_REQUESTPARTS_I64
                                         : ed2k::OP_REQUESTPARTS,
              ed2k::createRequestPartsPayload(attrs->link.hash, ranges,
                                              use64BitOffsets_));
}

bool Ed2kCommand::queueActivePeerPartReclaim()
{
  if (mode_ != Mode::PEER || incoming_ || !peerAccepted_ ||
      getRequestGroup()->downloadFinished() || !outbox_.empty()) {
    return false;
  }
  if (tailReclaimTimer_.difference(global::wallclock()) < 10_s) {
    return false;
  }
  tailReclaimTimer_ = global::wallclock();

  const auto attrs = getEd2kAttrs(getDownloadContext());
  auto state = getEd2kPeerState(attrs, endpoint_);
  if (!state || !state->accepted || state->dead || state->cancelled ||
      state->noFile || state->outOfParts || state->remoteQueueFull ||
      state->udpReaskPending || state->requestedParts.size() >= 3) {
    return false;
  }

  ed2k::PartRange reclaimed;
  if (!activelyReclaimEd2kStalledRequestedRange(
          attrs, endpoint_, state->partStatus, nowSeconds(), reclaimed) ||
      !blockRangeAvailable(state->requestedParts, reclaimed)) {
    return false;
  }

  std::vector<ed2k::PartRange> ranges;
  ranges.push_back(reclaimed);
  if (state->requestedParts.empty()) {
    getSegmentMan()->getSegmentWithIndex(
        getCuid(), static_cast<size_t>(reclaimed.begin /
                                      getDownloadContext()->getPieceLength()));
  }
  auto requested = state->requestedParts;
  requested.push_back(reclaimed);
  updateEd2kPeerRequestedParts(attrs, endpoint_, requested, nowSeconds());
  const auto protocol =
      use64BitOffsets_ ? ed2k::PROTO_EMULE : ed2k::PROTO_EDONKEY;
  queuePacket(protocol, use64BitOffsets_ ? ed2k::OP_REQUESTPARTS_I64
                                         : ed2k::OP_REQUESTPARTS,
              ed2k::createRequestPartsPayload(attrs->link.hash, ranges,
                                              use64BitOffsets_));
  A2_LOG_TRACE(fmt("CUID#%" PRId64
                   " - Actively reclaimed stalled ED2K part request begin=%"
                   PRId64 " end=%" PRId64 ".",
                   getCuid(), reclaimed.begin, reclaimed.end));
  state_ = State::WRITE;
  return true;
}

void Ed2kCommand::queueCancelTransfer()
{
  queuePacket(ed2k::PROTO_EDONKEY, ed2k::OP_CANCELTRANSFER, std::string());
}

bool Ed2kCommand::sendPendingCancelTransfer()
{
  if (mode_ != Mode::PEER || incoming_ || !peerAccepted_) {
    return false;
  }
  auto state = getEd2kPeerState(getEd2kAttrs(getDownloadContext()), endpoint_);
  if (!state || !state->cancelTransferSent || !state->requestedParts.empty()) {
    return false;
  }
  A2_LOG_TRACE(fmt("CUID#%" PRId64
                   " - Sending ED2K cancel transfer for reclaimed range.",
                   getCuid()));
  queueCancelTransfer();
  state_ = State::WRITE;
  peerAccepted_ = false;
  closeAfterOutbox_ = true;
  return true;
}

bool Ed2kCommand::expireStalledTransfer()
{
  if (mode_ != Mode::PEER || incoming_) {
    return false;
  }
  constexpr int64_t TRANSFER_TIMEOUT = 100;
  const auto retryWait =
      std::max<int64_t>(1, getOption()->getAsInt(PREF_RETRY_WAIT));
  if (!expireEd2kStalledPeerTransfer(getEd2kAttrs(getDownloadContext()),
                                     getSegmentMan().get(), endpoint_,
                                     getCuid(), nowSeconds(),
                                     TRANSFER_TIMEOUT, retryWait)) {
    return false;
  }
  queueCancelTransfer();
  state_ = State::WRITE;
  return true;
}

ed2k::SharedResponder Ed2kCommand::createSharedResponder()
{
  auto rgman = getDownloadEngine()->getRequestGroupMan().get();
  auto uploadQueue = rgman ? rgman->getEd2kUploadQueue() : nullptr;
  return ed2k::SharedResponder(uploadQueue, rgman, endpoint_,
                               remotePeerInfo_.userHash,
                               [this](uint8_t protocol, uint8_t opcode,
                                      const std::string& payload) {
                                 queuePacket(protocol, opcode, payload);
                               });
}

bool Ed2kCommand::updatePeerEndpointFromHello(bool helloPacket)
{
  const auto minimumSize = ed2k::HASH_LENGTH + 6 + (helloPacket ? 1 : 0);
  if (!incoming_ || body_.size() < minimumSize) {
    return true;
  }
  size_t offset = helloPacket ? 1 : 0;
  const auto userHash = body_.substr(offset, ed2k::HASH_LENGTH);
  offset += ed2k::HASH_LENGTH + 4;
  const auto listenPort = ed2k::readUInt16(body_.data() + offset);
  if (listenPort == 0 || listenPort == endpoint_.port) {
    auto state = getEd2kPeerState(getEd2kAttrs(getDownloadContext()), endpoint_);
    if (state && userHash.size() == ed2k::HASH_LENGTH) {
      state->endpoint.userHash = userHash;
    }
    return true;
  }

  auto attrs = getEd2kAttrs(getDownloadContext());
  auto oldEndpoint = endpoint_;
  ed2k::Endpoint newEndpoint = endpoint_;
  newEndpoint.port = listenPort;
  newEndpoint.userHash = userHash;
  auto findState = [&](const ed2k::Endpoint& peer) -> ed2k::PeerState* {
    auto i = std::find_if(attrs->peerStates.begin(), attrs->peerStates.end(),
                          [&](const ed2k::PeerState& state) {
                            return state.endpoint.host == peer.host &&
                                   state.endpoint.port == peer.port;
                          });
    return i == attrs->peerStates.end() ? nullptr : &*i;
  };
  auto oldState = findState(oldEndpoint);
  auto existingState = findState(newEndpoint);
  if (existingState && existingState != oldState &&
      (existingState->connecting || existingState->accepted)) {
    if (oldState) {
      oldState->connecting = false;
      oldState->accepted = false;
      releaseEd2kRequestedRanges(attrs, oldState->requestedParts);
      oldState->requestedParts.clear();
    }
    state_ = State::DONE;
    return false;
  }
  endpoint_ = newEndpoint;
  addEd2kPeer(attrs, endpoint_);
  auto newState = getEd2kPeerState(attrs, endpoint_);
  if (newState) {
    newState->connecting = true;
    newState->dead = false;
    newState->endpoint.userHash = userHash;
  }
  markEd2kDirectCallbackAccepted(attrs, endpoint_, nowSeconds());
  oldState = findState(oldEndpoint);
  if (oldState && oldState != newState) {
    oldState->connecting = false;
    oldState->accepted = false;
    releaseEd2kRequestedRanges(attrs, oldState->requestedParts);
    oldState->requestedParts.clear();
  }
  return true;
}

namespace {
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
} // namespace

void Ed2kCommand::routeIncomingFileRequest()
{
  if (!incoming_ || body_.size() < ed2k::HASH_LENGTH) {
    return;
  }
  switch (currentHeader_.opcode) {
  case ed2k::OP_REQUESTFILENAME:
  case ed2k::OP_SETREQFILEID:
  case ed2k::OP_HASHSETREQUEST:
  case ed2k::OP_STARTUPLOADREQ:
  case ed2k::OP_REQUESTPARTS:
  case ed2k::OP_REQUESTPARTS_I64:
  case ed2k::OP_AICHFILEHASHREQ:
  case ed2k::OP_MULTIPACKET:
  case ed2k::OP_MULTIPACKET_EXT:
    break;
  default:
    return;
  }

  const auto fileHash = body_.substr(0, ed2k::HASH_LENGTH);
  auto currentAttrs = getEd2kAttrs(getDownloadContext());
  if (currentAttrs && currentAttrs->link.hash == fileHash) {
    return;
  }
  auto session =
      getDownloadEngine()->getRequestGroupMan()->getEd2kSession();
  auto target = std::find_if(
      session->downloads().begin(), session->downloads().end(),
      [&](RequestGroup* group) {
        auto attrs = getEd2kAttrs(group->getDownloadContext());
        return attrs && !group->isHaltRequested() && attrs->link.hash == fileHash;
      });
  if (target == session->downloads().end()) {
    return;
  }

  if (currentAttrs) {
    markEd2kPeerDisconnected(currentAttrs, endpoint_);
  }
  changeRequestGroup(*target);
  use64BitOffsets_ = getDownloadContext()->getTotalLength() >
                     std::numeric_limits<uint32_t>::max();
  auto attrs = getEd2kAttrs(getDownloadContext());
  addEd2kPeer(attrs, endpoint_, ed2k::PEER_SOURCE_INCOMING);
  markEd2kPeerConnecting(attrs, endpoint_);
  A2_LOG_TRACE(fmt("CUID#%" PRId64
                   " - Routed incoming ED2K peer %s:%u to file %s.",
                   getCuid(), endpoint_.host.c_str(), endpoint_.port,
                   util::toHex(fileHash).c_str()));
}

bool Ed2kCommand::switchToAlternativeDownload()
{
  if (mode_ != Mode::PEER || incoming_ || firewallCheck_) {
    return false;
  }
  auto session =
      getDownloadEngine()->getRequestGroupMan()->getEd2kSession();
  auto target = session->findAlternativeDownload(getRequestGroup(), endpoint_);
  if (!target) {
    return false;
  }

  auto oldAttrs = getEd2kAttrs(getDownloadContext());
  if (auto oldState = getEd2kPeerState(oldAttrs, endpoint_)) {
    releaseEd2kRequestedRanges(oldAttrs, oldState->requestedParts);
    oldState->requestedParts.clear();
    oldState->accepted = false;
    oldState->connecting = false;
  }
  changeRequestGroup(target);
  use64BitOffsets_ = getDownloadContext()->getTotalLength() >
                     std::numeric_limits<uint32_t>::max();
  peerFileStatusReceived_ = false;
  peerFileRequestSent_ = false;
  peerFileStatusRequested_ = false;
  peerAccepted_ = false;
  sourceExchangeRequested_ = false;
  aichFileHashRequested_ = false;
  resetCompressedPartInflaters();

  auto attrs = getEd2kAttrs(getDownloadContext());
  addEd2kPeer(attrs, endpoint_, ed2k::PEER_SOURCE_EXCHANGE);
  markEd2kPeerConnecting(attrs, endpoint_);
  queuePeerFileRequest();
  state_ = State::WRITE;
  A2_LOG_DEBUG(fmt("CUID#%" PRId64
                   " - Switched ED2K peer %s:%u to alternative file %s.",
                   getCuid(), endpoint_.host.c_str(), endpoint_.port,
                   util::toHex(attrs->link.hash).c_str()));
  return true;
}

void Ed2kCommand::startResolve()
{
  uint16_t port = endpoint_.port;
  if (shouldObfuscateServerConnection()) {
    const auto state = getEd2kServerState(
        getEd2kAttrs(getDownloadContext()), endpoint_);
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
      engine->findAllCachedIPAddresses(
          std::back_inserter(resolvedAddresses_), endpoint_.host, port);
    }
    if (resolvedAddresses_.empty()) {
      resolveRequestId_ = engine->getSystemResolver()->resolve(
          endpoint_.host, port,
          !getOption()->getAsBool(PREF_DISABLE_IPV6), getTimeout());
      A2_LOG_DEBUG(fmt(MSG_RESOLVING_HOSTNAME, getCuid(),
                       endpoint_.host.c_str()));
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
          ->getOrCreateServerStat(endpoint_.host,
                                  mode_ == Mode::SERVER ? "ed2k-server"
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
  A2_LOG_DEBUG(fmt(MSG_NAME_RESOLUTION_COMPLETE, getCuid(),
                   endpoint_.host.c_str(),
                   strjoin(resolvedAddresses_.begin(),
                           resolvedAddresses_.end(), ", ").c_str()));
  connectedHostname_ = endpoint_.host;
  connectedAddr_ = ipaddr;
  connectedPort_ = port;
  startConnect();
}

void Ed2kCommand::startConnect()
{
  A2_LOG_DEBUG(fmt("CUID#%" PRId64 " - Connecting to ED2K %s %s:%u.",
                  getCuid(), mode_ == Mode::SERVER ? "server" : "peer",
                  connectedAddr_.c_str(), connectedPort_));
  createSocket();
  getSocket()->establishConnection(connectedAddr_, connectedPort_);
  setWriteCheckSocket(getSocket());
  state_ = State::CONNECTING;
  addCommandSelf();
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
    throw DL_RETRY_EX(
        fmt("Unsupported ED2K packet protocol 0x%02x.",
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
    A2_LOG_TRACE(fmt("CUID#%" PRId64
                     " - Unpacked ED2K packet opcode=0x%02x, "
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

void Ed2kCommand::handlePartData(int64_t begin, const std::string& data)
{
  const auto end = begin + static_cast<int64_t>(data.size());
  const auto attrs = getEd2kAttrs(getDownloadContext());
  const auto peerState = getEd2kPeerState(attrs, endpoint_);
  const auto requested = peerState && std::any_of(
      peerState->requestedParts.begin(), peerState->requestedParts.end(),
      [begin, end](const ed2k::PartRange& range) {
        return range.begin <= begin && end <= range.end;
      });
  if (!requested) {
    throw DL_RETRY_EX("Unrequested ED2K part data.");
  }
  ed2k::PeerTransfer transfer(getDownloadContext().get(), getPieceStorage().get(),
                              getSegmentMan().get(), getCuid());
  if (peerStat_) {
    peerStat_->updateDownload(data.size());
  }
  std::shared_ptr<Segment> completedSegment;
  try {
    completedSegment = transfer.writePartData(begin, data);
  }
  catch (DlRetryEx&) {
    if (!transfer.hasCorruptPiece()) {
      throw;
    }
    removeEd2kPeerCompletedRequestedRange(attrs, endpoint_, begin, end,
                                          nowSeconds());
    queueAichRecoveryRequest(transfer.corruptPieceIndex());
    return;
  }
  removeEd2kPeerCompletedRequestedRange(attrs, endpoint_, begin, end,
                                        nowSeconds());
  if (!completedSegment) {
    return;
  }
  transfer.completeVerifiedSegment(completedSegment);
  if (getRequestGroup()->downloadFinished()) {
    getRequestGroup()->enableSeedOnly();
  }
}

void Ed2kCommand::handleServerPacket()
{
  auto attrs = getEd2kAttrs(getDownloadContext());
  if (currentHeader_.opcode == ed2k::OP_IDCHANGE) {
    ed2k::ServerIdChange idChange;
    if (!ed2k::parseServerIdChangePayload(idChange, body_)) {
      throw DL_RETRY_EX("Bad ED2K server ID change.");
    }
    auto session =
        getDownloadEngine()->getRequestGroupMan()->getEd2kSession();
    for (auto group : session->downloads()) {
      updateEd2kServerIdChange(getEd2kAttrs(group->getDownloadContext()),
                               endpoint_, idChange);
    }
    A2_LOG_INFO(fmt("component=ed2k event=server_connected gid=%s cuid=%" PRId64
                    " endpoint=%s:%u id_type=%s client_id=0x%08x",
                    GroupId::toHex(getRequestGroup()->getGID()).c_str(),
                    getCuid(), logging::sanitizeText(endpoint_.host).c_str(),
                    endpoint_.port, idChange.highId ? "high" : "low",
                    idChange.clientId));
    if (attrs->searchActive) {
      queueServerOfferFiles();
      queueSearchRequest();
    }
    else {
      queueServerOfferFiles();
      A2_LOG_DEBUG(fmt("CUID#%" PRId64
                      " - ED2K server %s:%u requesting sources for %s.",
                      getCuid(), endpoint_.host.c_str(), endpoint_.port,
                      util::toHex(attrs->link.hash).c_str()));
      if (!queueAllServerSourceRequests()) {
        markEd2kServerSourceRequestFinished(attrs, endpoint_);
        state_ = State::READ_HEADER;
        return;
      }
    }
    state_ = State::WRITE;
    return;
  }
  if (currentHeader_.opcode == ed2k::OP_FOUNDSOURCES ||
      currentHeader_.opcode == ed2k::OP_FOUNDSOURCES_OBFU) {
    auto session =
        getDownloadEngine()->getRequestGroupMan()->getEd2kSession();
    RequestGroup* sourceGroup = nullptr;
    std::vector<ed2k::FoundSource> sources;
    for (auto group : session->downloads()) {
      auto candidateAttrs = getEd2kAttrs(group->getDownloadContext());
      if (candidateAttrs && ed2k::parseFoundSourcesPayload(
                                sources, body_, candidateAttrs->link.hash,
                                currentHeader_.opcode ==
                                    ed2k::OP_FOUNDSOURCES_OBFU)) {
        sourceGroup = group;
        attrs = candidateAttrs;
        break;
      }
      sources.clear();
    }
    if (!sourceGroup) {
      A2_LOG_DEBUG(fmt("CUID#%" PRId64
                      " - ED2K server %s:%u returned unusable sources.",
                      getCuid(), endpoint_.host.c_str(), endpoint_.port));
      updateEd2kServerSourceResponse(attrs, endpoint_, 0, nowSeconds());
      markEd2kServerSourceRequestFinished(attrs, endpoint_);
      state_ = State::READ_HEADER;
      return;
    }
    auto serverState = getEd2kServerState(attrs, endpoint_);
    const bool canRequestCallback =
        serverState && serverState->handshakeCompleted && serverState->highId;
    for (const auto& source : sources) {
      if (source.lowId) {
        if (canRequestCallback) {
          addEd2kFoundSource(attrs, source, ed2k::PEER_SOURCE_SERVER, true);
          queueCallbackRequest(sourceGroup, source.clientId);
        }
        else {
          addEd2kFoundSource(attrs, source, ed2k::PEER_SOURCE_SERVER, false);
        }
      }
    }
    mergeEd2kServerSources(attrs, sources, ed2k::PEER_SOURCE_SERVER);
    A2_LOG_DEBUG(fmt("CUID#%" PRId64
                    " - ED2K server %s:%u returned %lu source(s).",
                    getCuid(), endpoint_.host.c_str(), endpoint_.port,
                    static_cast<unsigned long>(sources.size())));
    updateEd2kServerSourceResponse(attrs, endpoint_, sources.size(),
                                   nowSeconds());
    schedulePendingEd2kPeers(sourceGroup, getDownloadEngine());
    markEd2kServerSourceRequestFinished(attrs, endpoint_);
    state_ = outbox_.empty() ? State::READ_HEADER : State::WRITE;
    return;
  }
  if (currentHeader_.opcode == ed2k::OP_CALLBACKREQUESTED) {
    ed2k::Endpoint peer;
    if (!ed2k::parseCallbackRequestIncomingPayload(peer, body_)) {
      throw DL_RETRY_EX("Bad ED2K callback request.");
    }
    RequestGroup* callbackGroup = getRequestGroup();
    if (!pendingCallbacks_.empty()) {
      callbackGroup = pendingCallbacks_.front().group;
      attrs = getEd2kAttrs(callbackGroup->getDownloadContext());
    }
    addEd2kPeer(attrs, peer, ed2k::PEER_SOURCE_SERVER);
    if (!pendingCallbacks_.empty()) {
      markEd2kCallbackAccepted(attrs, pendingCallbacks_.front().clientId, peer,
                               nowSeconds());
      pendingCallbacks_.pop_front();
    }
    schedulePendingEd2kPeers(callbackGroup, getDownloadEngine());
    state_ = State::READ_HEADER;
    return;
  }
  if (currentHeader_.opcode == ed2k::OP_CALLBACK_FAIL) {
    if (body_.size() >= 4) {
      const auto clientId = ed2k::readUInt32(body_.data());
      auto i = std::find_if(
          pendingCallbacks_.begin(), pendingCallbacks_.end(),
          [&](const PendingCallback& callback) {
            return callback.clientId == clientId;
          });
      if (i != pendingCallbacks_.end()) {
        markEd2kCallbackFailed(
            getEd2kAttrs(i->group->getDownloadContext()), clientId);
        pendingCallbacks_.erase(i);
      }
    }
    A2_LOG_DEBUG(fmt("CUID#%" PRId64
                    " - ED2K server %s:%u reported callback failure.",
                    getCuid(), endpoint_.host.c_str(), endpoint_.port));
    state_ = State::READ_HEADER;
    return;
  }
  if (currentHeader_.opcode == ed2k::OP_REJECT) {
    A2_LOG_DEBUG(fmt("CUID#%" PRId64
                    " - ED2K server %s:%u rejected the last command.",
                    getCuid(), endpoint_.host.c_str(), endpoint_.port));
    state_ = State::READ_HEADER;
    return;
  }
  if (currentHeader_.opcode == ed2k::OP_SEARCHRESULT) {
    ed2k::SearchResult result;
    if (!ed2k::parseSearchResultPayload(result, body_, "server")) {
      A2_LOG_DEBUG(fmt("CUID#%" PRId64
                      " - ED2K server %s:%u returned an unusable search result.",
                      getCuid(), endpoint_.host.c_str(), endpoint_.port));
      state_ = State::READ_HEADER;
      return;
    }
    addEd2kSearchResults(getEd2kAttrs(getDownloadContext()), result.entries,
                         result.moreResults);
    state_ = State::READ_HEADER;
  }
  if (currentHeader_.opcode == ed2k::OP_SERVERSTATUS) {
    ed2k::ServerStatus status;
    if (!ed2k::parseServerStatusPayload(status, body_)) {
      throw DL_RETRY_EX("Bad ED2K server status.");
    }
    updateEd2kServerStatus(attrs, endpoint_, status);
  }
  if (currentHeader_.opcode == ed2k::OP_SERVERMESSAGE) {
    std::string message;
    if (!ed2k::parseServerMessagePayload(message, body_)) {
      throw DL_RETRY_EX("Bad ED2K server message.");
    }
    A2_LOG_DEBUG(fmt("CUID#%" PRId64 " - ED2K server %s:%u message: %s",
                    getCuid(), endpoint_.host.c_str(), endpoint_.port,
                    message.c_str()));
    updateEd2kServerMessage(attrs, endpoint_, message);
  }
  if (currentHeader_.opcode == ed2k::OP_SERVERIDENT) {
    ed2k::ServerIdent ident;
    if (!ed2k::parseServerIdentPayload(ident, body_)) {
      throw DL_RETRY_EX("Bad ED2K server ident.");
    }
    updateEd2kServerIdent(attrs, endpoint_, ident);
  }
  if (currentHeader_.opcode == ed2k::OP_SERVERLIST) {
    std::vector<ed2k::Endpoint> servers;
    if (!ed2k::parseServerListPayload(servers, body_)) {
      throw DL_RETRY_EX("Bad ED2K server list.");
    }
    for (const auto& server : servers) {
      auto before = attrs->servers.size();
      auto i = std::find_if(attrs->servers.begin(), attrs->servers.end(),
                            [&](const ed2k::Endpoint& item) {
                              return item.host == server.host &&
                                     item.port == server.port;
                            });
      if (i == attrs->servers.end()) {
        attrs->servers.push_back(server);
      }
      if (attrs->servers.size() != before) {
        getEd2kServerState(attrs, server);
      }
    }
  }
}

void Ed2kCommand::handlePeerPacket()
{
  routeIncomingFileRequest();
  auto attrs = getEd2kAttrs(getDownloadContext());
  if (currentHeader_.protocol == ed2k::PROTO_EMULE &&
      currentHeader_.opcode != ed2k::OP_COMPRESSEDPART &&
      currentHeader_.opcode != ed2k::OP_COMPRESSEDPART_I64) {
    switch (currentHeader_.opcode) {
    case ed2k::OP_EMULEINFO:
      if (ed2k::parseEmuleInfoPayload(remotePeerInfo_, body_)) {
        updatePeerUdpMetadata(attrs, endpoint_, remotePeerInfo_);
        queueEmuleInfo(true);
        state_ = State::WRITE;
      }
      break;
    case ed2k::OP_EMULEINFOANSWER:
      if (!ed2k::parseEmuleInfoPayload(remotePeerInfo_, body_)) {
        throw DL_RETRY_EX("Bad eMule info answer.");
      }
      updatePeerUdpMetadata(attrs, endpoint_, remotePeerInfo_);
      if (!outbox_.empty()) {
        state_ = State::WRITE;
      }
      break;
    case ed2k::OP_ANSWERSOURCES2: {
      ed2k::SourceExchangeAnswer answer;
      if (!ed2k::parseAnswerSources2Payload(answer, body_, attrs->link.hash)) {
        throw DL_RETRY_EX("Bad ED2K source exchange answer.");
      }
      mergeEd2kSourceExchangePeers(attrs, answer.entries, endpoint_);
      schedulePendingPeers();
      break;
    }
    case ed2k::OP_ANSWERSOURCES: {
      ed2k::SourceExchangeAnswer answer;
      const auto version = remotePeerInfo_.miscOptions.sourceExchange1Version;
      if (!ed2k::parseAnswerSourcesPayload(answer, body_, attrs->link.hash,
                                           version)) {
        throw DL_RETRY_EX("Bad ED2K source exchange answer.");
      }
      mergeEd2kSourceExchangePeers(attrs, answer.entries, endpoint_);
      schedulePendingPeers();
      break;
    }
    case ed2k::OP_REQUESTSOURCES:
      if (body_ == attrs->link.hash) {
        queueSourceExchangeAnswer(
            std::max<uint8_t>(1, remotePeerInfo_.miscOptions.sourceExchange1Version));
        if (!outbox_.empty()) {
          state_ = State::WRITE;
        }
      }
      else if (createSharedResponder().queueSourceExchangeAnswer(
            body_,
            std::max<uint8_t>(1, remotePeerInfo_.miscOptions.sourceExchange1Version))) {
        if (!outbox_.empty()) {
          state_ = State::WRITE;
        }
      }
      break;
    case ed2k::OP_REQUESTSOURCES2: {
      uint8_t version = 0;
      if (ed2k::parseRequestSources2Payload(version, body_,
                                            attrs->link.hash)) {
        queueSourceExchangeAnswer(version);
        if (!outbox_.empty()) {
          state_ = State::WRITE;
        }
      }
      else if (body_.size() >= ed2k::HASH_LENGTH + 3 &&
               ed2k::parseRequestSources2Payload(
                   version, body_, body_.substr(3, ed2k::HASH_LENGTH)) &&
               createSharedResponder().queueSourceExchangeAnswer(
                   body_.substr(3, ed2k::HASH_LENGTH), version)) {
        if (!outbox_.empty()) {
          state_ = State::WRITE;
        }
      }
      else {
        throw DL_RETRY_EX("Bad ED2K source exchange request.");
      }
      break;
    }
    case ed2k::OP_AICHFILEHASHREQ:
      if (body_.size() != ed2k::HASH_LENGTH ||
          !createSharedResponder().queueAichFileHashAnswer(body_)) {
        break;
      }
      state_ = State::WRITE;
      break;
    case ed2k::OP_AICHFILEHASHANS: {
      ed2k::AichFileHashAnswer answer;
      if (!ed2k::parseAichFileHashAnswerPayload(answer, body_,
                                                attrs->link.hash)) {
        throw DL_RETRY_EX("Bad ED2K AICH file hash answer.");
      }
      recordEd2kAichHashVote(attrs, answer.rootHash, endpoint_.host);
      break;
    }
    case ed2k::OP_MULTIPACKETANSWER: {
      ed2k::MultipacketAnswer answer;
      if (!ed2k::parseMultipacketAnswerPayload(answer, body_,
                                               attrs->link.hash)) {
        throw DL_RETRY_EX("Bad ED2K multipacket answer.");
      }
      if (answer.hasFileStatus) {
        if (answer.completeSource) {
          answer.partStatus.assign(getDownloadContext()->getNumPieces(), true);
        }
        updateEd2kPeerPartStatus(attrs, endpoint_, answer.partStatus);
        peerFileStatusReceived_ = true;
      }
      if (answer.hasAichRootHash) {
        recordEd2kAichHashVote(attrs, answer.aichRootHash, endpoint_.host);
      }
      if (answer.hasFileStatus) {
        queuePeerPostFileStatusRequests();
        state_ = State::WRITE;
      }
      else if (answer.hasFileName &&
               getDownloadContext()->getTotalLength() <= ed2k::PIECE_LENGTH) {
        queuePeerPostFileStatusRequests();
        state_ = State::WRITE;
      }
      break;
    }
    case ed2k::OP_MULTIPACKET:
    case ed2k::OP_MULTIPACKET_EXT:
      if (createSharedResponder().queueMultipacketAnswer(
              body_, currentHeader_.opcode == ed2k::OP_MULTIPACKET_EXT,
              remotePeerInfo_.miscOptions.extendedRequestsVersion,
              remotePeerInfo_.miscOptions.sourceExchange1Version)) {
        state_ = State::WRITE;
      }
      break;
    case ed2k::OP_AICHANSWER: {
      ed2k::AichAnswer answer;
      if (!ed2k::parseAichAnswerPayload(answer, body_, attrs->link.hash)) {
        throw DL_RETRY_EX("Bad ED2K AICH answer.");
      }
      if (!answer.failed) {
        if (!attrs->aichRootTrusted) {
          throw DL_RETRY_EX("Untrusted ED2K AICH recovery root.");
        }
        if (answer.rootHash != attrs->aichRootHash) {
          throw DL_RETRY_EX("Bad ED2K AICH recovery root.");
        }
        const auto partSize =
            std::min<int64_t>(ed2k::PIECE_LENGTH,
                              getDownloadContext()->getTotalLength() -
                                  static_cast<int64_t>(answer.partIndex) *
                                      ed2k::PIECE_LENGTH);
        ed2k::AichRecoveryData recovery;
        ed2k::AichRecoverySet recoverySet;
        if (partSize <= 0 ||
            !ed2k::parseAichRecoveryData(
                recovery, answer.recoveryData, static_cast<size_t>(partSize),
                use64BitOffsets_) ||
            !ed2k::buildAichRecoverySet(
                recoverySet, recovery, attrs->aichRootHash,
                static_cast<size_t>(getDownloadContext()->getTotalLength()),
                answer.partIndex)) {
          throw DL_RETRY_EX("Bad ED2K AICH recovery data.");
        }
        storeAichRecoverySet(attrs, recoverySet);
      }
      break;
    }
    case ed2k::OP_CALLBACK: {
      ed2k::BuddyCallback callback;
      if (!ed2k::parseBuddyCallbackPayload(callback, body_) ||
          callback.buddyId != ed2k::ed2kHashToKadId(attrs->clientHash) ||
          callback.fileId != ed2k::ed2kHashToKadId(attrs->link.hash)) {
        break;
      }
      addEd2kPeer(attrs, callback.endpoint, ed2k::PEER_SOURCE_KAD);
      auto e = getDownloadEngine();
      e->addCommand(make_unique<Ed2kCommand>(e->newCUID(), getRequestGroup(),
                                             e, callback.endpoint, false));
      A2_LOG_TRACE(fmt("Accepted ED2K buddy callback for %s:%u.",
                       callback.endpoint.host.c_str(),
                       callback.endpoint.port));
      break;
    }
    case ed2k::OP_AICHREQUEST:
      if (body_.size() >= ed2k::HASH_LENGTH) {
        createSharedResponder().queueAichAnswer(
            body_.substr(0, ed2k::HASH_LENGTH), body_);
        if (!outbox_.empty()) {
          state_ = State::WRITE;
        }
      }
      break;
    default:
      break;
    }
    return;
  }
  switch (currentHeader_.opcode) {
  case ed2k::OP_KAD_FWTCPCHECK_ACK: {
    auto session =
        getDownloadEngine()->getRequestGroupMan()->getEd2kSession();
    bool expected = false;
    for (auto group : session->downloads()) {
      auto groupAttrs = getEd2kAttrs(group->getDownloadContext());
      if (groupAttrs && consumeEd2kKadFirewallCheckHost(groupAttrs,
                                                        endpoint_.host)) {
        expected = true;
      }
    }
    if (!expected) {
      throw DL_RETRY_EX("Unsolicited ED2K Kad firewall-check acknowledgement.");
    }
    for (auto group : session->downloads()) {
      auto groupAttrs = getEd2kAttrs(group->getDownloadContext());
      if (groupAttrs) {
        groupAttrs->kadFirewalled = false;
      }
    }
    state_ = State::DONE;
    break;
  }
  case ed2k::OP_HELLO:
    if (!updatePeerEndpointFromHello(true)) {
      break;
    }
    if (!ed2k::parsePeerHelloPayload(remotePeerInfo_, body_, true)) {
      throw DL_RETRY_EX("Bad ED2K peer hello.");
    }
    updatePeerUdpMetadata(attrs, endpoint_, remotePeerInfo_);
    queuePeerHelloAnswer();
    if (firewallCheck_) {
      queuePacket(ed2k::KAD_PROTOCOL, ed2k::OP_KAD_FWTCPCHECK_ACK,
                  std::string());
      closeAfterOutbox_ = true;
    }
    else {
      queuePeerFileRequest();
    }
    state_ = State::WRITE;
    break;
  case ed2k::OP_HELLOANSWER:
    if (!updatePeerEndpointFromHello(false)) {
      break;
    }
    if (!ed2k::parsePeerHelloPayload(remotePeerInfo_, body_, false)) {
      throw DL_RETRY_EX("Bad ED2K peer hello answer.");
    }
    updatePeerUdpMetadata(attrs, endpoint_, remotePeerInfo_);
    if (firewallCheck_) {
      queuePacket(ed2k::KAD_PROTOCOL, ed2k::OP_KAD_FWTCPCHECK_ACK,
                  std::string());
      closeAfterOutbox_ = true;
    }
    else {
      queuePeerFileRequest();
    }
    state_ = State::WRITE;
    break;
  case ed2k::OP_REQFILENAMEANSWER:
    if (body_.size() < ed2k::HASH_LENGTH ||
        body_.substr(0, ed2k::HASH_LENGTH) != attrs->link.hash) {
      throw DL_RETRY_EX("ED2K file answer hash mismatch.");
    }
    if (!peerFileStatusRequested_ && !peerFileStatusReceived_ &&
        getDownloadContext()->getTotalLength() > ed2k::PIECE_LENGTH) {
      queuePeerFileStatusRequest();
      state_ = State::WRITE;
    }
    else if (ed2k::hashSetPartCount(getDownloadContext()->getTotalLength()) >
                 0 &&
             attrs->pieceHashes.empty()) {
      queuePeerHashSetRequest();
      state_ = State::WRITE;
    }
    else {
      queueSourceExchangeRequest();
      queuePeerStartUpload();
      state_ = State::WRITE;
    }
    break;
  case ed2k::OP_REQUESTFILENAME:
    if (body_.size() < ed2k::HASH_LENGTH) {
      throw DL_RETRY_EX("Bad ED2K file request.");
    }
    createSharedResponder().queueFileNameAnswer(
        body_.substr(0, ed2k::HASH_LENGTH));
    state_ = State::WRITE;
    break;
  case ed2k::OP_SETREQFILEID:
    if (body_.size() != ed2k::HASH_LENGTH) {
      throw DL_RETRY_EX("Bad ED2K file status request.");
    }
    createSharedResponder().queueFileStatusAnswer(body_);
    state_ = State::WRITE;
    break;
  case ed2k::OP_FILESTATUS: {
    std::vector<bool> bitfield;
    if (!ed2k::parseFileStatusPayload(bitfield, body_, attrs->link.hash,
                                      getDownloadContext()->getNumPieces())) {
      throw DL_RETRY_EX("ED2K file status hash mismatch.");
    }
    updateEd2kPeerPartStatus(attrs, endpoint_, bitfield);
    peerFileStatusReceived_ = true;
    queuePeerPostFileStatusRequests();
    state_ = State::WRITE;
    break;
  }
  case ed2k::OP_HASHSETANSWER: {
    std::vector<std::string> pieceHashes;
    if (!ed2k::parseHashSetAnswerPayload(pieceHashes, body_,
                                         attrs->link.hash) ||
        pieceHashes.size() !=
            ed2k::hashSetPartCount(getDownloadContext()->getTotalLength()) ||
        ed2k::rootHash(pieceHashes) != attrs->link.hash) {
      throw DOWNLOAD_FAILURE_EXCEPTION2("Bad ED2K hash set.",
                                        error_code::CHECKSUM_ERROR);
    }
    attrs->pieceHashes = std::move(pieceHashes);
    queuePeerStartUpload();
    state_ = State::WRITE;
    break;
  }
  case ed2k::OP_HASHSETREQUEST:
    if (body_.size() != ed2k::HASH_LENGTH) {
      throw DL_RETRY_EX("Bad ED2K hash set request.");
    }
    createSharedResponder().queueHashSetAnswer(body_);
    state_ = State::WRITE;
    break;
  case ed2k::OP_STARTUPLOADREQ:
    if (body_.size() != ed2k::HASH_LENGTH) {
      throw DL_RETRY_EX("Bad ED2K upload request.");
    }
    createSharedResponder().requestUploadSlot(
        body_, std::chrono::duration_cast<std::chrono::seconds>(
                   global::wallclock().getTime().time_since_epoch())
                   .count());
    state_ = State::WRITE;
    break;
  case ed2k::OP_ACCEPTUPLOADREQ:
    peerAccepted_ = true;
    markEd2kPeerAccepted(attrs, endpoint_);
    A2_LOG_DEBUG(fmt("component=ed2k event=peer_download_ready gid=%s "
                     "cuid=%" PRId64 " endpoint=%s:%u",
                     GroupId::toHex(getRequestGroup()->getGID()).c_str(),
                     getCuid(), logging::sanitizeText(endpoint_.host).c_str(),
                     endpoint_.port));
    queuePeerPartRequest();
    state_ = State::WRITE;
    break;
  case ed2k::OP_OUTOFPARTREQS:
    markEd2kPeerOutOfParts(attrs, endpoint_, nowSeconds());
    if (switchToAlternativeDownload()) {
      break;
    }
    schedulePendingPeers();
    state_ = State::DONE;
    break;
  case ed2k::OP_FILEREQANSNOFIL:
    markEd2kPeerDead(
        attrs, endpoint_,
        std::chrono::duration_cast<std::chrono::seconds>(
            global::wallclock().getTime().time_since_epoch())
            .count(),
        std::max<int64_t>(1, getOption()->getAsInt(PREF_RETRY_WAIT)));
    if (switchToAlternativeDownload()) {
      break;
    }
    schedulePendingPeers();
    state_ = State::DONE;
    break;
  case ed2k::OP_CANCELTRANSFER:
    markEd2kPeerCancelled(attrs, endpoint_);
    schedulePendingPeers();
    state_ = State::DONE;
    break;
  case ed2k::OP_QUEUERANK:
  case ed2k::OP_QUEUERANKING: {
    uint16_t rank = 0;
    if (!ed2k::parseQueueRankPayload(rank, body_)) {
      throw DL_RETRY_EX("Bad ED2K queue rank.");
    }
    auto peerState = getEd2kPeerState(attrs, endpoint_);
    const std::vector<bool> partStatus =
        peerState ? peerState->partStatus : std::vector<bool>();
    markEd2kPeerQueued(attrs, endpoint_, rank, partStatus);
    schedulePendingPeers();
    state_ = State::DONE;
    break;
  }
  case ed2k::OP_SENDINGPART:
  case ed2k::OP_SENDINGPART_I64: {
    const bool is64 = currentHeader_.opcode == ed2k::OP_SENDINGPART_I64;
    const size_t metaLength = is64 ? 32 : 24;
    if (body_.size() < metaLength) {
      throw DL_RETRY_EX("Truncated ED2K part packet.");
    }
    auto hash = body_.substr(0, ed2k::HASH_LENGTH);
    if (hash != attrs->link.hash) {
      throw DL_RETRY_EX("ED2K part hash mismatch.");
    }
    const int64_t begin =
        is64 ? ed2k::readUInt64(body_.data() + 16)
             : ed2k::readUInt32(body_.data() + 16);
    const int64_t end =
        is64 ? ed2k::readUInt64(body_.data() + 24)
             : ed2k::readUInt32(body_.data() + 20);
    if (begin < 0 || end <= begin ||
        static_cast<size_t>(end - begin) != body_.size() - metaLength) {
      throw DL_RETRY_EX("Bad ED2K part range.");
    }
    const auto data = body_.substr(metaLength);
    if (!remotePeerInfo_.userHash.empty()) {
      auto rgman = getDownloadEngine()->getRequestGroupMan().get();
      if (rgman && rgman->getEd2kUploadQueue()) {
        rgman->getEd2kUploadQueue()->noteDownloaded(remotePeerInfo_.userHash,
                                                    data.size());
      }
    }
    handlePartData(begin, data);
    if (getRequestGroup()->downloadFinished()) {
      state_ = State::DONE;
    }
    else {
      queuePeerPartRequest();
      state_ = State::WRITE;
    }
    break;
  }
  case ed2k::OP_REQUESTPARTS:
    createSharedResponder().queuePartAnswers(body_, false);
    if (!outbox_.empty()) {
      state_ = State::WRITE;
    }
    break;
  case ed2k::OP_REQUESTPARTS_I64:
    createSharedResponder().queuePartAnswers(body_, true);
    if (!outbox_.empty()) {
      state_ = State::WRITE;
    }
    break;
  case ed2k::OP_COMPRESSEDPART:
  case ed2k::OP_COMPRESSEDPART_I64: {
    const bool is64 = currentHeader_.opcode == ed2k::OP_COMPRESSEDPART_I64;
    ed2k::CompressedPartHeader header;
    std::string compressedData;
    if (!ed2k::parseCompressedPartPayload(header, compressedData, body_,
                                          attrs->link.hash, is64)) {
      throw DL_RETRY_EX("Bad compressed ED2K part packet.");
    }
    const auto peerState = getEd2kPeerState(attrs, endpoint_);
    auto compressedState = findCompressedPartState(header.begin);
    if (!compressedState && peerState) {
      auto requested = std::find_if(
          peerState->requestedParts.begin(), peerState->requestedParts.end(),
          [&](const ed2k::PartRange& range) {
            return range.begin <= header.begin && header.begin < range.end;
          });
      if (requested != peerState->requestedParts.end()) {
        compressedState = getOrCreateCompressedPartState(*requested);
      }
    }
    if (!compressedState) {
      throw DL_RETRY_EX("Unexpected compressed ED2K part range.");
    }
    const auto writeBegin =
        compressedState->block.begin +
        compressedState->inflater.inflatedLength();
    if (writeBegin < compressedState->block.begin ||
        writeBegin >= compressedState->block.end) {
      throw DL_RETRY_EX("Unexpected compressed ED2K part range.");
    }
    const auto maxOutput = static_cast<size_t>(
        std::min<int64_t>(ed2k::BLOCK_LENGTH,
                          compressedState->block.end - writeBegin));
    std::string data;
    if (!compressedState->inflater.inflateChunk(
            data, compressedData, compressedState->block.begin, maxOutput)) {
      throw DL_RETRY_EX("Bad compressed ED2K part data.");
    }
    if (!data.empty()) {
      handlePartData(writeBegin, data);
    }
    if (!compressedState->inflater.active()) {
      releaseCompletedCompressedPartState(compressedState->block);
    }
    if (getRequestGroup()->downloadFinished()) {
      state_ = State::DONE;
    }
    else {
      queuePeerPartRequest();
      state_ = State::WRITE;
    }
    break;
  }
  default:
    break;
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

} // namespace aria2
