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
#ifndef D_ED2K_LINK_H
#define D_ED2K_LINK_H

#include "common.h"

#include <cstdint>
#include <string>
#include <vector>

namespace aria2 {

namespace ed2k {

constexpr uint16_t SOURCE_CRYPT_SUPPORT = 0x01;
constexpr uint16_t SOURCE_CRYPT_REQUEST = 0x02;
constexpr uint16_t SOURCE_CRYPT_REQUIRE = 0x04;
constexpr uint16_t SOURCE_CRYPT_DIRECT_CALLBACK = 0x08;
constexpr uint16_t SOURCE_CRYPT_HAS_USER_HASH = 0x80;

enum class LinkType {
  FILE,
  SERVER,
  SERVER_LIST,
  NODES_LIST,
  SEARCH,
};

struct Endpoint {
  std::string host;
  uint16_t port = 0;
  uint16_t cryptOptions = 0;
  std::string userHash;
};

struct FoundSource {
  Endpoint endpoint;
  uint32_t clientId = 0;
  bool lowId = false;
};

struct Link {
  LinkType type = LinkType::FILE;
  std::string name;
  int64_t size = 0;
  std::string hash;
  std::vector<std::string> pieceHashes;
  std::string aichHash;
  std::vector<Endpoint> sources;
  Endpoint server;
  std::string url;
};

Link parseLink(const std::string& uri);
Endpoint parseEndpoint(const std::string& value);
std::string toFileLink(const Link& link);

} // namespace ed2k

} // namespace aria2

#endif // D_ED2K_LINK_H
