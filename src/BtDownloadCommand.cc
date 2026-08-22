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
#include "BtDownloadCommand.h"

#include "BtDownload.h"
#include "BtSession.h"
#include "DownloadEngine.h"
#include "Option.h"
#include "RequestGroup.h"
#include "error_code.h"
#include "prefs.h"
#include "util.h"
#include "Notifier.h"
#include "SingletonHolder.h"

namespace aria2 {

BtDownloadCommand::BtDownloadCommand(cuid_t cuid,
                                     std::shared_ptr<BtDownload> download,
                                     BtSession* session, RequestGroup* group,
                                     DownloadEngine* engine)
    : Command(cuid),
      download_(std::move(download)),
      session_(session),
      group_(group),
      engine_(engine)
{
  setStatusRealtime();
  group_->increaseNumCommand();
}

BtDownloadCommand::~BtDownloadCommand() { group_->decreaseNumCommand(); }

bool BtDownloadCommand::execute()
{
  engine_->setRefreshInterval(std::chrono::milliseconds(500));

  if (download_->failed()) {
    group_->setLastErrorCode(error_code::UNKNOWN_ERROR,
                             download_->snapshot().errorMessage.c_str());
    if (download_->stopped()) {
      return true;
    }
    if (download_->recoverableError()) {
      group_->setHaltRequested(true, RequestGroup::NONE);
      group_->setPauseRequested(true);
      session_->requestStop(download_, BtDownload::StopReason::Pause);
    }
    else {
      session_->requestStop(download_, BtDownload::StopReason::Stop);
    }
  }

  if (group_->isHaltRequested() && !download_->stopRequested()) {
    session_->requestStop(download_, group_->isPauseRequested()
                                         ? BtDownload::StopReason::Pause
                                         : BtDownload::StopReason::Stop);
  }

  if (download_->stopped()) {
    return true;
  }

  if (download_->snapshot().finished && !download_->stopRequested() &&
      !seedingStarted_) {
    seedingStarted_ = true;
    group_->enableSeedOnly();
  }

  if (download_->snapshot().finished && !download_->stopRequested() &&
      !completionNotified_) {
    completionNotified_ = true;
    util::executeHookByOptName(group_, group_->getOption().get(),
                               PREF_ON_BT_DOWNLOAD_COMPLETE);
    SingletonHolder<Notifier>::instance()->notifyDownloadEvent(
        EVENT_ON_BT_DOWNLOAD_COMPLETE, group_);
  }

  if (download_->snapshot().finished && !download_->stopRequested()) {
    const auto& snapshot = download_->snapshot();
    bool stop = false;
    if (group_->getOption()->defined(PREF_SEED_TIME)) {
      stop = snapshot.seedingTime >=
             static_cast<int>(group_->getOption()->getAsDouble(PREF_SEED_TIME) *
                              60.0);
    }
    const auto ratio = group_->getOption()->getAsDouble(PREF_SEED_RATIO);
    if (ratio > 0.0 && snapshot.completedLength > 0) {
      stop = stop || static_cast<double>(snapshot.allTimeUpload) /
                             snapshot.completedLength >=
                         ratio;
    }
    if (stop) {
      session_->requestStop(download_, BtDownload::StopReason::Stop);
    }
  }

  engine_->addCommand(std::unique_ptr<Command>(this));
  return false;
}

} // namespace aria2
