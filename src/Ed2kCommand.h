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
#ifndef D_ED2K_COMMAND_H
#define D_ED2K_COMMAND_H

#include "AbstractCommand.h"
#include "DHKeyExchange.h"
#include "ed2k_compression.h"
#include "ed2k_link.h"
#include "ed2k_packet.h"
#include "ed2k_peer.h"
#include "TimerA2.h"

#include <array>
#include <memory>
#include <deque>
#include <vector>
#include <cstdint>

namespace aria2 {

class SocketCore;
class ARC4Encryptor;
class PeerStat;

namespace ed2k {
class SharedResponder;
} // namespace ed2k

class Ed2kCommand : public AbstractCommand {
private:
  enum class Mode { SERVER, PEER };
  enum class State {
    INIT,
    RESOLVING,
    CONNECTING,
    INCOMING_READ_MARKER,
    INCOMING_READ_RANDOM,
    INCOMING_READ_MAGIC,
    INCOMING_READ_METHOD,
    INCOMING_READ_PADDING,
    INCOMING_WRITE_RESPONSE,
    OBFUSCATION_WRITE,
    OBFUSCATION_READ_MAGIC,
    OBFUSCATION_READ_METHOD,
    OBFUSCATION_READ_PADDING,
    SERVER_OBFUSCATION_WRITE,
    SERVER_OBFUSCATION_READ_KEY,
    SERVER_OBFUSCATION_READ_MAGIC,
    SERVER_OBFUSCATION_READ_METHOD,
    SERVER_OBFUSCATION_READ_PADDING,
    SERVER_OBFUSCATION_WRITE_RESPONSE,
    WRITE,
    READ_HEADER,
    READ_BODY,
    DONE
  };

  Mode mode_;
  ed2k::Endpoint endpoint_;
  State state_;
  std::vector<std::string> resolvedAddresses_;
  uint64_t resolveRequestId_;
  std::string connectedHostname_;
  std::string connectedAddr_;
  uint16_t connectedPort_;
  std::deque<std::string> outbox_;
  std::deque<bool> outboxEncrypted_;
  std::deque<bool> outboxTransferData_;
  std::array<char, 6> headerBuf_;
  size_t headerRead_;
  ed2k::PacketHeader currentHeader_;
  std::string body_;
  size_t bodyRead_;
  bool peerFileStatusReceived_;
  bool peerFileRequestSent_;
  bool peerFileStatusRequested_;
  bool peerAccepted_;
  bool sourceExchangeRequested_;
  bool aichFileHashRequested_;
  bool use64BitOffsets_;
  bool incoming_;
  bool countAsDownloadCommand_;
  bool firewallCheck_;
  bool closeAfterOutbox_;
  Timer protocolActivity_;
  Timer tailReclaimTimer_;
  struct PendingCallback {
    RequestGroup* group;
    uint32_t clientId;
  };
  std::deque<PendingCallback> pendingCallbacks_;
  ed2k::EmulePeerInfo localPeerInfo_;
  ed2k::EmulePeerInfo remotePeerInfo_;
  std::unique_ptr<ARC4Encryptor> obfuscationEncryptor_;
  std::unique_ptr<ARC4Encryptor> obfuscationDecryptor_;
  std::string obfuscationWriteBuf_;
  size_t obfuscationWriteOffset_;
  std::array<char, 4> obfuscationMagicBuf_;
  size_t obfuscationMagicRead_;
  std::array<char, 2> obfuscationMethodBuf_;
  size_t obfuscationMethodRead_;
  std::array<char, 3> incomingObfuscationMethodBuf_;
  size_t incomingObfuscationMethodRead_;
  char incomingObfuscationMarker_;
  std::string obfuscationPaddingBuf_;
  size_t obfuscationPaddingRead_;
  bool obfuscationEnabled_;
  bool serverObfuscation_;
  std::unique_ptr<DHKeyExchange> serverDh_;
  MSEDHPublicKey serverDhPeerKey_{};
  size_t serverDhPeerKeyRead_ = 0;
  struct CompressedPartState {
    ed2k::PartRange block;
    ed2k::CompressedPartInflater inflater;
  };
  std::vector<std::unique_ptr<CompressedPartState>> compressedPartStates_;
  std::shared_ptr<PeerStat> peerStat_;

