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
#ifndef D_CURL_DOWNLOAD_COMMAND_H
#define D_CURL_DOWNLOAD_COMMAND_H

#include "Command.h"

#include <memory>

namespace aria2 {

class CurlDownload;
class CurlSession;
class DownloadEngine;
class RequestGroup;

class CurlDownloadCommand : public Command {
public:
  CurlDownloadCommand(cuid_t cuid, std::shared_ptr<CurlDownload> download,
                      CurlSession* session, RequestGroup* group,
                      DownloadEngine* engine);
  ~CurlDownloadCommand() override;

  bool execute() override;

private:
  std::shared_ptr<CurlDownload> download_;
  CurlSession* session_;
  RequestGroup* group_;
  DownloadEngine* engine_;
};

} // namespace aria2

#endif // D_CURL_DOWNLOAD_COMMAND_H
