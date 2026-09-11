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
#include "Option.h"
#include "BtDownloadImpl.h"
#include "RequestGroup.h"
#include "BtDownload.h"
#include "BtSession.h"
#include "BtSnapshot.h"
#include "Log.h"
#include "RecoverableException.h"
#include "fmt.h"
#include "prefs.h"
#include <libtorrent/alert_types.hpp>
#include <libtorrent/error_code.hpp>
#include <libtorrent/operations.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <utility>
#include "bittorrent/BtSessionInternal.h"

namespace aria2 {
using namespace bt_session;

void BtSession::handleAlert(lt::save_resume_data_alert* saved)
{
  auto download = findDownload(saved->handle);
  if (download) {
    try {
      saveResume(download->impl_->resumePath, saved->params);
    }
    catch (RecoverableException& error) {
      A2_LOG_ERROR_EX("Failed to save BitTorrent resume data", error);
    }
    const bool saveStopState = download->impl_->stopSavePending;
    finishResumeSave(download.get());
    if (saveStopState) {
      download->impl_->stopSavePending = false;
      download->impl_->resumeSaveOutstanding = true;
      auto flags = lt::torrent_handle::save_info_dict;
      if (download->stopReason() == BtDownload::StopReason::Stop) {
        flags |= lt::torrent_handle::flush_disk_cache;
      }
      download->impl_->handle.save_resume_data(flags);
      return;
    }
    if (download->shutdownStage() == BtDownload::ShutdownStage::SavingResume) {
      if (download->stopReason() == BtDownload::StopReason::Pause ||
          download->stopReason() == BtDownload::StopReason::FileSelection) {
        download->finishStopping();
      }
      else {
        const auto managed = impl_->downloads.find(download->impl_->gid);
        if (managed != impl_->downloads.end()) {
          beginNativeDelete(managed->second, DeleteIntent::Permanent);
        }
      }
    }
  }
}

void BtSession::handleAlert(lt::save_resume_data_failed_alert* failed)
{
  auto download = findDownload(failed->handle);
  if (download) {
    if (failed->error != lt::errors::resume_data_not_modified) {
      A2_LOG_ERROR(fmt("Failed to save BitTorrent resume data %s: %s",
                       download->impl_->resumePath.c_str(),
                       failed->error.message().c_str()));
    }
    const bool saveStopState = download->impl_->stopSavePending;
    finishResumeSave(download.get());
    if (saveStopState) {
      download->impl_->stopSavePending = false;
      download->impl_->resumeSaveOutstanding = true;
      auto flags = lt::torrent_handle::save_info_dict;
      if (download->stopReason() == BtDownload::StopReason::Stop) {
        flags |= lt::torrent_handle::flush_disk_cache;
      }
      download->impl_->handle.save_resume_data(flags);
      return;
    }
    if (download->shutdownStage() == BtDownload::ShutdownStage::SavingResume) {
      if (download->stopReason() == BtDownload::StopReason::Pause ||
          download->stopReason() == BtDownload::StopReason::FileSelection) {
        download->finishStopping();
      }
      else {
        const auto managed = impl_->downloads.find(download->impl_->gid);
        if (managed != impl_->downloads.end()) {
          beginNativeDelete(managed->second, DeleteIntent::Permanent);
        }
      }
    }
  }
}

void BtSession::handleAlert(lt::torrent_removed_alert* removed)
{
  auto download = findDownload(removed->handle);
  if (download) {
    if (download->impl_->handle == removed->handle) {
      download->impl_->handle = {};
    }
    impl_->handles.erase(removed->handle);
  }
}

void BtSession::handleAlert(lt::torrent_deleted_alert* deleted)
{
  finishNativeDelete(hashKey(deleted->info_hashes));
}

void BtSession::handleAlert(lt::torrent_delete_failed_alert* failed)
{
  finishNativeDelete(hashKey(failed->info_hashes),
                     failed->error ? failed->error.message() : std::string());
}

void BtSession::handleAlert(lt::file_error_alert* error)
{
  auto download = findDownload(error->handle);
  if (download) {
    BtErrorSnapshot snapshot;
    snapshot.present = true;
    snapshot.recoverable = true;
    snapshot.code = error->error.value();
    snapshot.kind = "storage";
    snapshot.category = error->error.category().name();
    snapshot.message = error->error.message();
    snapshot.operation = lt::operation_name(error->op);
    snapshot.file = error->filename();
    const auto managed = impl_->downloads.find(download->impl_->gid);
    if (managed != impl_->downloads.end() &&
        recoverPartfile(managed->second, snapshot)) {
      A2_LOG_WARN(error->message());
      return;
    }
    failFilePriorityUpdate(download.get());
    snapshot.recoverable = false;
    download->setError(std::move(snapshot));
  }
  A2_LOG_ERROR(error->message());
}

void BtSession::handleAlert(lt::torrent_error_alert* error)
{
  auto download = findDownload(error->handle);
  if (download && download->snapshot().error.kind != "storage") {
    BtErrorSnapshot snapshot;
    snapshot.present = true;
    snapshot.recoverable = false;
    snapshot.code = error->error.value();
    snapshot.kind = "torrent";
    snapshot.category = error->error.category().name();
    snapshot.message = error->error.message();
    download->setError(std::move(snapshot));
  }
  A2_LOG_ERROR(fmt("component=bittorrent event=task_failed gid=%s "
                   "category=%s code=%d file=%s message=%s",
                   gidFor(download).c_str(), error->error.category().name(),
                   error->error.value(),
                   logging::sanitizeText(error->filename()).c_str(),
                   logging::sanitizeText(error->error.message()).c_str()));
}

void BtSession::handleAlert(lt::storage_moved_alert* moved)
{
  auto download = findDownload(moved->handle);
  if (download && download->group()) {
    download->impl_->params.save_path = moved->storage_path();
    download->impl_->previousSavePath.clear();
    requestResumeCheckpoint(download.get(), true);
  }
}

void BtSession::handleAlert(lt::storage_moved_failed_alert* moved)
{
  auto download = findDownload(moved->handle);
  if (download && download->group()) {
    auto& option = download->group()->getOption();
    option->put(PREF_DIR, download->impl_->previousSavePath);
    download->impl_->params.save_path = download->impl_->previousSavePath;
    download->updateFilePaths(download->group()->getDownloadContext(),
                              option.get());
    download->updateSelection(download->group()->getDownloadContext());
    download->impl_->previousSavePath.clear();
  }
  A2_LOG_ERROR(moved->message());
}

void BtSession::handleAlert(lt::file_rename_failed_alert* renamed)
{
  A2_LOG_ERROR(renamed->message());
}

} // namespace aria2