  bool queueDueServerRequest();
  bool protocolDeadlineActive() const;
  void noteProtocolActivity();
  bool downloadRateLimited() const;
  bool uploadRateLimited() const;
  bool shouldObfuscatePeerConnection() const;
  void initPeerObfuscation();
  bool flushObfuscationHandshake();
  bool readObfuscationMagic();
  bool readObfuscationMethod();
  bool readObfuscationPadding();
  bool readIncomingObfuscationMarker();
  bool readIncomingObfuscationRandom();
  bool readIncomingObfuscationMagic();
  bool readIncomingObfuscationMethod();
  bool readIncomingObfuscationPadding();
  bool flushIncomingObfuscationResponse();
  bool shouldObfuscateServerConnection() const;
  void initServerObfuscation();
  bool flushServerObfuscationRequest();
  bool readServerObfuscationKey();
  bool readServerObfuscationMagic();
  bool readServerObfuscationMethod();
  bool readServerObfuscationPadding();
  bool flushServerObfuscationResponse();
  void encryptPacket(std::string& data);
  void decryptData(char* data, size_t length);
  void resetCompressedPartInflaters();
  CompressedPartState* findCompressedPartState(int64_t begin);
  CompressedPartState* getOrCreateCompressedPartState(
      const ed2k::PartRange& block);
  void releaseCompletedCompressedPartState(const ed2k::PartRange& block);
  void startResolve();
  void startConnect();
  bool flushOutbox();
  bool readHeader();
  bool readBody();
  void handlePacket();
  void handleServerPacket();
  void handlePeerPacket();
  void queuePacket(uint8_t protocol, uint8_t opcode, const std::string& payload);
  void queueServerLogin();
  void queueServerOfferFiles();
  bool queueGetSources(RequestGroup* group = nullptr);
  bool queueAllServerSourceRequests();
  void queueSearchRequest();
  void queueCallbackRequest(RequestGroup* group, uint32_t clientId);
  uint32_t localEd2kClientId() const;
  ed2k::Endpoint localEd2kServerEndpoint() const;
  void queuePeerHello();
  void queuePeerHelloAnswer();
  void queueEmuleInfo(bool answer);
  void queuePeerFileRequest();
  void queuePeerFileStatusRequest();
  void queuePeerHashSetRequest();
  void queuePeerPostFileStatusRequests();
  void queueAichFileHashRequest();
  void queueAichRecoveryRequest(size_t pieceIndex);
  void queueSourceExchangeRequest();
  void queueSourceExchangeAnswer(uint8_t version);
  void queuePeerStartUpload();
  void queuePeerPartRequest();
  bool queueActivePeerPartReclaim();
  void queueCancelTransfer();
  bool sendPendingCancelTransfer();
  bool expireStalledTransfer();
  ed2k::SharedResponder createSharedResponder();
  bool updatePeerEndpointFromHello(bool helloPacket);
  bool switchToAlternativeDownload();
  void routeIncomingFileRequest();
  void addPeer(const ed2k::Endpoint& peer);
  void addPeers(const std::vector<ed2k::Endpoint>& peers);
  void schedulePendingPeers();
  void handlePartData(int64_t begin, const std::string& data);

protected:
  virtual bool executeInternal() override;
  virtual bool noCheck() const override;
  virtual bool prepareForRetry(time_t wait) override;

public:
  Ed2kCommand(cuid_t cuid, RequestGroup* requestGroup, DownloadEngine* e,
              ed2k::Endpoint endpoint, bool serverMode,
              bool countAsDownloadCommand = true,
              bool firewallCheck = false);
  Ed2kCommand(cuid_t cuid, RequestGroup* requestGroup, DownloadEngine* e,
              ed2k::Endpoint endpoint,
              const std::shared_ptr<SocketCore>& socket);
  virtual ~Ed2kCommand();

  virtual bool execute() override;
};

} // namespace aria2

#endif // D_ED2K_COMMAND_H
