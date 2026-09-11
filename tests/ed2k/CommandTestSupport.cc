/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
#include "CommandTestSupport.h"
#include "ContextAttribute.h"
#include "GroupId.h"
#include "a2netcompat.h"
#include <cstddef>
#include <cstdint>
#include <utility>
#include "Ed2kCommand.h"

#include <array>
#include "a2doctest.h"
#include <memory>
#include <vector>

#include "ARC4Encryptor.h"
#include "DownloadContext.h"
#include "DownloadEngine.h"
#include "Ed2kAttribute.h"
#include "Ed2kKadCommand.h"
#include "Ed2kListenCommand.h"
#include "Ed2kSession.h"
#include "Ed2kSharingTimeSeedCriteria.h"
#include "DefaultPieceStorage.h"
#include "DiskAdaptor.h"
#include "DownloadResult.h"
#include "SeedCheckCommand.h"
#include "ShareRatioSeedCriteria.h"
#include "FileEntry.h"
#include "MessageDigest.h"
#include "Option.h"
#include "RequestGroup.h"
#include "RequestGroupMan.h"
#include "SocketCore.h"
#include "ed2k_hash.h"
#include "ed2k_kad.h"
#include "ed2k_link.h"
#include "ed2k_packet.h"
#include "message_digest_helper.h"
#include "prefs.h"
#include "a2functional.h"

