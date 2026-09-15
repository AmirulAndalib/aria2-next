/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
#include <cstdint>
#include <string>
#include <vector>
#include "ed2k_aich.h"
#include "ed2k_constants.h"
#include "ed2k_link.h"
#include "ed2k_packet.h"
#include "ed2k_server.h"
#include "ed2k_endpoint.h"

#include "a2doctest.h"
#include <cstring>

namespace aria2::ed2k {

TEST_CASE("Ed2kHelperTest.testServerPayloadParsers")
{
  ServerIdChange idChange;
  REQUIRE(parseServerIdChangePayload(idChange, packUInt32(0x04030201)));
  REQUIRE_EQ((uint32_t)0x04030201, idChange.clientId);
  REQUIRE(idChange.highId);
  REQUIRE_EQ(std::string("1.2.3.4"), idChange.ipAddress);
  REQUIRE_EQ((uint32_t)0, idChange.tcpFlags);

  REQUIRE(parseServerIdChangePayload(idChange, packUInt32(120)));
  REQUIRE_EQ((uint32_t)120, idChange.clientId);
  REQUIRE(!idChange.highId);
  REQUIRE(idChange.ipAddress.empty());
  REQUIRE(parseServerIdChangePayload(
      idChange, packUInt32(120) + packUInt32(0x55aa) + packUInt32(4661)));
  REQUIRE_EQ((uint32_t)120, idChange.clientId);
  REQUIRE_EQ((uint32_t)0x55aa, idChange.tcpFlags);
  REQUIRE_EQ((uint32_t)4661, idChange.auxPort);
  REQUIRE(parseServerIdChangePayload(
      idChange, packUInt32(0x04030201) + packUInt32(SRV_TCPFLG_TCPOBFUSCATION) +
                    packUInt32(4661) + packUInt32(0x04030201) +
                    packUInt32(4666)));
  REQUIRE_EQ((uint16_t)4666, idChange.tcpObfuscationPort);
  REQUIRE(parseServerIdChangePayload(
      idChange, packUInt32(0x04030201) + packUInt32(0x55aa) + packUInt32(4661) +
                    packUInt32(0x04030201)));
  REQUIRE_EQ((uint32_t)0x04030201, idChange.clientId);
  REQUIRE_EQ((uint32_t)0x55aa, idChange.tcpFlags);
  REQUIRE_EQ((uint32_t)4661, idChange.auxPort);
  REQUIRE_EQ((uint16_t)0, idChange.tcpObfuscationPort);

  REQUIRE(parseServerIdChangePayload(
      idChange, packUInt32(0x04030201) + packUInt32(0x55aa) + packUInt32(4661) +
                    packUInt32(0x04030201) + packUInt32(4666) +
                    packUInt32(0xdeadbeef)));
  REQUIRE_EQ((uint16_t)4666, idChange.tcpObfuscationPort);

  ServerStatus status;
  REQUIRE(
      parseServerStatusPayload(status, packUInt32(1234) + packUInt32(5678)));
  REQUIRE_EQ((uint32_t)1234, status.users);
  REQUIRE_EQ((uint32_t)5678, status.files);
  REQUIRE_EQ((uint32_t)0, status.challenge);

  REQUIRE(parseServerStatusPayload(status, packUInt32(1234) + packUInt32(5678) +
                                               packUInt32(9000)));
  REQUIRE_EQ((uint32_t)1234, status.users);
  REQUIRE_EQ((uint32_t)5678, status.files);
  REQUIRE_EQ((uint32_t)0, status.challenge);

  REQUIRE(parseServerStatusPayload(status,
                                   packUInt32(0x55aa0011) + packUInt32(1234) +
                                       packUInt32(5678) + packUInt32(9000)));
  REQUIRE_EQ((uint32_t)0x55aa0011, status.users);
  REQUIRE_EQ((uint32_t)1234, status.files);
  REQUIRE_EQ((uint32_t)0, status.challenge);

  REQUIRE(parseServerUdpStatusPayload(
      status, packUInt32(0x55aa0011) + packUInt32(1234) + packUInt32(5678) +
                  packUInt32(9000) + packUInt32(100) + packUInt32(200) +
                  packUInt32(0x01020304) + packUInt32(77) + packUInt16(4665) +
                  packUInt16(4666) + packUInt32(0x11223344)));
  REQUIRE_EQ((uint32_t)0x55aa0011, status.challenge);
  REQUIRE_EQ((uint32_t)1234, status.users);
  REQUIRE_EQ((uint32_t)5678, status.files);
  REQUIRE_EQ((uint32_t)9000, status.maxUsers);
  REQUIRE_EQ((uint32_t)100, status.softFiles);
  REQUIRE_EQ((uint32_t)200, status.hardFiles);
  REQUIRE_EQ((uint32_t)0x01020304, status.udpFlags);
  REQUIRE_EQ((uint32_t)77, status.lowIdUsers);
  REQUIRE_EQ((uint16_t)4665, status.udpObfuscationPort);
  REQUIRE_EQ((uint16_t)4666, status.tcpObfuscationPort);
  REQUIRE_EQ((uint32_t)0x11223344, status.udpKey);
  REQUIRE(parseServerUdpStatusPayload(
      status, packUInt32(0x55aa0011) + packUInt32(1234) + packUInt32(5678) +
                  packUInt32(9000) + packUInt32(100) + packUInt32(200) +
                  packUInt32(0x01020304) + packUInt32(77) + packUInt16(4665) +
                  packUInt16(4666) + packUInt32(0x11223344) + packUInt16(0)));
  REQUIRE_EQ((uint32_t)0x55aa0011, status.challenge);
  REQUIRE_EQ((uint16_t)4665, status.udpObfuscationPort);
  REQUIRE_EQ((uint16_t)4666, status.tcpObfuscationPort);
  REQUIRE_EQ((uint32_t)0x11223344, status.udpKey);

  std::string messagePayload = packUInt16(5) + "hello";
  std::string message;
  REQUIRE(parseServerMessagePayload(message, messagePayload));
  REQUIRE_EQ(std::string("hello"), message);

  std::string identPayload(16, '\x11');
  identPayload += packUInt32(0x04030201);
  identPayload += packUInt16(4661);
  identPayload += packUInt32(2);
  identPayload += createStringTag(0x01, "server name");
  identPayload += createStringTag(0x0b, "server description");
  identPayload += packUInt16(0);
  ServerIdent ident;
  REQUIRE(parseServerIdentPayload(ident, identPayload));
  REQUIRE_EQ(std::string("1.2.3.4"), ident.endpoint.host);
  REQUIRE_EQ((uint16_t)4661, ident.endpoint.port);
  REQUIRE_EQ(std::string("server name"), ident.name);
  REQUIRE_EQ(std::string("server description"), ident.description);

  std::vector<Endpoint> servers;
  std::string serverList;
  serverList.push_back('\x02');
  serverList += packUInt32(0x04030201);
  serverList += packUInt16(4661);
  serverList += packUInt32(0x08070605);
  serverList += packUInt16(4662);
  serverList += packUInt16(0);
  REQUIRE(parseServerListPayload(servers, serverList));
  REQUIRE_EQ((size_t)2, servers.size());
  REQUIRE_EQ(std::string("1.2.3.4"), servers[0].host);
  REQUIRE_EQ((uint16_t)4661, servers[0].port);
  REQUIRE_EQ(std::string("5.6.7.8"), servers[1].host);
  REQUIRE_EQ((uint16_t)4662, servers[1].port);
}

} // namespace aria2::ed2k
