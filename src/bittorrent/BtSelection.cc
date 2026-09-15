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
#include "BtDownloadImpl.h"
#include "DownloadContext.h"
#include "Option.h"
#include "BtDownload.h"
#include "BtSession.h"
#include "BtSettings.h"
#include "RequestGroup.h"
#include "prefs.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <libtorrent/download_priority.hpp>
#include <libtorrent/torrent_flags.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <utility>
#include <vector>
#include "support/Numbers.h"
#include "bittorrent/BtSessionInternal.h"

namespace aria2 {
namespace bt_session {
std::vector<lt::download_priority_t> makeFilePriorities(RequestGroup* group)
{
  std::vector<lt::download_priority_t> priorities;
  if (!group) {
    return priorities;
  }
  const auto& files = group->getDownloadContext()->getFileEntries();
  priorities.reserve(files.size());
  for (const auto& file : files) {
    priorities.push_back(file->isRequested() ? lt::default_priority
                                             : lt::dont_download);
  }

  const auto& specification = group->getOption()->get(PREF_BT_FILE_PRIORITY);
  if (specification.empty()) {
    return priorities;
  }
  applyBtFilePrioritySpec(priorities, specification);
  for (size_t i = 0; i < priorities.size(); ++i) {
    files[i]->setRequested(priorities[i] != lt::dont_download);
  }
  return priorities;
}

SelectionPlan makeSelectionPlan(const lt::torrent_handle& handle,
                                RequestGroup* group,
                                bool restoreNativePiecePriorities)
{
  SelectionPlan plan;
  if (!group) {
    return plan;
  }
  group->getBtDownload()->updateSelection(group->getDownloadContext());
  if (!handle.is_valid()) {
    return plan;
  }
  const auto info = handle.torrent_file();
  if (!info || group->getDownloadContext()->getFileEntries().size() !=
                   static_cast<size_t>(info->layout().num_files())) {
    return plan;
  }
  plan.files = makeFilePriorities(group);
  const auto& savePath = group->getOption()->get(PREF_DIR);
  size_t index = 0;
  for (const auto& file : group->getDownloadContext()->getFileEntries()) {
    if (index < static_cast<size_t>(info->layout().num_files())) {
      const auto nativeIndex = lt::file_index_t{static_cast<int>(index)};
      if (info->layout().pad_file_at(nativeIndex)) {
        plan.files[index] = lt::dont_download;
        file->setRequested(false);
      }
      auto storagePath = file->getPath();
      if (!savePath.empty() && storagePath.size() > savePath.size() &&
          storagePath.compare(0, savePath.size(), savePath) == 0 &&
          (storagePath[savePath.size()] == '/' ||
           storagePath[savePath.size()] == '\\')) {
        storagePath.erase(0, savePath.size() + 1);
      }
      if (storagePath != info->layout().file_path(nativeIndex)) {
        handle.rename_file(nativeIndex, storagePath);
      }
    }
    ++index;
  }
  group->getBtDownload()->updateSelection(group->getDownloadContext());
  auto& snapshotFiles = group->getBtDownload()->mutableSnapshot().files;
  const auto snapshotCount = std::min(snapshotFiles.size(), plan.files.size());
  for (size_t i = 0; i < snapshotCount; ++i) {
    snapshotFiles[i].priority =
        static_cast<int>(static_cast<uint8_t>(plan.files[i]));
  }

  const bool prioritizeBoundaries =
      group->getOption()->getAsBool(PREF_BT_FIRST_LAST_PIECE_FIRST);
  if (!prioritizeBoundaries && !restoreNativePiecePriorities) {
    return plan;
  }
  plan.pieceOverrides = prioritizeBoundaries;

  plan.pieces.assign(static_cast<size_t>(info->num_pieces()),
                     lt::dont_download);
  const auto& files = info->layout();
  const auto pieceLength = std::max(1, info->piece_length());
  index = 0;
  for (const auto& file : group->getDownloadContext()->getFileEntries()) {
    if (index >= static_cast<size_t>(files.num_files())) {
      break;
    }
    const auto nativeIndex = lt::file_index_t{static_cast<int>(index)};
    const auto fileSize = files.file_size(nativeIndex);
    if (!file->isRequested() || fileSize <= 0) {
      ++index;
      continue;
    }
    const auto first = info->map_file(nativeIndex, 0, 1).piece;
    const auto last = info->map_file(nativeIndex, fileSize - 1, 1).piece;
    for (int piece = static_cast<int>(first); piece <= static_cast<int>(last);
         ++piece) {
      plan.pieces[static_cast<size_t>(piece)] = plan.files[index];
    }
    const auto boundaryPieces = std::max<int64_t>(
        1, static_cast<int64_t>(std::ceil(static_cast<long double>(fileSize) *
                                          0.01L / pieceLength)));
    for (int64_t offset = 0; offset < boundaryPieces; ++offset) {
      const auto front = static_cast<int64_t>(static_cast<int>(first)) + offset;
      const auto back = static_cast<int64_t>(static_cast<int>(last)) - offset;
      if (prioritizeBoundaries && front <= static_cast<int>(last)) {
        plan.pieces[static_cast<size_t>(front)] = lt::top_priority;
      }
      if (prioritizeBoundaries && back >= static_cast<int>(first)) {
        plan.pieces[static_cast<size_t>(back)] = lt::top_priority;
      }
    }
    ++index;
  }
  return plan;
}

void updateDownloadContext(BtDownload* download, RequestGroup* group)
{
  download->populateDownloadContext(group->getDownloadContext(),
                                    group->getOption().get());
  auto selected =
      util::parseIntSegments(group->getOption()->get(PREF_SELECT_FILE));
  selected.normalize();
  group->getDownloadContext()->setFileFilter(std::move(selected));
  download->updateFilePaths(group->getDownloadContext(),
                            group->getOption().get());
  download->updateSelection(group->getDownloadContext());
}
} // namespace bt_session
using namespace bt_session;

bool BtSession::synchronizeSelection(BtDownload* download)
{
  if (!download || !download->group() || !download->impl_->handle.is_valid() ||
      !download->impl_->handle.in_session()) {
    return false;
  }

  auto plan =
      makeSelectionPlan(download->impl_->handle, download->group(),
                        !download->impl_->appliedPiecePriorities.empty());
  download->impl_->desiredFilePriorities = std::move(plan.files);
  download->impl_->desiredPiecePriorities = std::move(plan.pieces);

  if (download->impl_->desiredFilePriorities.empty()) {
    return false;
  }

  if (download->impl_->filePriorityUpdatePending) {
    return true;
  }
  if (!download->impl_->desiredFilePriorities.empty() &&
      download->impl_->desiredFilePriorities !=
          download->impl_->appliedFilePriorities) {
    download->impl_->filePriorityUpdatePending = true;
    download->impl_->handle.prioritize_files(
        download->impl_->desiredFilePriorities);
    return true;
  }
  if (download->impl_->desiredPiecePriorities !=
      download->impl_->appliedPiecePriorities) {
    download->impl_->handle.prioritize_pieces(
        download->impl_->desiredPiecePriorities);
    if (plan.pieceOverrides) {
      download->impl_->appliedPiecePriorities =
          download->impl_->desiredPiecePriorities;
    }
    else {
      download->impl_->appliedPiecePriorities.clear();
    }
  }
  return false;
}

void BtSession::finishFilePriorityUpdate(BtDownload* download)
{
  if (!download || !download->impl_->filePriorityUpdatePending) {
    return;
  }
  download->impl_->filePriorityUpdatePending = false;
  download->impl_->appliedFilePriorities =
      download->impl_->handle.get_file_priorities();
  download->impl_->appliedPiecePriorities.clear();
  continueSelectionSynchronization(download);
}

void BtSession::continueSelectionSynchronization(BtDownload* download)
{
  if (synchronizeSelection(download)) {
    return;
  }
  requestResumeCheckpoint(download);
  if (download->impl_->resumeAfterFilePriorityUpdate) {
    download->impl_->resumeAfterFilePriorityUpdate = false;
    const bool selectionApplying = download->fileSelectionApplying();
    download->completeFileSelectionApply();
    if (selectionApplying) {
      requestProgressRefresh(download);
    }
    if (download->group() && !download->group()->isHaltRequested() &&
        download->shutdownStage() == BtDownload::ShutdownStage::Idle) {
      resumeTorrent(download);
    }
  }
}

void BtSession::failFilePriorityUpdate(BtDownload* download)
{
  if (!download || !download->impl_->filePriorityUpdatePending) {
    return;
  }
  download->impl_->filePriorityUpdatePending = false;
  download->impl_->resumeAfterFilePriorityUpdate = false;
  download->impl_->appliedFilePriorities.clear();
  download->impl_->appliedPiecePriorities.clear();
  download->failFileSelectionApply();
}

void BtSession::requestProgressRefresh(BtDownload* download)
{
  if (!download || !download->impl_->handle.is_valid() ||
      !download->impl_->handle.in_session()) {
    return;
  }
  download->beginProgressRefresh();
  download->impl_->handle.post_status(lt::torrent_handle::query_pieces |
                                      lt::torrent_handle::query_name |
                                      lt::torrent_handle::query_save_path);
  download->impl_->handle.post_file_progress({});
}

void BtSession::resumeTorrent(BtDownload* download)
{
  if (!download || !download->group() || !download->impl_->handle.is_valid()) {
    return;
  }
  if (download->group()->getOption()->getAsBool(PREF_CHECK_INTEGRITY) &&
      !download->impl_->initialRecheckStarted) {
    download->impl_->initialRecheckStarted = true;
    download->impl_->handle.force_recheck();
  }
  download->impl_->handle.unset_flags(lt::torrent_flags::auto_managed |
                                      lt::torrent_flags::stop_when_ready);
  download->impl_->handle.resume();
}

} // namespace aria2
