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
#include "DHKeyExchange.h"
#include "DlRetryEx.h"
#include "Ed2kAttribute.h"
#include "Ed2kCommand.h"
#include "Log.h"
#include "MSEDHKeyExchange.h"
#include "a2functional.h"
#include "ed2k_crypto.h"
#include "ed2k_packet.h"
#include "fmt.h"
#include <algorithm>
#include <array>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include "support/Random.h"
#include "ed2k/Ed2kCommandInternal.h"

namespace aria2 {
using namespace ed2k_command;

bool Ed2kCommand::shouldObfuscateServerConnection() const
{
  if (mode_ != Mode::SERVER || incoming_) {
    return false;
  }
  const auto state =
      getEd2kServerState(getEd2kAttrs(getDownloadContext()), endpoint_);
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
  serverDh_ = make_unique<DHKeyExchange>(privateKey, ed2k::SERVER_DH_PRIME_HEX);

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
  obfuscationWriteBuf_.append(reinterpret_cast<const char*>(publicKey.data()),
                              publicKey.size());
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
    getSocket()->readData(reinterpret_cast<char*>(serverDhPeerKey_.data()) +
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
  const std::string sharedSecret(reinterpret_cast<const char*>(secret.data()),
                                 secret.size());
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
  if (ed2k::readUInt32(obfuscationMagicBuf_.data()) != ED2K_OBFUSCATION_SYNC) {
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
    size_t length =
        incomingObfuscationMethodBuf_.size() - incomingObfuscationMethodRead_;
    getSocket()->readData(incomingObfuscationMethodBuf_.data() +
                              incomingObfuscationMethodRead_,
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
  obfuscationWriteBuf_.push_back(static_cast<char>(ED2K_OBFUSCATION_METHOD));
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

} // namespace aria2
