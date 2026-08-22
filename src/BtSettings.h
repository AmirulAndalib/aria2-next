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
#ifndef D_BT_SETTINGS_H
#define D_BT_SETTINGS_H

#include <libtorrent/settings_pack.hpp>

namespace aria2 {

class Option;

libtorrent::settings_pack makeBtSettings(const Option* option);

} // namespace aria2

#endif // D_BT_SETTINGS_H
