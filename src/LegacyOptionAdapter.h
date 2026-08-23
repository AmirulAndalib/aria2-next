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
#ifndef D_LEGACY_OPTION_ADAPTER_H
#define D_LEGACY_OPTION_ADAPTER_H

#include <string>
#include <vector>

#include <aria2/aria2.h>

namespace aria2 {

class Option;

enum class LegacyOptionSource {
  CommandLine,
  Configuration,
  Session,
  Rpc,
  Library,
};

bool isLegacyOption(const std::string& name);

KeyVals adaptLegacyOptions(const KeyVals& options, LegacyOptionSource source);

std::vector<std::string> adaptLegacyCommandLine(int argc, char* argv[],
                                                LegacyOptionSource source);

bool projectLegacyOption(const Option* option, const std::string& name,
                         std::string& value);

KeyVals projectLegacyOptions(const Option* option);

} // namespace aria2

#endif // D_LEGACY_OPTION_ADAPTER_H
