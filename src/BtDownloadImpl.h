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
#ifndef D_BT_DOWNLOAD_IMPL_H
#define D_BT_DOWNLOAD_IMPL_H

#include <string>
#include <vector>

#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/torrent_handle.hpp>

#include "BtDownload.h"
#include "TimerA2.h"

namespace aria2 {

struct BtDownload::Impl {
  libtorrent::add_torrent_params params;
  libtorrent::torrent_handle handle;
  std::vector<std::string> sourceTrackers;
  std::vector<int> sourceTrackerTiers;
  std::string resumePath;
  std::string pendingSavePath;
  std::string previousSavePath;
  bool resumeLoaded = false;
  bool resumeSaveOutstanding = false;
  bool checkpointPending = false;
  bool stopSavePending = false;
  bool initialRecheckStarted = false;
  bool resumeAfterFilePriority = false;
  bool fileSelectionResumePending = false;
  Timer lastResumeSave = Timer::zero();
  Timer lastTrackerUpdate = Timer::zero();
};

} // namespace aria2

#endif // D_BT_DOWNLOAD_IMPL_H