namespace aria2::test::ed2k_command {
std::shared_ptr<Option> createOption()
{
  auto option = std::make_shared<Option>();
  option->put(PREF_CONNECT_TIMEOUT, "5");
  option->put(PREF_RETRY_WAIT, "1");
  option->put(PREF_ED2K_LISTEN_PORT, "0");
  option->put(PREF_ED2K_UPLOAD_SLOTS, "3");
  option->put(PREF_DIR, A2_TEST_OUT_DIR);
  option->put(PREF_OUT, "ed2k-command-test.bin");
  option->put(PREF_MAX_CONCURRENT_DOWNLOADS, "5");
  option->put(PREF_MAX_DOWNLOAD_RESULT, "5");
  option->put(PREF_MAX_OVERALL_DOWNLOAD_LIMIT, "0");
  option->put(PREF_MAX_OVERALL_UPLOAD_LIMIT, "0");
  option->put(PREF_BT_MAX_OPEN_FILES, "100");
  option->put(PREF_DETACH_SHARE_ONLY, A2_V_TRUE);
  option->put(PREF_ENABLE_RPC, A2_V_FALSE);
  return option;
}

std::vector<std::string> createPieceHashes()
{
  return std::vector<std::string>{std::string(16, '\x11'),
                                  std::string(16, '\x22')};
}

std::shared_ptr<DownloadContext> createEd2kContext()
{
  auto dctx = std::make_shared<DownloadContext>();
  const std::shared_ptr<FileEntry> entries[] = {std::make_shared<FileEntry>(
      A2_TEST_OUT_DIR "/ed2k-command-test.bin", ed2k::PIECE_LENGTH + 1, 0)};
  dctx->setFileEntries(std::begin(entries), std::end(entries));
  dctx->setPieceLength(ed2k::PIECE_LENGTH);

  auto attrs = make_unique<Ed2kAttribute>();
  attrs->link.type = ed2k::LinkType::FILE;
  attrs->link.name = "ed2k-command-test.bin";
  attrs->link.size = ed2k::PIECE_LENGTH + 1;
  attrs->link.hash = ed2k::rootHash(createPieceHashes());
  attrs->clientHash =
      normalizeEd2kClientHash(std::string(ed2k::HASH_LENGTH, '\x42'));
  dctx->setAttribute(CTX_ATTR_ED2K, std::move(attrs));
  return dctx;
}

std::shared_ptr<RequestGroup>
createRequestGroup(const std::shared_ptr<Option>& option,
                   const std::shared_ptr<DownloadContext>& dctx)
{
  auto group = std::make_shared<RequestGroup>(GroupId::create(), option);
  group->setDownloadContext(dctx);
  group->setNumConcurrentCommand(3);
  return group;
}

std::shared_ptr<SocketCore> acceptPeer(SocketCore& listenSocket,
                                       DownloadEngine& engine)
{
  for (int i = 0; i < MAX_ENGINE_TICKS && !listenSocket.isReadable(0); ++i) {
    engine.run(true);
  }
  REQUIRE(listenSocket.isReadable(0));
  auto socket = listenSocket.acceptConnection();
  socket->setNonBlockingMode();
  return socket;
}

void readFromSocket(const std::shared_ptr<SocketCore>& socket,
                    DownloadEngine& engine, char* data, size_t length)
{
  size_t readLength = 0;
  for (int i = 0; i < MAX_ENGINE_TICKS && readLength < length; ++i) {
    if (!socket->isReadable(0)) {
      engine.run(true);
      continue;
    }
    size_t len = length - readLength;
    socket->readData(data + readLength, len);
    readLength += len;
  }
  REQUIRE_EQ(length, readLength);
}

std::string readPacket(const std::shared_ptr<SocketCore>& socket,
                       DownloadEngine& engine)
{
  std::array<char, 6> header;
  readFromSocket(socket, engine, header.data(), header.size());
  ed2k::PacketHeader packetHeader;
  REQUIRE(ed2k::readPacketHeader(packetHeader, header.data(), header.size()));
  std::string body(packetHeader.payloadSize(), '\0');
  if (!body.empty()) {
    readFromSocket(socket, engine, &body[0], body.size());
  }
  return std::string(header.data(), header.size()) + body;
}

ed2k::PacketHeader packetHeaderOf(const std::string& packet)
{
  ed2k::PacketHeader header;
  REQUIRE(ed2k::readPacketHeader(header, packet.data(), 6));
  return header;
}

std::string packetBodyOf(const std::string& packet) { return packet.substr(6); }

void runEngineTicks(DownloadEngine& engine, int ticks)
{
  for (int i = 0; i < ticks; ++i) {
    engine.run(true);
  }
}

ReceivedDatagram readDatagramFrom(SocketCore& socket, DownloadEngine& engine)
{
  std::array<char, 64_k> data;
  for (int i = 0; i < MAX_ENGINE_TICKS && !socket.isReadable(0); ++i) {
    engine.run(true);
  }
  REQUIRE(socket.isReadable(0));
  Endpoint sender;
  auto length = socket.readDataFrom(data.data(), data.size(), sender);
  REQUIRE(length >= 2);
  ReceivedDatagram result;
  result.data.assign(data.data(), data.data() + length);
  result.sender = sender;
  return result;
}

std::string readDatagram(SocketCore& socket, DownloadEngine& engine)
{
  return readDatagramFrom(socket, engine).data;
}

void writeDatagram(SocketCore& socket, const std::string& datagram,
                   uint16_t port)
{
  REQUIRE_EQ(
      static_cast<ssize_t>(datagram.size()),
      socket.writeData(datagram.data(), datagram.size(), "127.0.0.1", port));
}

ed2k::PacketHeader datagramHeaderOf(const std::string& datagram)
{
  ed2k::PacketHeader header;
  REQUIRE(ed2k::readDatagramHeader(header, datagram.data(), datagram.size()));
  return header;
}

std::string datagramBodyOf(const std::string& datagram)
{
  return datagram.substr(2);
}

std::string decodeKadDatagram(const std::string& datagram,
                              const std::string& contactId)
{
  ed2k::KadObfuscatedDatagram parsed;
  if (ed2k::parseKadObfuscatedDatagram(parsed, datagram, contactId)) {
    return parsed.datagram;
  }
  return datagram;
}

std::string decodeKadDatagramWithKey(const std::string& datagram,
                                     uint32_t udpKey)
{
  ed2k::KadObfuscatedDatagram parsed;
  if (ed2k::parseKadObfuscatedDatagram(parsed, datagram, udpKey)) {
    return parsed.datagram;
  }
  return datagram;
}

ReceivedDatagram readKadDatagramWithOpcode(SocketCore& socket,
                                           DownloadEngine& engine,
                                           uint8_t opcode,
                                           const std::string& contactId)
{
  for (int i = 0; i < MAX_ENGINE_TICKS; ++i) {
    auto datagram = readDatagramFrom(socket, engine);
    datagram.data = decodeKadDatagram(datagram.data, contactId);
    if (datagramHeaderOf(datagram.data).opcode == opcode) {
      return datagram;
    }
  }
  FAIL("Expected ED2K Kad datagram opcode was not received.");
  return ReceivedDatagram();
}

std::string createAMuleTcpObfuscationKey(const std::string& userHash,
                                         uint8_t magicValue,
                                         uint32_t randomKeyPart)
{
  std::string keyData = userHash;
  keyData.push_back(static_cast<char>(magicValue));
  keyData += ed2k::packUInt32(randomKeyPart);
  std::array<unsigned char, 16> digest;
  auto md5 = MessageDigest::create("md5");
  message_digest::digest(digest.data(), digest.size(), md5.get(),
                         keyData.data(), keyData.size());
  return std::string(reinterpret_cast<const char*>(digest.data()),
                     digest.size());
}

void discardTcpObfuscationPrefix(ARC4Encryptor& rc4)
{
  std::array<unsigned char, 1_k> garbage;
  rc4.encrypt(garbage.size(), garbage.data(), garbage.data());
}

ed2k::KadContact createKadContact(const std::string& id, uint16_t udpPort)
{
  ed2k::KadContact contact;
  contact.id = id;
  contact.host = "127.0.0.1";
  contact.udpPort = udpPort;
  contact.tcpPort = 4662;
  contact.version = 8;
  return contact;
}

} // namespace aria2::test::ed2k_command
