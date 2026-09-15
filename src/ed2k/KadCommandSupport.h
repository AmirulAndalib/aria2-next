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
#ifndef D_KAD_COMMAND_SUPPORT_H
#define D_KAD_COMMAND_SUPPORT_H
#include "Ed2kKadState.h"
#include <vector>

namespace aria2 {
struct Ed2kAttribute;
class DownloadEngine;
class RequestGroup;
namespace kad_command {
ed2k::KadTraversal* pendingTraversal(Ed2kAttribute& attrs,
                                     ed2k::KadTransactionPurpose purpose);
ed2k::Endpoint toEndpoint(const ed2k::KadContact& contact);
uint16_t localEd2kTcpPort(const DownloadEngine* e);
uint16_t localEd2kUdpPort(const DownloadEngine* e);
uint32_t localKadUdpVerifyKey(const Ed2kAttribute* attrs,
                              const ed2k::Endpoint& endpoint);
bool publishableAddress(const std::string& host);
uint32_t publicIpv4Value(const Ed2kAttribute* attrs);
uint8_t localDirectCallbackOptions();
std::vector<bool> localPartStatus(RequestGroup* group);
} // namespace kad_command
} // namespace aria2
#endif // D_KAD_COMMAND_SUPPORT_H
