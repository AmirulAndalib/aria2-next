/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
#include <cstdint>
#include <string>
#include <vector>
#include "ed2k_aich.h"
#include "ed2k_hash.h"
#include "ed2k_kad.h"
#include "ed2k_link.h"
#include "ed2k_packet.h"
#include "ed2k_server.h"
#include "ed2k_endpoint.h"
#include "Ed2kKadState.h"

#include "a2doctest.h"
#include <cstring>

#include "support/Encoding.h"

namespace aria2::ed2k {

TEST_CASE("Ed2kHelperTest.testKadRoutingStatePayload")
{
  std::string selfIdHex("23a8ceff57a7a32d562d649ed7893796");
  auto selfId = util::fromHex(selfIdHex.begin(), selfIdHex.end());
  KadRoutingSnapshot snapshot;
  snapshot.selfId = selfId;
  snapshot.lastBootstrap = 100;
  snapshot.lastRefresh = 200;
  snapshot.lastSelfRefresh = 300;
  snapshot.lastFirewalledCheck = 500;
  snapshot.lastSourcePublish = 600;
  snapshot.lastSourceSearch = 700;
  snapshot.sourceSearchCount = 3;
  snapshot.udpVerifyKey = 0x61726961;
  snapshot.firewalled = false;
  snapshot.observedAddresses.push_back("203.0.113.55");
  Endpoint router;
  router.host = "203.0.113.1";
  router.port = 4672;
  snapshot.routerNodes.push_back(router);
  KadContact routerContact;
  std::string routerIdHex("11111111111111111111111111111111");
  routerContact.id = util::fromHex(routerIdHex.begin(), routerIdHex.end());
  routerContact.host = "203.0.113.2";
  routerContact.udpPort = 4672;
  routerContact.tcpPort = 4662;
  routerContact.version = 8;
  routerContact.udpKey = 0x55667788;
  snapshot.routerContacts.push_back(routerContact);
  snapshot.buckets.resize(2);
  snapshot.buckets[1].lastActive = 400;
  KadRoutingNode node;
  std::string nodeIdHex("31d6cfe0d16ae931b73c59d7e0c089c0");
  node.contact.id = util::fromHex(nodeIdHex.begin(), nodeIdHex.end());
  node.contact.host = "198.51.100.2";
  node.contact.udpPort = 4672;
  node.contact.tcpPort = 4662;
  node.contact.version = 8;
  node.confirmed = true;
  node.seed = true;
  node.failCount = 3;
  node.firstSeen = 50;
  node.lastSeen = 75;
  snapshot.buckets[1].live.push_back(node);
  node.contact.host = "198.51.100.3";
  node.confirmed = false;
  node.seed = false;
  snapshot.buckets[1].replacements.push_back(node);

  auto payload = createKadRoutingStatePayload(snapshot);
  KadRoutingSnapshot parsed;
  REQUIRE(parseKadRoutingStatePayload(parsed, payload));
  REQUIRE_EQ(selfId, parsed.selfId);
  REQUIRE_EQ((int64_t)100, parsed.lastBootstrap);
  REQUIRE_EQ((int64_t)200, parsed.lastRefresh);
  REQUIRE_EQ((int64_t)300, parsed.lastSelfRefresh);
  REQUIRE_EQ((int64_t)500, parsed.lastFirewalledCheck);
  REQUIRE_EQ((int64_t)600, parsed.lastSourcePublish);
  REQUIRE_EQ((int64_t)700, parsed.lastSourceSearch);
  REQUIRE_EQ((uint32_t)3, parsed.sourceSearchCount);
  REQUIRE_EQ((uint32_t)0x61726961, parsed.udpVerifyKey);
  REQUIRE(!parsed.firewalled);
  REQUIRE_EQ((size_t)1, parsed.observedAddresses.size());
  REQUIRE_EQ(std::string("203.0.113.55"), parsed.observedAddresses[0]);
  REQUIRE_EQ((size_t)1, parsed.routerNodes.size());
  REQUIRE_EQ(std::string("203.0.113.1"), parsed.routerNodes[0].host);
  REQUIRE_EQ((uint16_t)4672, parsed.routerNodes[0].port);
  REQUIRE_EQ((size_t)1, parsed.routerContacts.size());
  REQUIRE_EQ(routerContact.id, parsed.routerContacts[0].id);
  REQUIRE_EQ(std::string("203.0.113.2"), parsed.routerContacts[0].host);
  REQUIRE_EQ((uint8_t)8, parsed.routerContacts[0].version);
  REQUIRE_EQ((uint32_t)0x55667788, parsed.routerContacts[0].udpKey);
  REQUIRE_EQ((size_t)2, parsed.buckets.size());
  REQUIRE_EQ((int64_t)400, parsed.buckets[1].lastActive);
  REQUIRE_EQ((size_t)1, parsed.buckets[1].live.size());
  REQUIRE(parsed.buckets[1].live[0].confirmed);
  REQUIRE(parsed.buckets[1].live[0].seed);
  REQUIRE_EQ((uint32_t)3, parsed.buckets[1].live[0].failCount);
  REQUIRE_EQ((int64_t)50, parsed.buckets[1].live[0].firstSeen);
  REQUIRE_EQ((int64_t)75, parsed.buckets[1].live[0].lastSeen);
  REQUIRE_EQ(std::string("198.51.100.2"),
             parsed.buckets[1].live[0].contact.host);
  REQUIRE_EQ((size_t)1, parsed.buckets[1].replacements.size());
  REQUIRE(!parsed.buckets[1].replacements[0].confirmed);
  REQUIRE_EQ(std::string("198.51.100.3"),
             parsed.buckets[1].replacements[0].contact.host);

  REQUIRE(!parseKadRoutingStatePayload(parsed,
                                       payload.substr(0, payload.size() - 1)));
}

TEST_CASE("Ed2kHelperTest.testServerStatePayload")
{
  ServerState state;
  Endpoint server;
  server.host = "203.0.113.10";
  server.port = 4661;
  state.endpoint = server;
  state.name = "Peer Server";
  state.description = "Primary ED2K server";
  state.connected = true;
  state.handshakeCompleted = true;
  state.clientId = 0x0a000001;
  state.highId = true;
  state.ipAddress = "1.0.0.10";
  state.tcpFlags = 0x55aa;
  state.users = 1234;
  state.files = 5678;
  state.maxUsers = 9000;
  state.softFiles = 100;
  state.hardFiles = 200;
  state.udpFlags = 0x01020304;
  state.lowIdUsers = 77;
  state.udpObfuscationPort = 4665;
  state.tcpObfuscationPort = 4666;
  state.udpKey = 0x11223344;
  state.udpStatusChallenge = 0x55aa0011;
  state.lastUdpStatusTime = 120;
  state.nextSourceRequestTime = 180;
  state.lastSourceResponseTime = 200;
  state.lastSourceCount = 3;
  state.lastUdpSourceRequestTime = 210;
  state.failCount = 2;
  state.lastFailureTime = 100;
  state.nextRetryTime = 160;
  state.lastMessage = "hello";

  auto payload = createServerStatePayload(state);
  ServerState parsed;
  REQUIRE(parseServerStatePayload(parsed, payload));
  REQUIRE_EQ(std::string("203.0.113.10"), parsed.endpoint.host);
  REQUIRE_EQ((uint16_t)4661, parsed.endpoint.port);
  REQUIRE_EQ(std::string("Peer Server"), parsed.name);
  REQUIRE_EQ(std::string("Primary ED2K server"), parsed.description);
  REQUIRE(parsed.connected);
  REQUIRE(parsed.handshakeCompleted);
  REQUIRE_EQ((uint32_t)0x0a000001, parsed.clientId);
  REQUIRE(parsed.highId);
  REQUIRE_EQ(std::string("1.0.0.10"), parsed.ipAddress);
  REQUIRE_EQ((uint32_t)0x55aa, parsed.tcpFlags);
  REQUIRE_EQ((uint32_t)1234, parsed.users);
  REQUIRE_EQ((uint32_t)5678, parsed.files);
  REQUIRE_EQ((uint32_t)9000, parsed.maxUsers);
  REQUIRE_EQ((uint32_t)100, parsed.softFiles);
  REQUIRE_EQ((uint32_t)200, parsed.hardFiles);
  REQUIRE_EQ((uint32_t)0x01020304, parsed.udpFlags);
  REQUIRE_EQ((uint32_t)77, parsed.lowIdUsers);
  REQUIRE_EQ((uint16_t)4665, parsed.udpObfuscationPort);
  REQUIRE_EQ((uint16_t)4666, parsed.tcpObfuscationPort);
  REQUIRE_EQ((uint32_t)0x11223344, parsed.udpKey);
  REQUIRE_EQ((uint32_t)0x55aa0011, parsed.udpStatusChallenge);
  REQUIRE_EQ((int64_t)120, parsed.lastUdpStatusTime);
  REQUIRE_EQ((int64_t)180, parsed.nextSourceRequestTime);
  REQUIRE_EQ((int64_t)200, parsed.lastSourceResponseTime);
  REQUIRE_EQ((uint32_t)3, parsed.lastSourceCount);
  REQUIRE_EQ((int64_t)210, parsed.lastUdpSourceRequestTime);
  REQUIRE_EQ((uint32_t)2, parsed.failCount);
  REQUIRE_EQ((int64_t)100, parsed.lastFailureTime);
  REQUIRE_EQ((int64_t)160, parsed.nextRetryTime);
  REQUIRE_EQ(std::string("hello"), parsed.lastMessage);

  std::string v1Payload = payload;
  v1Payload.replace(sizeof("A2ED2KSRV") - 1, 4, packUInt32(1));
  v1Payload.erase(sizeof("A2ED2KSRV") - 1 + 4 + 2 + server.host.size() + 2,
                  2 + state.name.size() + 2 + state.description.size());
  v1Payload.erase(sizeof("A2ED2KSRV") - 1 + 4 + 2 + server.host.size() + 2 + 2 +
                      4 + 1 + 2 + state.ipAddress.size() + 4 + 4 + 4 + 4 + 4 +
                      4 + 4 + 4 + 2 + 2 + 4 + 4 + 8,
                  8 + 4 + 8 + 8);
  REQUIRE(parseServerStatePayload(parsed, v1Payload));
  REQUIRE_EQ((int64_t)0, parsed.nextSourceRequestTime);
  REQUIRE_EQ((int64_t)0, parsed.lastSourceResponseTime);
  REQUIRE_EQ((uint32_t)0, parsed.lastSourceCount);
  REQUIRE_EQ((int64_t)0, parsed.lastUdpSourceRequestTime);
  REQUIRE(parsed.name.empty());
  REQUIRE(parsed.description.empty());

  REQUIRE(
      !parseServerStatePayload(parsed, payload.substr(0, payload.size() - 1)));
}

TEST_CASE("Ed2kHelperTest.testNodesDatParser")
{
  std::string nodeIdHex("23a8ceff57a7a32d562d649ed7893796");
  auto nodeId = util::fromHex(nodeIdHex.begin(), nodeIdHex.end());
  KadContact contact;
  contact.id = nodeId;
  contact.host = "203.0.113.1";
  contact.udpPort = 4672;
  contact.tcpPort = 4661;
  contact.version = 8;
  KadContact invalid = contact;
  invalid.host = "0.0.0.0";

  std::string payload;
  payload += packUInt32(0);
  payload += packUInt32(3);
  payload += packUInt32(1);
  payload += packUInt32(1);
  payload += createKadResponsePayload(nodeId, std::vector<KadContact>{contact})
                 .substr(HASH_LENGTH + 1);

  NodesDat nodes;
  REQUIRE(parseNodesDat(nodes, payload));
  REQUIRE_EQ((uint32_t)3, nodes.version);
  REQUIRE_EQ((uint32_t)1, nodes.bootstrapEdition);
  REQUIRE_EQ((size_t)1, nodes.contacts.size());
  REQUIRE_EQ(std::string("203.0.113.1"), nodes.contacts[0].host);
  REQUIRE_EQ((uint16_t)4672, nodes.contacts[0].udpPort);
  REQUIRE_EQ((size_t)1, nodes.verified.size());
  REQUIRE(nodes.verified[0]);

  std::string amuleV2;
  amuleV2 += packUInt32(0);
  amuleV2 += packUInt32(2);
  amuleV2 += packUInt32(1);
  amuleV2 += nodeId;
  amuleV2 += std::string("\x05\x18\x9f\x01", 4);
  amuleV2 += packUInt16(4672);
  amuleV2 += packUInt16(4662);
  amuleV2.push_back('\x08');
  amuleV2 += packUInt64(0);
  amuleV2.push_back('\0');
  REQUIRE(parseNodesDat(nodes, amuleV2));
  REQUIRE_EQ((size_t)1, nodes.contacts.size());
  REQUIRE_EQ(std::string("1.159.24.5"), nodes.contacts[0].host);

  std::string normal;
  normal += packUInt32(0);
  normal += packUInt32(2);
  normal += packUInt32(2);
  normal += createKadResponsePayload(nodeId,
                                     std::vector<KadContact>{contact, invalid})
                .substr(HASH_LENGTH + 1);
  normal += packUInt64(0);
  normal.push_back('\0');
  normal += packUInt64(0);
  normal.push_back('\x01');
  REQUIRE(parseNodesDat(nodes, normal));
  REQUIRE_EQ((uint32_t)2, nodes.version);
  REQUIRE_EQ((size_t)1, nodes.verified.size());
  REQUIRE(nodes.verified[0]);

  REQUIRE(!parseNodesDat(nodes, packUInt32(0) + packUInt32(2) +
                                    packUInt32(0xffffffffu)));
}

TEST_CASE("Ed2kHelperTest.testServerMetParser")
{
  std::string data;
  data.push_back('\x0e');
  data += packUInt32(1);
  data += packUInt32(0x04030201);
  data += packUInt16(4661);
  data += packUInt32(8);
  data.push_back('\x82');
  data.push_back('\x01');
  data += packUInt16(11);
  data += "Peer Server";
  data.push_back('\x82');
  data.push_back('\x0b');
  data += packUInt16(19);
  data += "Primary ED2K server";
  data += createUInt32Tag(0x87, 9000);
  data += createUInt32Tag(0x88, 100);
  data += createUInt32Tag(0x89, 200);
  data += createUInt32Tag(0x92, 0x01020304);
  data += createUInt32Tag(0x94, 77);
  data += createUInt32Tag(0x95, 0x11223344);

  auto servers = parseServerMet(data);

  REQUIRE_EQ((size_t)1, servers.size());
  REQUIRE_EQ(std::string("1.2.3.4"), servers[0].host);
  REQUIRE_EQ((uint16_t)4661, servers[0].port);

  auto entries = parseServerMetEntries(data);

  REQUIRE_EQ((size_t)1, entries.size());
  REQUIRE_EQ(std::string("1.2.3.4"), entries[0].endpoint.host);
  REQUIRE_EQ((uint16_t)4661, entries[0].endpoint.port);
  REQUIRE_EQ(std::string("Peer Server"), entries[0].name);
  REQUIRE_EQ(std::string("Primary ED2K server"), entries[0].description);
  REQUIRE_EQ((uint32_t)9000, entries[0].maxUsers);
  REQUIRE_EQ((uint32_t)100, entries[0].softFiles);
  REQUIRE_EQ((uint32_t)200, entries[0].hardFiles);
  REQUIRE_EQ((uint32_t)0x01020304, entries[0].udpFlags);
  REQUIRE_EQ((uint32_t)77, entries[0].lowIdUsers);
  REQUIRE_EQ((uint32_t)0x11223344, entries[0].udpKey);

  data[0] = static_cast<char>(0xe0);
  entries = parseServerMetEntries(data);

  REQUIRE_EQ((size_t)1, entries.size());
  REQUIRE_EQ(std::string("1.2.3.4"), entries[0].endpoint.host);
  REQUIRE_EQ((uint16_t)4661, entries[0].endpoint.port);
  REQUIRE_EQ(std::string("Peer Server"), entries[0].name);

  std::string hostnameEntry;
  hostnameEntry.push_back('\x0e');
  hostnameEntry += packUInt32(1);
  hostnameEntry += packUInt32(0);
  hostnameEntry += packUInt16(4661);
  hostnameEntry += packUInt32(2);
  hostnameEntry += createStringTag(0x85, "peer.example.org");
  hostnameEntry += createStringTag(0x01, "Hostname Server");

  entries = parseServerMetEntries(hostnameEntry);

  REQUIRE_EQ((size_t)1, entries.size());
  REQUIRE_EQ(std::string("peer.example.org"), entries[0].endpoint.host);
  REQUIRE_EQ((uint16_t)4661, entries[0].endpoint.port);
  REQUIRE_EQ(std::string("Hostname Server"), entries[0].name);
}

} // namespace aria2::ed2k
