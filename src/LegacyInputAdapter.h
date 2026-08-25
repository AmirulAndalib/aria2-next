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
#ifndef D_LEGACY_INPUT_ADAPTER_H
#define D_LEGACY_INPUT_ADAPTER_H

#include <string>
#include <vector>

#include <aria2/aria2.h>

namespace aria2 {

class Option;

enum class LegacyInputSource {
  CommandLine,
  Configuration,
  Session,
  Rpc,
  Library,
};

bool isLegacyInputOption(const std::string& name);

KeyVals normalizeLegacyInput(const KeyVals& options, LegacyInputSource source);

std::vector<std::string> normalizeLegacyCommandLine(int argc, char* argv[],
                                                    LegacyInputSource source);

bool projectLegacyOption(const Option* option, const std::string& name,
                         std::string& value);

KeyVals projectLegacyOptions(const Option* option);

} // namespace aria2

#endif // D_LEGACY_INPUT_ADAPTER_H
