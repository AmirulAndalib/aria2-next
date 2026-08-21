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
#include "ed2k_crypto.h"

#include <array>

#include "ARC4Encryptor.h"
#include "MessageDigest.h"
#include "ed2k_constants.h"
#include "ed2k_helper.h"
#include "ed2k_link.h"
#include "message_digest_helper.h"

namespace aria2 {

namespace ed2k {

namespace {

constexpr uint8_t UDP_CLIENT_TO_SERVER_MAGIC = 0x6b;
constexpr uint8_t UDP_SERVER_TO_CLIENT_MAGIC = 0xa5;
constexpr uint8_t UDP_PEER_MAGIC = 91;
constexpr uint32_t UDP_SYNC_MAGIC = 0x13ef24d5;
constexpr uint32_t UDP_PEER_SYNC_MAGIC = 0x395f2ec1;
constexpr size_t UDP_HEADER_LENGTH = 8;

std::string md5Digest(const std::string& data)
{
  std::array<unsigned char, 16> digest;
  auto md5 = MessageDigest::create("md5");
  message_digest::digest(digest.data(), digest.size(), md5.get(), data.data(),
                         data.size());
  return std::string(reinterpret_cast<const char*>(digest.data()), digest.size());
}

void initializeUdpCipher(ARC4Encryptor& cipher, const std::string& keyData)
{
  const auto key = md5Digest(keyData);
  cipher.init(reinterpret_cast<const unsigned char*>(key.data()), key.size());
  std::array<unsigned char, 1024> discarded{};
  cipher.encrypt(discarded.size(), discarded.data(), discarded.data());
}

std::array<unsigned char, 4> udpSyncBytes()
{
  return {{static_cast<unsigned char>(UDP_SYNC_MAGIC >> 24),
           static_cast<unsigned char>(UDP_SYNC_MAGIC >> 16),
           static_cast<unsigned char>(UDP_SYNC_MAGIC >> 8),
           static_cast<unsigned char>(UDP_SYNC_MAGIC)}};
}

std::array<unsigned char, 4> peerUdpSyncBytes()
{
  return {{static_cast<unsigned char>(UDP_PEER_SYNC_MAGIC >> 24),
           static_cast<unsigned char>(UDP_PEER_SYNC_MAGIC >> 16),
           static_cast<unsigned char>(UDP_PEER_SYNC_MAGIC >> 8),
           static_cast<unsigned char>(UDP_PEER_SYNC_MAGIC)}};
}

bool isPlainUdpMarker(uint8_t marker)
{
  return marker == PROTO_EMULE || marker == KAD_PACKED_PROTOCOL ||
         marker == KAD_PROTOCOL || marker == PROTO_PACKED;
}

} // namespace

std::string createTcpObfuscationKey(const std::string& userHash,
                                    uint8_t magicValue,
                                    uint32_t randomKeyPart)
{
  if (userHash.size() != HASH_LENGTH) {
    return std::string();
  }

  std::string keyData = userHash;
  keyData.push_back(static_cast<char>(magicValue));
  keyData += packUInt32(randomKeyPart);

  std::array<unsigned char, 16> digest;
  auto md5 = MessageDigest::create("md5");
  message_digest::digest(digest.data(), digest.size(), md5.get(),
                         keyData.data(), keyData.size());
  return std::string(reinterpret_cast<const char*>(digest.data()),
                     digest.size());
}

std::string createServerTcpObfuscationKey(const std::string& sharedSecret,
                                          uint8_t magicValue)
{
  if (sharedSecret.size() != 96) {
    return std::string();
  }
  std::string keyData = sharedSecret;
  keyData.push_back(static_cast<char>(magicValue));

  std::array<unsigned char, 16> digest;
  auto md5 = MessageDigest::create("md5");
  message_digest::digest(digest.data(), digest.size(), md5.get(),
                         keyData.data(), keyData.size());
  return std::string(reinterpret_cast<const char*>(digest.data()),
                     digest.size());
}

std::string encryptServerUdpDatagram(const std::string& datagram,
                                     uint32_t baseKey,
                                     uint16_t randomKeyPart)
{
  if (datagram.empty() || baseKey == 0) {
    return std::string();
  }
  std::string keyData = packUInt32(baseKey);
  keyData.push_back(static_cast<char>(UDP_CLIENT_TO_SERVER_MAGIC));
  keyData += packUInt16(randomKeyPart);
  ARC4Encryptor cipher;
  initializeUdpCipher(cipher, keyData);

  auto marker = static_cast<uint8_t>(randomKeyPart >> 8);
  if (marker == PROTO_EDONKEY) {
    marker ^= 0x01;
  }
  std::string encrypted(UDP_HEADER_LENGTH + datagram.size(), '\0');
  encrypted[0] = static_cast<char>(marker);
  encrypted[1] = static_cast<char>(randomKeyPart);
  encrypted[2] = static_cast<char>(randomKeyPart >> 8);

  const auto sync = udpSyncBytes();
  cipher.encrypt(sync.size(),
                 reinterpret_cast<unsigned char*>(&encrypted[3]), sync.data());
  const unsigned char paddingLength = 0;
  cipher.encrypt(1, reinterpret_cast<unsigned char*>(&encrypted[7]),
                 &paddingLength);
  cipher.encrypt(datagram.size(),
                 reinterpret_cast<unsigned char*>(&encrypted[UDP_HEADER_LENGTH]),
                 reinterpret_cast<const unsigned char*>(datagram.data()));
  return encrypted;
}

bool decryptServerUdpDatagram(std::string& datagram,
                              const std::string& encrypted, uint32_t baseKey)
{
  if (encrypted.size() <= UDP_HEADER_LENGTH || baseKey == 0 ||
      static_cast<uint8_t>(encrypted[0]) == PROTO_EDONKEY) {
    return false;
  }
  std::string keyData = packUInt32(baseKey);
  keyData.push_back(static_cast<char>(UDP_SERVER_TO_CLIENT_MAGIC));
  keyData.append(encrypted, 1, 2);
  ARC4Encryptor cipher;
  initializeUdpCipher(cipher, keyData);

  std::array<unsigned char, 4> sync{};
  cipher.encrypt(sync.size(), sync.data(),
                 reinterpret_cast<const unsigned char*>(encrypted.data() + 3));
  if (sync != udpSyncBytes()) {
    return false;
  }
  unsigned char paddingLength = 0;
  cipher.encrypt(1, &paddingLength,
                 reinterpret_cast<const unsigned char*>(encrypted.data() + 7));
  paddingLength &= 0x0f;
  if (encrypted.size() <= UDP_HEADER_LENGTH + paddingLength) {
    return false;
  }
  if (paddingLength != 0) {
    std::array<unsigned char, 15> padding{};
    cipher.encrypt(paddingLength, padding.data(),
                   reinterpret_cast<const unsigned char*>(
                       encrypted.data() + UDP_HEADER_LENGTH));
  }
  const auto offset = UDP_HEADER_LENGTH + paddingLength;
  datagram.resize(encrypted.size() - offset);
  cipher.encrypt(datagram.size(), reinterpret_cast<unsigned char*>(&datagram[0]),
                 reinterpret_cast<const unsigned char*>(encrypted.data() + offset));
  return true;
}

std::string encryptPeerUdpDatagram(const std::string& datagram,
                                   const std::string& remoteUserHash,
                                   uint32_t localPublicIp,
                                   uint16_t randomKeyPart)
{
  if (datagram.empty() || remoteUserHash.size() != HASH_LENGTH ||
      localPublicIp == 0) {
    return std::string();
  }
  std::string keyData = remoteUserHash + packUInt32(localPublicIp);
  keyData.push_back(static_cast<char>(UDP_PEER_MAGIC));
  keyData += packUInt16(randomKeyPart);
  ARC4Encryptor cipher;
  initializeUdpCipher(cipher, keyData);

  auto marker = static_cast<uint8_t>((randomKeyPart >> 8) | 0x01);
  if (isPlainUdpMarker(marker)) {
    marker ^= 0x04;
  }
  std::string encrypted(UDP_HEADER_LENGTH + datagram.size(), '\0');
  encrypted[0] = static_cast<char>(marker);
  encrypted[1] = static_cast<char>(randomKeyPart);
  encrypted[2] = static_cast<char>(randomKeyPart >> 8);
  const auto sync = peerUdpSyncBytes();
  cipher.encrypt(sync.size(),
                 reinterpret_cast<unsigned char*>(&encrypted[3]), sync.data());
  const unsigned char paddingLength = 0;
  cipher.encrypt(1, reinterpret_cast<unsigned char*>(&encrypted[7]),
                 &paddingLength);
  cipher.encrypt(datagram.size(),
                 reinterpret_cast<unsigned char*>(&encrypted[UDP_HEADER_LENGTH]),
                 reinterpret_cast<const unsigned char*>(datagram.data()));
  return encrypted;
}

bool decryptPeerUdpDatagram(std::string& datagram,
                            const std::string& encrypted,
                            const std::string& localUserHash,
                            uint32_t remotePublicIp)
{
  if (encrypted.size() <= UDP_HEADER_LENGTH ||
      localUserHash.size() != HASH_LENGTH || remotePublicIp == 0 ||
      isPlainUdpMarker(static_cast<uint8_t>(encrypted[0]))) {
    return false;
  }
  std::string keyData = localUserHash + packUInt32(remotePublicIp);
  keyData.push_back(static_cast<char>(UDP_PEER_MAGIC));
  keyData.append(encrypted, 1, 2);
  ARC4Encryptor cipher;
  initializeUdpCipher(cipher, keyData);

  std::array<unsigned char, 4> sync{};
  cipher.encrypt(sync.size(), sync.data(),
                 reinterpret_cast<const unsigned char*>(encrypted.data() + 3));
  if (sync != peerUdpSyncBytes()) {
    return false;
  }
  unsigned char paddingLength = 0;
  cipher.encrypt(1, &paddingLength,
                 reinterpret_cast<const unsigned char*>(encrypted.data() + 7));
  paddingLength &= 0x0f;
  if (encrypted.size() <= UDP_HEADER_LENGTH + paddingLength) {
    return false;
  }
  if (paddingLength != 0) {
    std::array<unsigned char, 15> padding{};
    cipher.encrypt(paddingLength, padding.data(),
                   reinterpret_cast<const unsigned char*>(
                       encrypted.data() + UDP_HEADER_LENGTH));
  }
  const auto offset = UDP_HEADER_LENGTH + paddingLength;
  datagram.resize(encrypted.size() - offset);
  cipher.encrypt(datagram.size(), reinterpret_cast<unsigned char*>(&datagram[0]),
                 reinterpret_cast<const unsigned char*>(encrypted.data() + offset));
  return true;
}

} // namespace ed2k

} // namespace aria2
