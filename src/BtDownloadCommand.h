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
#ifndef D_BT_DOWNLOAD_COMMAND_H
#define D_BT_DOWNLOAD_COMMAND_H

#include "Command.h"

#include <memory>

namespace aria2 {

class BtDownload;
class BtSession;
class DownloadEngine;
class RequestGroup;

class BtDownloadCommand : public Command {
private:
  std::shared_ptr<BtDownload> download_;
  BtSession* session_;
  RequestGroup* group_;
  DownloadEngine* engine_;
  bool seedingStarted_ = false;
  bool completionNotified_ = false;

public:
  BtDownloadCommand(cuid_t cuid, std::shared_ptr<BtDownload> download,
                    BtSession* session, RequestGroup* group,
                    DownloadEngine* engine);
  ~BtDownloadCommand() override;

  bool execute() override;
};

} // namespace aria2

#endif // D_BT_DOWNLOAD_COMMAND_H
