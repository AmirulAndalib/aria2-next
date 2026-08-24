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
#include "CurlCheckIntegrityEntry.h"

#include "RequestGroup.h"
#include "error_code.h"

namespace aria2 {

CurlCheckIntegrityEntry::CurlCheckIntegrityEntry(RequestGroup* requestGroup)
    : PieceHashCheckIntegrityEntry(requestGroup)
{
}

void CurlCheckIntegrityEntry::onDownloadFinished(
    std::vector<std::unique_ptr<Command>>&, DownloadEngine*)
{
}

void CurlCheckIntegrityEntry::onDownloadIncomplete(
    std::vector<std::unique_ptr<Command>>&, DownloadEngine*)
{
  getRequestGroup()->setLastErrorCode(error_code::CHECKSUM_ERROR);
}

} // namespace aria2
