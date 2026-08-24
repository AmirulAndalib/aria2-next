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

#include "Option.h"
#include "prefs.h"
#include "util.h"

namespace aria2 {

namespace state {

namespace {

std::string protocolDirectory(const Option* option, const char* protocol)
{
  if (!option || option->blank(PREF_STATE_DIR)) {
    return {};
  }
  return util::applyDir(option->get(PREF_STATE_DIR), protocol);
}

} // namespace

std::string btSessionFile(const Option* option)
{
  const auto directory = protocolDirectory(option, "bittorrent");
  return directory.empty() ? std::string()
                           : util::applyDir(directory, "session");
}

std::string btResumeDirectory(const Option* option)
{
  const auto directory = protocolDirectory(option, "bittorrent");
  return directory.empty() ? std::string()
                           : util::applyDir(directory, "torrents");
}

std::string ed2kDatabaseFile(const Option* option)
{
  const auto directory = protocolDirectory(option, "ed2k");
  return directory.empty() ? std::string()
                           : util::applyDir(directory, "state.db");
}

std::string streamDatabaseFile(const Option* option)
{
  const auto directory = protocolDirectory(option, "stream");
  return directory.empty() ? std::string()
                           : util::applyDir(directory, "state.db");
}

} // namespace state

} // namespace aria2
