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
#ifndef D_ED2K_KAD_COMMAND_H
#define D_ED2K_KAD_COMMAND_H

#include "Command.h"

#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "Ed2kKadState.h"
#include "ed2k_link.h"

namespace aria2 {

class DownloadEngine;
class RequestGroup;
class SocketCore;

namespace ed2k {
struct ServerState;
}

class Ed2kKadCommand : public Command {
public:
  Ed2kKadCommand(cuid_t cuid, RequestGroup* requestGroup, DownloadEngine* e);
  virtual ~Ed2kKadCommand();

  virtual bool execute() override;
  uint16_t getLocalUdpPort() const;
  bool waitLocalUdpReadable(time_t timeout) const;
#ifdef A2_TEST_DIR
  size_t testQueueDuePeerReasks(int64_t now) { return queueDuePeerReasks(now); }
  size_t testQueueDueKadCallbacks(int64_t now) { return queueDueKadCallbacks(now); }
  size_t testQueuedPacketCount() const { return outbox_.size(); }
  const std::pair<ed2k::Endpoint, std::string>& testQueuedPacketAt(
      size_t index) const
  {
    return outbox_.at(index);
  }
  void testHandleEd2kUdpPacket(const ed2k::Endpoint& endpoint, uint8_t opcode,
                               const std::string& payload)
  {
    handleEd2kUdpPacket(endpoint, opcode, payload);
  }
#endif // A2_TEST_DIR

private:
  RequestGroup* requestGroup_;
  DownloadEngine* e_;
  std::shared_ptr<SocketCore> socket_;
  std::deque<std::pair<ed2k::Endpoint, std::string>> outbox_;
  bool initialized_;
  int64_t lastServerStatusPoll_;
  size_t bootstrapCursor_;

  void init();
  void queueBootstrap();
  void queueRefresh();
  void queueFirewalledCheck();
  void queueSourcePublish();
  void queueServerStatusPoll();
  void queueServerSourcePoll();
  void queueSourceSearch();
  void queueKeywordSearch();
  size_t queueDuePeerReasks(int64_t now);
  size_t queueDueKadCallbacks(int64_t now);
  void queueTraversalActions(
      ed2k::KadTraversal& traversal,
      const std::vector<ed2k::KadTraversalAction>& actions);
  void queuePacket(const ed2k::Endpoint& endpoint, uint8_t opcode,
                   const std::string& payload);
  void queueKadContactPacket(const ed2k::KadContact& contact, uint8_t opcode,
                             const std::string& payload);
  void queueKadResponsePacket(const ed2k::Endpoint& endpoint,
                              const ed2k::KadObfuscatedDatagram& context,
                              uint8_t opcode, const std::string& payload);
  bool tryDecodeKadObfuscatedDatagram(ed2k::KadObfuscatedDatagram& parsed,
                                      const ed2k::Endpoint& endpoint,
                                      const std::string& raw);
  bool tryDecodeServerObfuscatedDatagram(std::string& datagram,
                                         const ed2k::Endpoint& endpoint,
                                         const std::string& raw) const;
  bool tryDecodePeerObfuscatedDatagram(std::string& datagram,
                                       const ed2k::Endpoint& endpoint,
                                       const std::string& raw) const;
  bool findServerByUdpEndpoint(ed2k::Endpoint& server,
                               const ed2k::Endpoint& endpoint) const;
  void queueServerUdpPacket(const ed2k::ServerState& server, uint8_t opcode,
                            const std::string& payload);
  void queueEmuleUdpPacket(const ed2k::Endpoint& endpoint, uint8_t opcode,
                           const std::string& payload);
  void sendQueuedPackets();
  void receivePackets();
  void handlePacket(const ed2k::Endpoint& endpoint, uint8_t opcode,
                    const std::string& payload);
  void handlePacket(const ed2k::Endpoint& endpoint,
                    const ed2k::KadObfuscatedDatagram* context,
                    uint8_t opcode, const std::string& payload);
  void handleEd2kUdpPacket(const ed2k::Endpoint& endpoint, uint8_t opcode,
                           const std::string& payload);
  RequestGroup* findKadTargetGroup(const std::string& targetId) const;
  RequestGroup* findPeerGroup(const ed2k::Endpoint& endpoint,
                              const std::string& userHash = std::string()) const;
  int64_t nowSeconds() const;
};

} // namespace aria2

#endif // D_ED2K_KAD_COMMAND_H
