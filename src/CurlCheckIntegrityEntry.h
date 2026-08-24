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
#ifndef D_CURL_CHECK_INTEGRITY_ENTRY_H
#define D_CURL_CHECK_INTEGRITY_ENTRY_H

#include "Command.h"
#include "PieceHashCheckIntegrityEntry.h"

namespace aria2 {

class CurlCheckIntegrityEntry : public PieceHashCheckIntegrityEntry {
public:
  explicit CurlCheckIntegrityEntry(RequestGroup* requestGroup);

  void onDownloadFinished(std::vector<std::unique_ptr<Command>>& commands,
                          DownloadEngine* engine) override;
  void onDownloadIncomplete(std::vector<std::unique_ptr<Command>>& commands,
                            DownloadEngine* engine) override;
};

} // namespace aria2

#endif // D_CURL_CHECK_INTEGRITY_ENTRY_H
