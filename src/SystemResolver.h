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
#ifndef D_SYSTEM_RESOLVER_H
#define D_SYSTEM_RESOLVER_H

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace aria2 {

class SystemResolver {
public:
  using RequestId = uint64_t;

  enum class Status { Pending, Success, Error };

  SystemResolver();
  ~SystemResolver();

  SystemResolver(const SystemResolver&) = delete;
  SystemResolver& operator=(const SystemResolver&) = delete;

  RequestId resolve(const std::string& hostname, uint16_t port,
                    bool allowIPv6, std::chrono::seconds timeout);
  Status take(RequestId id, std::vector<std::string>& addresses,
              std::string& error);
  void cancel(RequestId id);
  bool poll();
  bool hasPending() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace aria2

#endif // D_SYSTEM_RESOLVER_H
