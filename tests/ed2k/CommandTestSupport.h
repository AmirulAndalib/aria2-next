/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
#ifndef ARIA2_TEST_ED2K_COMMAND_SUPPORT_H
#define ARIA2_TEST_ED2K_COMMAND_SUPPORT_H
#include "SocketCore.h"
#include "ed2k_packet.h"
#include "ed2k_kad.h"
#include <memory>
#include <string>
#include <vector>

namespace aria2 {
class ARC4Encryptor;
class DownloadContext;
class DownloadEngine;
class Option;
class RequestGroup;
namespace test::ed2k_command {
constexpr int MAX_ENGINE_TICKS = 200;
constexpr uint8_t ED2K_OBFUSCATION_MAGIC_REQUESTER = 34;
constexpr uint8_t ED2K_OBFUSCATION_MAGIC_SERVER = 203;
constexpr uint32_t ED2K_OBFUSCATION_SYNC = 0x835e6fc4;

struct ReceivedDatagram {
  std::string data;
  Endpoint sender;
};

std::shared_ptr<Option> createOption();
std::vector<std::string> createPieceHashes();
std::shared_ptr<DownloadContext> createEd2kContext();
std::shared_ptr<RequestGroup>
createRequestGroup(const std::shared_ptr<Option>& option,
                   const std::shared_ptr<DownloadContext>& dctx);
std::shared_ptr<SocketCore> acceptPeer(SocketCore& listener,
                                       DownloadEngine& engine);
void readFromSocket(const std::shared_ptr<SocketCore>& socket,
                    DownloadEngine& engine, char* data, size_t length);
std::string readPacket(const std::shared_ptr<SocketCore>& socket,
                       DownloadEngine& engine);
ed2k::PacketHeader packetHeaderOf(const std::string& packet);
std::string packetBodyOf(const std::string& packet);
void runEngineTicks(DownloadEngine& engine, int ticks);
ReceivedDatagram readDatagramFrom(SocketCore& socket, DownloadEngine& engine);
std::string readDatagram(SocketCore& socket, DownloadEngine& engine);
void writeDatagram(SocketCore& socket, const std::string& datagram,
                   uint16_t port);
ed2k::PacketHeader datagramHeaderOf(const std::string& datagram);
std::string datagramBodyOf(const std::string& datagram);
std::string decodeKadDatagram(const std::string& datagram,
                              const std::string& contactId);
std::string decodeKadDatagramWithKey(const std::string& datagram,
                                     uint32_t udpKey);
ReceivedDatagram readKadDatagramWithOpcode(SocketCore& socket,
                                           DownloadEngine& engine,
                                           uint8_t opcode,
                                           const std::string& contactId);
std::string createAMuleTcpObfuscationKey(const std::string& userHash,
                                         uint8_t magicValue,
                                         uint32_t randomKeyPart);
void discardTcpObfuscationPrefix(ARC4Encryptor& rc4);
ed2k::KadContact createKadContact(const std::string& id, uint16_t udpPort);
} // namespace test::ed2k_command
} // namespace aria2
#endif
