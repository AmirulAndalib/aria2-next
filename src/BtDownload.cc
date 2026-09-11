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
#include "BtDownload.h"
#include <string>
#include <utility>
#include "BtDownloadImpl.h"
#include "RequestGroup.h"
#include "DlAbortEx.h"

namespace aria2 {

namespace lt = libtorrent;

bool BtDownload::hasMetadata() const { return snapshot_.hasMetadata; }

bool BtDownload::active() const
{
  return shutdownStage_ != ShutdownStage::Complete && !failed();
}

bool BtDownload::stopped() const
{
  return shutdownStage_ == ShutdownStage::Complete;
}

bool BtDownload::failed() const { return snapshot_.error.present; }

void BtDownload::requestStop(StopReason reason)
{
  if (shutdownStage_ != ShutdownStage::Idle) {
    if (reason == StopReason::Stop) {
      stopReason_ = reason;
      snapshot_.state = BtSnapshot::State::Stopping;
      return;
    }
    if (reason == StopReason::FileSelection &&
        stopReason_ == StopReason::Pause) {
      stopReason_ = reason;
      snapshot_.state = BtSnapshot::State::Stopping;
    }
    return;
  }
  stopReason_ = reason;
  shutdownStage_ = ShutdownStage::PendingHandle;
  snapshot_.state = BtSnapshot::State::Stopping;
}

void BtDownload::beginSavingResume()
{
  shutdownStage_ = ShutdownStage::SavingResume;
}

void BtDownload::beginRemoving() { shutdownStage_ = ShutdownStage::Removing; }

void BtDownload::finishStopping()
{
  shutdownStage_ = ShutdownStage::Complete;
  switch (stopReason_) {
  case StopReason::Pause:
    snapshot_.state = BtSnapshot::State::Paused;
    break;
  case StopReason::FileSelection:
    snapshot_.state = BtSnapshot::State::Paused;
    break;
  case StopReason::None:
  case StopReason::Stop:
    snapshot_.state = BtSnapshot::State::Stopped;
    break;
  }
}

void BtDownload::applyTransportState(BtSnapshot::State state)
{
  if (snapshot_.error.present) {
    snapshot_.state = BtSnapshot::State::Error;
    return;
  }
  if (impl_->recheckAfterAdd &&
      (impl_->nativeState == BtNativeState::Adding ||
       impl_->nativeState == BtNativeState::Removing)) {
    snapshot_.state = BtSnapshot::State::Recovering;
    return;
  }
  if ((group_ && group_->isPauseRequested()) || awaitingFileSelection() ||
      fileSelectionReady()) {
    snapshot_.state = BtSnapshot::State::Paused;
    return;
  }
  if (shutdownStage_ != ShutdownStage::Idle) {
    return;
  }
  snapshot_.state = state;
}

void BtDownload::setError(std::string message)
{
  BtErrorSnapshot error;
  error.present = true;
  error.kind = "engine";
  error.category = "aria2";
  error.message = std::move(message);
  setError(std::move(error));
}

void BtDownload::setError(BtErrorSnapshot error)
{
  error.present = true;
  snapshot_.error = std::move(error);
  snapshot_.state = BtSnapshot::State::Error;
  snapshot_.selectedComplete = false;
}

void BtDownload::clearError() { snapshot_.error = {}; }

bool BtDownload::takeCompletionNotification()
{
  if (!snapshot_.selectedComplete || completionNotified_) {
    return false;
  }
  completionNotified_ = true;
  return true;
}

void BtDownload::prepareStart()
{
  if (awaitingFileSelection()) {
    throw DL_ABORT_EX(
        "BitTorrent download is awaiting a valid select-file option");
  }
  if (fileSelectionReady()) {
    throw DL_ABORT_EX("BitTorrent file selection has not been resumed");
  }
  stopReason_ = StopReason::None;
  shutdownStage_ = ShutdownStage::Idle;
  impl_->payloadDownloaded = 0;
  impl_->payloadUploaded = 0;
  clearError();
  if (!fileSelectionApplying()) {
    snapshot_.fileSelectionState = BtSnapshot::FileSelectionState::None;
  }
  snapshot_.state = snapshot_.hasMetadata
                        ? BtSnapshot::State::Adding
                        : BtSnapshot::State::DownloadingMetadata;
}

} // namespace aria2
