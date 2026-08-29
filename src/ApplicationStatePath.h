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
#ifndef D_APPLICATION_STATE_PATH_H
#define D_APPLICATION_STATE_PATH_H

#include <string>

namespace aria2 {

class Option;

namespace state {

std::string defaultDirectory();
std::string btSessionFile(const Option* option);
std::string btTorrentDirectory(const Option* option);
std::string ed2kDatabaseFile(const Option* option);
std::string streamDatabaseFile(const Option* option);

} // namespace state

} // namespace aria2

#endif // D_APPLICATION_STATE_PATH_H
