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
#include "FileEntry.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>
#include "BtDownload.h"
#include "BtDownloadImpl.h"
#include "bittorrent/BtDownloadSupport.h"
#include "DownloadContext.h"
#include "RequestGroup.h"
#include <libtorrent/torrent_info.hpp>
#include <algorithm>
#include <limits>

namespace aria2 {

namespace lt = libtorrent;
using namespace bt_download;

void BtDownload::updateSelection(
    const std::shared_ptr<DownloadContext>& context)
{
  const auto& files = context->getFileEntries();
  const auto count = std::min(files.size(), snapshot_.files.size());
  int64_t selectedLength = 0;
  int64_t selectedCompletedLength = 0;
  const auto addLength = [](int64_t current, int64_t value) {
    value = std::max<int64_t>(0, value);
    return current > std::numeric_limits<int64_t>::max() - value
               ? std::numeric_limits<int64_t>::max()
               : current + value;
  };
  for (size_t i = 0; i < count; ++i) {
    snapshot_.files[i].selected = files[i]->isRequested();
    snapshot_.files[i].path = files[i]->getPath();
    if (snapshot_.files[i].selected) {
      selectedLength = addLength(selectedLength, snapshot_.files[i].length);
      selectedCompletedLength = addLength(
          selectedCompletedLength, std::min(snapshot_.files[i].completedLength,
                                            snapshot_.files[i].length));
    }
  }
  snapshot_.totalLength = selectedLength;
  snapshot_.completedLength = selectedCompletedLength;
  snapshot_.progressPpm =
      selectedLength > 0
          ? static_cast<int>(std::min<long double>(
                1000000.0L, static_cast<long double>(selectedCompletedLength) *
                                1000000.0L / selectedLength))
          : 0;
}

void BtDownload::restoreResumeProgress()
{
  if (!impl_->params.ti || impl_->params.have_pieces.empty() ||
      snapshot_.files.empty()) {
    return;
  }

  std::vector<int64_t> completed(snapshot_.files.size(), 0);
  const auto& info = *impl_->params.ti;
  const auto pieceCount =
      std::min(impl_->params.have_pieces.size(), info.num_pieces());
  for (int index = 0; index < pieceCount; ++index) {
    const auto piece = lt::piece_index_t{index};
    if (!impl_->params.have_pieces[piece]) {
      continue;
    }
    for (const auto& slice : info.map_block(piece, 0, info.piece_size(piece))) {
      const auto file = static_cast<size_t>(static_cast<int>(slice.file_index));
      if (file >= completed.size()) {
        continue;
      }
      const auto remaining =
          std::max<int64_t>(0, snapshot_.files[file].length - completed[file]);
      completed[file] += std::min<int64_t>(remaining, slice.size);
    }
  }
  for (size_t index = 0; index < completed.size(); ++index) {
    snapshot_.files[index].completedLength = completed[index];
  }
  updateSelection(group_->getDownloadContext());
  nativeFinished_ = snapshot_.hasMetadata &&
                    snapshot_.completedLength == snapshot_.totalLength;
  snapshot_.selectedComplete = nativeFinished_;
  snapshot_.complete =
      hasAllPieces(impl_->params.have_pieces, info.num_pieces());
}

void BtDownload::refreshLogicalProgress()
{
  if (!group_) {
    return;
  }
  updateSelection(group_->getDownloadContext());
  snapshot_.selectedComplete =
      progressState_ == ProgressState::Stable && nativeFinished_ &&
      snapshot_.fileSelectionState == BtSnapshot::FileSelectionState::None &&
      !snapshot_.error.present && snapshot_.totalLength > 0 &&
      snapshot_.completedLength == snapshot_.totalLength;
}

void BtDownload::beginProgressVerification()
{
  progressState_ = ProgressState::Verifying;
  nativeFinished_ = false;
  snapshot_.selectedComplete = false;
}

void BtDownload::beginSelectionProgressHold()
{
  progressState_ = ProgressState::Selecting;
  nativeFinished_ = false;
  snapshot_.selectedComplete = false;
  snapshot_.complete = false;
}

void BtDownload::beginProgressRefresh()
{
  progressState_ = ProgressState::Refreshing;
  snapshot_.selectedComplete = false;
}

void BtDownload::applyNativeCompletion(bool finished, bool seeding)
{
  nativeFinished_ = finished;
  snapshot_.complete = seeding;
  refreshLogicalProgress();
}

void BtDownload::applyFileProgress(const std::vector<int64_t>& completedLengths)
{
  if (progressState_ == ProgressState::Verifying ||
      progressState_ == ProgressState::Selecting) {
    return;
  }

  const auto count = std::min(snapshot_.files.size(), completedLengths.size());
  for (size_t i = 0; i < count; ++i) {
    snapshot_.files[i].completedLength =
        std::clamp<int64_t>(completedLengths[i], 0, snapshot_.files[i].length);
  }
  progressState_ = ProgressState::Stable;
  refreshLogicalProgress();
}

} // namespace aria2
