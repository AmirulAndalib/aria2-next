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
#include "ContextAttribute.h"
#include "aria2/aria2.h"
#include <cstddef>
#include <libtorrent/units.hpp>
#include <memory>
#include <utility>
#include <vector>
#include "BtDownload.h"
#include "BtDownloadImpl.h"
#include "bittorrent/BtDownloadSupport.h"
#include "BtMetadata.h"
#include "DownloadContext.h"
#include "FileEntry.h"
#include "RequestGroup.h"
#include "Option.h"
#include "DlAbortEx.h"
#include "support/Numbers.h"
#include "support/FilePath.h"
#include "prefs.h"
#include "fmt.h"
#include <libtorrent/torrent_info.hpp>
#include <sstream>

namespace aria2 {

namespace lt = libtorrent;
using namespace bt_download;

void BtDownload::populateDownloadContext(
    const std::shared_ptr<DownloadContext>& context, const Option* option)
{
  auto attrs = std::make_shared<BtMetadata>();
  assignHashes(attrs.get(), snapshot_, impl_->params.info_hashes);
  attrs->announceList = announceList(impl_->params);
  attrs->name = snapshot_.name;

  if (!impl_->params.ti) {
    context->markTotalLengthIsUnknown();
    auto name = impl_->params.name;
    if (name.empty()) {
      name = !snapshot_.infoHashV1.empty() ? snapshot_.infoHashV1
                                           : snapshot_.infoHashV2;
    }
    context->getFirstFileEntry()->setPath(
        util::applyDir(option->get(PREF_DIR), name));
    context->setAttribute(CTX_ATTR_BT, std::move(attrs));
    context->setAcceptMetalink(false);
    return;
  }

  const auto& info = *impl_->params.ti;
  const auto& files = info.layout();
  std::vector<std::shared_ptr<FileEntry>> entries;
  entries.reserve(static_cast<size_t>(files.num_files()));
  for (lt::file_index_t index{0}; index < files.end_file(); ++index) {
    entries.push_back(std::make_shared<FileEntry>(
        util::applyDir(option->get(PREF_DIR), files.file_path(index)),
        files.file_size(index), files.file_offset(index)));
  }
  context->setFileEntries(entries.begin(), entries.end());
  context->setPieceLength(info.piece_length());
  context->markTotalLengthIsKnown();
  context->setBasePath(util::applyDir(option->get(PREF_DIR), info.name()));

  attrs->name = info.name();
  attrs->mode =
      files.num_files() > 1 ? BT_FILE_MODE_MULTI : BT_FILE_MODE_SINGLE;
  attrs->privateTorrent = info.priv();
  attrs->creationDate = impl_->params.creation_date;
  attrs->comment = impl_->params.comment;
  attrs->createdBy = impl_->params.created_by;
  snapshot_.name = attrs->name;
  snapshot_.privateTorrent = attrs->privateTorrent;
  snapshot_.hasMetadata = true;
  snapshot_.totalLength = context->getTotalLength();
  snapshot_.files.clear();
  for (const auto& entry : entries) {
    snapshot_.files.push_back(
        {entry->getPath(), entry->getLength(), 0, 1, entry->isRequested()});
  }
  context->setAttribute(CTX_ATTR_BT, std::move(attrs));
  context->setAcceptMetalink(false);
}

void BtDownload::updateFilePaths(
    const std::shared_ptr<DownloadContext>& context, const Option* option) const
{
  if (!impl_->params.ti) {
    return;
  }
  context->setBasePath(
      util::applyDir(option->get(PREF_DIR), impl_->params.ti->name()));
  const auto& files = impl_->params.ti->layout();
  for (lt::file_index_t index{0}; index < files.end_file(); ++index) {
    context->setFilePathWithIndex(
        static_cast<size_t>(static_cast<int>(index)) + 1,
        util::applyDir(option->get(PREF_DIR), files.file_path(index)));
  }
  std::istringstream indexOut(option->get(PREF_INDEX_OUT));
  for (const auto& entry : util::createIndexPaths(indexOut)) {
    context->setFilePathWithIndex(
        entry.first, util::applyDir(option->get(PREF_DIR), entry.second));
  }
}

bool BtDownload::awaitingFileSelection() const
{
  return snapshot_.fileSelectionState ==
         BtSnapshot::FileSelectionState::Awaiting;
}

bool BtDownload::fileSelectionReady() const
{
  return snapshot_.fileSelectionState == BtSnapshot::FileSelectionState::Ready;
}

bool BtDownload::fileSelectionApplying() const
{
  return snapshot_.fileSelectionState ==
         BtSnapshot::FileSelectionState::Applying;
}

bool BtDownload::shouldPauseAfterMetadata() const
{
  return source_ == Source::Magnet &&
         snapshot_.fileSelectionState == BtSnapshot::FileSelectionState::None &&
         group_ && group_->getOption()->getAsBool(PREF_ENABLE_RPC) &&
         group_->getOption()->getAsBool(PREF_PAUSE_METADATA);
}

std::string BtDownload::fileSelectionError(const Option* option) const
{
  if (!snapshot_.hasMetadata || snapshot_.files.empty()) {
    return "BitTorrent metadata is unavailable for file selection";
  }
  if (!option || !option->defined(PREF_SELECT_FILE) ||
      option->blank(PREF_SELECT_FILE)) {
    return "BitTorrent file selection requires select-file";
  }
  auto selected = util::parseIntSegments(option->get(PREF_SELECT_FILE));
  selected.normalize();
  if (!selected.hasNext()) {
    return "BitTorrent file selection requires select-file";
  }
  while (selected.hasNext()) {
    const auto index = selected.next();
    if (index < 1 || static_cast<size_t>(index) > snapshot_.files.size()) {
      return fmt("BitTorrent select-file index %d is out of range", index);
    }
  }
  return {};
}

void BtDownload::validateFileSelection(const Option* option) const
{
  const auto error = fileSelectionError(option);
  if (!error.empty()) {
    throw DL_ABORT_EX(error);
  }
}

void BtDownload::beginFileSelectionPause()
{
  stopReason_ = StopReason::FileSelection;
  beginSelectionProgressHold();
  if (fileSelectionError(group_ ? group_->getOption().get() : nullptr)
          .empty()) {
    snapshot_.fileSelectionState = BtSnapshot::FileSelectionState::Ready;
  }
  else {
    snapshot_.fileSelectionState = BtSnapshot::FileSelectionState::Awaiting;
  }
  snapshot_.state = BtSnapshot::State::Paused;
  snapshot_.progressPpm = 0;
}

void BtDownload::submitFileSelection(const Option* option)
{
  validateFileSelection(option);
  if (snapshot_.fileSelectionState ==
          BtSnapshot::FileSelectionState::Awaiting ||
      snapshot_.fileSelectionState == BtSnapshot::FileSelectionState::Ready) {
    snapshot_.fileSelectionState = BtSnapshot::FileSelectionState::Ready;
    snapshot_.state = snapshot_.error.present ? BtSnapshot::State::Error
                                              : BtSnapshot::State::Paused;
    snapshot_.selectedComplete = false;
  }
}

void BtDownload::beginFileSelectionApply()
{
  if (awaitingFileSelection()) {
    throw DL_ABORT_EX(
        "BitTorrent download is awaiting a valid select-file option");
  }
  if (!fileSelectionReady() || !group_) {
    return;
  }
  validateFileSelection(group_->getOption().get());
  group_->getOption()->put(PREF_PAUSE_METADATA, A2_V_FALSE);
  beginSelectionProgressHold();
  snapshot_.fileSelectionState = BtSnapshot::FileSelectionState::Applying;
  snapshot_.state = BtSnapshot::State::Adding;
}

void BtDownload::completeFileSelectionApply()
{
  if (fileSelectionApplying()) {
    snapshot_.fileSelectionState = BtSnapshot::FileSelectionState::None;
    if (snapshot_.state == BtSnapshot::State::Paused) {
      snapshot_.state = BtSnapshot::State::Adding;
    }
  }
}

void BtDownload::failFileSelectionApply()
{
  if (fileSelectionApplying()) {
    progressState_ = ProgressState::Stable;
    snapshot_.fileSelectionState = BtSnapshot::FileSelectionState::Ready;
    snapshot_.state = BtSnapshot::State::Paused;
    snapshot_.selectedComplete = false;
  }
}

} // namespace aria2
