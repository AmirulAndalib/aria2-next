/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
#ifndef ARIA2_OPTIONS_OPTION_CATALOG_H
#define ARIA2_OPTIONS_OPTION_CATALOG_H
#include "OptionHandler.h"
#include <memory>
#include <vector>

namespace aria2 {
using OptionHandlers = std::vector<std::unique_ptr<OptionHandler>>;
namespace option {
// Creates the canonical option catalogue. The parser takes ownership and
// indexes entries by Pref ID, preserving CLI defaults and mutation permissions.
OptionHandlers createHandlers();
} // namespace option
} // namespace aria2
#endif
