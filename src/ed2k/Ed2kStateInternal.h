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
#ifndef D_ED2K_STATE_INTERNAL_H
#define D_ED2K_STATE_INTERNAL_H
#include "Ed2kAttribute.h"

namespace aria2::ed2k_state {
inline bool sameEndpoint(const ed2k::Endpoint& lhs, const ed2k::Endpoint& rhs)
{
  return lhs.host == rhs.host && lhs.port == rhs.port;
}
} // namespace aria2::ed2k_state
#endif // D_ED2K_STATE_INTERNAL_H
