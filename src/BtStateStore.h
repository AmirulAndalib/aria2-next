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
#ifndef D_BT_STATE_STORE_H
#define D_BT_STATE_STORE_H

#include <cstddef>
#include <set>
#include <string>

namespace aria2 {

class Option;

struct BtStateReference {
  std::string metadataPath;
  std::string resumePath;
};

class BtStateStore {
public:
  explicit BtStateStore(const Option* option);

  const std::string& directory() const { return directory_; }
  std::string metadataPath(const std::string& data) const;
  std::string resumePath(const std::string& identity) const;
  std::string storeMetadata(const std::string& data) const;
  bool ownsMetadata(const std::string& path) const;

  static std::string readResume(const std::string& path);
  static void writeResume(const std::string& path, const char* data,
                          size_t size);

  void collect(const std::set<std::string>& referencedPaths) const;

private:
  static void writeAtomic(const std::string& path, const char* data,
                          size_t size);

  std::string directory_;
};

} // namespace aria2

#endif // D_BT_STATE_STORE_H
