/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
#ifndef D_MUX_TIMELINE_H
#define D_MUX_TIMELINE_H

#include "MuxInput.h"

namespace aria2::media::muxing {
// Keep native source clocks and align selected tracks to the same recording
// window. Presentation boundaries are milliseconds; packet offsets are us.
void alignHls(Inputs&, int64_t presentationStart, int64_t nextBoundary,
              int64_t& liveOrigin, bool firstEpoch, bool live,
              const std::string& format);
} // namespace aria2::media::muxing

#endif // D_MUX_TIMELINE_H
