/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
#ifndef D_DASH_FILE_IO_H
#define D_DASH_FILE_IO_H

#include "common.h"
#include <gpac/dash.h>

namespace aria2::media {
class MediaJob;
// GPAC borrows the table and callback strings until it releases each IO handle.
// This adapter therefore outlives the native client owned by its MediaJob.
class DashFileIo {
public:
  explicit DashFileIo(MediaJob& job);
  GF_DASHFileIO* get() { return &native_; }

private:
  GF_DASHFileIO native_{};
};
} // namespace aria2::media

#endif // D_DASH_FILE_IO_H
