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
#ifdef _WIN32
#  include <windows.h>
#endif
#include <string>
#include "ApplicationStatePath.h"

#include <shlobj.h>

#include "DlAbortEx.h"
#include "support/FilePath.h"
#include "platform/NativeText.h"

namespace aria2 {

namespace state {

std::string defaultDirectory()
{
  PWSTR path = nullptr;
  const auto result = SHGetKnownFolderPath(FOLDERID_LocalAppData,
                                           KF_FLAG_DEFAULT, nullptr, &path);
  if (FAILED(result) || !path) {
    throw DL_ABORT_EX(
        "Unable to resolve the Windows application state directory");
  }
  const auto root = toForwardSlash(wCharToUtf8(path));
  CoTaskMemFree(path);
  return util::applyDir(root, "aria2-next");
}

} // namespace state

} // namespace aria2
