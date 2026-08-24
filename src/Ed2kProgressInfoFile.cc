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
#include "Ed2kProgressInfoFile.h"

#include "Ed2kSession.h"
#include "RequestGroup.h"

namespace aria2 {

namespace ed2k {

std::string Ed2kProgressInfoFile::getFilename()
{
  return session_ ? session_->databasePath() : std::string();
}

bool Ed2kProgressInfoFile::exists()
{
  return session_ && group_ && session_->hasDownloadState(group_);
}

void Ed2kProgressInfoFile::save()
{
  if (session_ && group_) {
    session_->saveDownloadState(group_);
  }
}

void Ed2kProgressInfoFile::load()
{
  if (session_ && group_) {
    session_->loadDownloadState(group_);
  }
}

void Ed2kProgressInfoFile::removeFile()
{
  if (session_ && group_) {
    session_->removeDownloadState(group_);
  }
}

} // namespace ed2k

} // namespace aria2
