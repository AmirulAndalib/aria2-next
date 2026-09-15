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
#include "DlRetryEx.h"
#include "Ed2kAttribute.h"
#include "Ed2kCommand.h"
#include "Log.h"
#include "a2functional.h"
#include "ed2k_crypto.h"
#include "ed2k_hash.h"
#include "ed2k_link.h"
#include "ed2k_packet.h"
#include "fmt.h"
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include "support/Random.h"
#include "ed2k/Ed2kCommandInternal.h"

namespace aria2 {
using namespace ed2k_command;

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
  if (ed2k::readUInt32(obfuscationMagicBuf_.data()) != ED2K_OBFUSCATION_SYNC) {
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
    getSocket()->readData(obfuscationMethodBuf_.data() + obfuscationMethodRead_,
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
    decryptData(obfuscationMethodBuf_.data() + obfuscationMethodRead_, len);
    obfuscationMethodRead_ += len;
  }
  if (static_cast<uint8_t>(obfuscationMethodBuf_[0]) !=
      ED2K_OBFUSCATION_METHOD) {
    throw DL_RETRY_EX("Unsupported ED2K obfuscation method.");
  }
  obfuscationPaddingBuf_.assign(static_cast<uint8_t>(obfuscationMethodBuf_[1]),
                                '\0');
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
  if (isEd2kProtocolMarker(static_cast<uint8_t>(incomingObfuscationMarker_))) {
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
  if (ed2k::readUInt32(obfuscationMagicBuf_.data()) != ED2K_OBFUSCATION_SYNC) {
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
    size_t len =
        incomingObfuscationMethodBuf_.size() - incomingObfuscationMethodRead_;
    getSocket()->readData(incomingObfuscationMethodBuf_.data() +
                              incomingObfuscationMethodRead_,
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
  obfuscationWriteBuf_.push_back(static_cast<char>(ED2K_OBFUSCATION_METHOD));
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
  obfuscationDecryptor_->encrypt(length, reinterpret_cast<unsigned char*>(data),
                                 reinterpret_cast<const unsigned char*>(data));
}

} // namespace aria2
