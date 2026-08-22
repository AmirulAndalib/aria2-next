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
#ifndef D_BT_RESUME_STORE_H
#define D_BT_RESUME_STORE_H

#include <cstddef>
#include <string>

namespace aria2 {

class Option;

class BtResumeStore {
public:
  static std::string path(const Option* option, const std::string& infoHash);
  static std::string read(const std::string& path);
  static void write(const std::string& path, const char* data, size_t size);
};

} // namespace aria2

#endif // D_BT_RESUME_STORE_H
