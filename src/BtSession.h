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
#ifndef D_BT_SESSION_H
#define D_BT_SESSION_H

#include "common.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "BtDownload.h"
#include "GroupId.h"

namespace aria2 {

class Command;
class DownloadEngine;
class Option;
class RequestGroup;

class BtSession {
public:
  struct Impl;

private:
  std::unique_ptr<Impl> impl_;

public:
  explicit BtSession(const Option* option);
  ~BtSession();

  BtSession(const BtSession&) = delete;
  BtSession& operator=(const BtSession&) = delete;

  std::unique_ptr<Command> start(const std::shared_ptr<BtDownload>& download,
                                 RequestGroup* group, DownloadEngine* engine);

  void poll();
  void requestStop(const std::shared_ptr<BtDownload>& download,
                   BtDownload::StopReason reason);
  void applyGlobalOptions(const Option* option);
  void applyDownloadOptions(const std::shared_ptr<BtDownload>& download,
                            const Option* option);
  void remove(a2_gid_t gid);

  uint16_t listenPort() const;
  uint16_t announcePort() const;
  std::string externalAddress() const;

  bool replaceIpFilter(const std::vector<std::string>& rules,
                       std::string& error);
  void loadIpFilter(const std::string& path);
  size_t ipFilterRuleCount() const;
  uint64_t ipFilterRevision() const;
};

} // namespace aria2

#endif // D_BT_SESSION_H
