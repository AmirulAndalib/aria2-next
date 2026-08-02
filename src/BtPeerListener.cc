/* <!-- copyright */
/*
 * aria2 - The high speed download utility
 *
 * Copyright (C) 2026 The aria2-next contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
/* copyright --> */
#include "BtPeerListener.h"

#include <algorithm>

#include "Log.h"
#include "RecoverableException.h"
#include "SegList.h"
#include "SimpleRandomizer.h"
#include "SocketCore.h"
#include "fmt.h"
#include "util.h"

namespace aria2 {

namespace {

std::shared_ptr<SocketCore> bindListener(uint16_t port, int family)
{
  auto socket = std::make_shared<SocketCore>();
  socket->bind(nullptr, port, family);
  socket->beginListen();
  return socket;
}

bool containsFamily(const std::vector<BtPeerListener::Entry>& entries,
                    int family)
{
  return std::any_of(entries.begin(), entries.end(),
                     [family](const BtPeerListener::Entry& entry) {
                       return entry.family == family;
                     });
}

} // namespace

bool BtPeerListener::rebind(const std::string& portSpec, bool enableIPv6)
{
  auto segments = util::parseIntSegments(portSpec);
  segments.normalize();
  std::vector<uint16_t> ports;
  while (segments.hasNext()) {
    ports.push_back(segments.next());
  }
  std::shuffle(ports.begin(), ports.end(), *SimpleRandomizer::getInstance());

  if (active() && std::find(ports.begin(), ports.end(), port_) != ports.end()) {
    return true;
  }

  std::vector<int> families{AF_INET};
  if (enableIPv6) {
    families.push_back(AF_INET6);
  }

  for (const auto port : ports) {
    std::vector<Entry> prepared;
    bool requiredFamilyFailed = false;
    for (const auto family : families) {
      try {
        prepared.push_back(Entry{family, bindListener(port, family)});
      }
      catch (RecoverableException& ex) {
        const auto required = containsFamily(entries_, family);
        const auto version = family == AF_INET ? 4 : 6;
        if (required) {
          requiredFamilyFailed = true;
          A2_LOG_ERROR_EX(
              fmt("IPv%d BitTorrent: failed to rebind TCP port %u", version,
                  port),
              ex);
        }
        else {
          A2_LOG_DEBUG_EX(
              fmt("IPv%d BitTorrent: TCP port %u is unavailable", version,
                  port),
              ex);
        }
      }
    }

    if (requiredFamilyFailed || prepared.empty()) {
      continue;
    }

    entries_.swap(prepared);
    port_ = port;
    for (const auto& entry : entries_) {
      const auto version = entry.family == AF_INET ? 4 : 6;
      A2_LOG_INFO(
          fmt(_("IPv%d BitTorrent: listening on TCP port %u"), version, port));
    }
    return true;
  }

  return false;
}

void BtPeerListener::close()
{
  entries_.clear();
  port_ = 0;
}

} // namespace aria2
