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
#ifndef ARIA2_ED2K_COMMAND_INTERNAL_H
#define ARIA2_ED2K_COMMAND_INTERNAL_H
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
#include "common.h"
#include "wallclock.h"

namespace aria2::ed2k_command {
constexpr uint8_t ED2K_OBFUSCATION_MAGIC_REQUESTER = 34;
constexpr uint8_t ED2K_OBFUSCATION_MAGIC_SERVER = 203;
constexpr uint32_t ED2K_OBFUSCATION_SYNC = 0x835e6fc4;
constexpr uint8_t ED2K_OBFUSCATION_METHOD = 0;

std::shared_ptr<Request> makeEd2kRequest(const ed2k::Endpoint& endpoint,
                                         bool serverMode);
bool rangesOverlap(const ed2k::PartRange& lhs, const ed2k::PartRange& rhs);
bool blockRangeAvailable(Ed2kAttribute* attrs, const ed2k::PartRange& range);
bool blockRangeAvailable(const std::vector<ed2k::PartRange>& ranges,
                         const ed2k::PartRange& range);
uint16_t localEd2kTcpPort(const DownloadEngine* e);
uint16_t localEd2kUdpPort(const DownloadEngine* e);
std::string createPeerFileRequestPayload(const DownloadContext* dctx,
                                         PieceStorage* pieceStorage,
                                         const std::string& fileHash,
                                         uint8_t extendedRequestsVersion);
bool isFileRequestPayloadForHash(const std::string& payload,
                                 const std::string& expectedHash);
std::vector<bool> createLocalPartStatus(const DownloadContext* dctx,
                                        PieceStorage* pieceStorage);
int64_t nowSeconds();
void storeAichRecoverySet(Ed2kAttribute* attrs,
                          const ed2k::AichRecoverySet& recoverySet);
void discardArc4Prefix(ARC4Encryptor& rc4);
bool isEd2kProtocolMarker(uint8_t value);
void updatePeerUdpMetadata(Ed2kAttribute* attrs, const ed2k::Endpoint& endpoint,
                           const ed2k::EmulePeerInfo& info);
} // namespace aria2::ed2k_command
#endif
