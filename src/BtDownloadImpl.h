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

#include <cstdint>
#include <string>
#include <vector>

#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/torrent_handle.hpp>

#include "BtDownload.h"
#include "TimerA2.h"

namespace aria2 {

enum class BtTrackerOrigin { Metainfo, Magnet, Resume, Global, Rpc };

struct BtTrackerSpec {
  std::string url;
  int tier = 0;
  BtTrackerOrigin origin = BtTrackerOrigin::Metainfo;

  bool operator==(const BtTrackerSpec& other) const
  {
    return url == other.url && tier == other.tier && origin == other.origin;
  }
};

struct BtDownload::Impl {
  libtorrent::add_torrent_params params;
  libtorrent::torrent_handle handle;
  std::vector<BtTrackerSpec> sourceTrackers;
  std::vector<BtTrackerSpec> effectiveTrackers;
  bool trackerOverride = false;
  uint64_t trackerRevision = 1;
  uint64_t appliedTrackerRevision = 0;
  std::string resumePath;
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
