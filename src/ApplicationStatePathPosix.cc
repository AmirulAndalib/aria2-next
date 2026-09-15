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
#include "ApplicationStatePath.h"

#include "DlAbortEx.h"
#include "util.h"

namespace aria2 {

namespace state {

std::string defaultDirectory()
{
  const auto home = util::getHomeDir();
  const auto root = util::getXDGDir(
      "XDG_STATE_HOME", home.empty() ? std::string() : home + "/.local/state");
  if (root.empty()) {
    throw DL_ABORT_EX("Unable to resolve the application state directory");
  }
  return util::applyDir(root, "aria2-next");
}

} // namespace state

} // namespace aria2
