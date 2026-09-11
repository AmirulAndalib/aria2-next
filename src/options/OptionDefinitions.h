/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
#ifndef ARIA2_OPTIONS_OPTION_DEFINITIONS_H
#define ARIA2_OPTIONS_OPTION_DEFINITIONS_H
#include "OptionCatalog.h"

namespace aria2::option {
void addGeneralOptions(OptionHandlers& handlers);
void addRpcOptions(OptionHandlers& handlers);
void addStreamOptions(OptionHandlers& handlers);
void addHttpOptions(OptionHandlers& handlers);
void addSftpOptions(OptionHandlers& handlers);
void addProxyOptions(OptionHandlers& handlers);
void addSharingOptions(OptionHandlers& handlers);
void addBitTorrentOptions(OptionHandlers& handlers);
void addMetalinkOptions(OptionHandlers& handlers);
void addHelpOptions(OptionHandlers& handlers);
} // namespace aria2::option
#endif
