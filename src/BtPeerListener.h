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
#ifndef D_BT_PEER_LISTENER_H
#define D_BT_PEER_LISTENER_H

#include "common.h"

#include <memory>
#include <string>
#include <vector>

namespace aria2 {

class SocketCore;

class BtPeerListener {
public:
  struct Entry {
    int family;
    std::shared_ptr<SocketCore> socket;
  };

private:
  std::vector<Entry> entries_;
  uint16_t port_ = 0;

public:
  bool rebind(const std::string& portSpec, bool enableIPv6);

  void close();

  bool active() const { return !entries_.empty(); }

  uint16_t port() const { return port_; }

  const std::vector<Entry>& entries() const { return entries_; }
};

} // namespace aria2

#endif // D_BT_PEER_LISTENER_H
