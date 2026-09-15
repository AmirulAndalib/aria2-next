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
#include "RequestGroup.h"
#include <libtorrent/session.hpp>
#include "BtDownload.h"
#include "BtDownloadImpl.h"
#include "BtSession.h"
#include "GroupId.h"
#include "Log.h"
#include "RecoverableException.h"
#include "fmt.h"
#include <algorithm>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/torrent_status.hpp>
#include <memory>
#include "bittorrent/BtSessionInternal.h"

namespace aria2 {
using namespace bt_session;

void BtSession::handleAlert(lt::add_torrent_alert* added)
{
  std::shared_ptr<BtDownload> download;
  const auto addedKey = hashKey(added->params.info_hashes);
  for (const auto& entry : impl_->downloads) {
    if (hashKey(entry.second->impl_->params.info_hashes) == addedKey) {
      download = entry.second;
      break;
    }
  }
  if (!download) {
    if (!added->error && added->handle.is_valid()) {
      impl_->session->remove_torrent(added->handle,
                                     lt::session::delete_partfile);
    }
    return;
  }
  if (added->error) {
    A2_LOG_ERROR(fmt("component=bittorrent event=task_add_failed gid=%s "
                     "category=%s code=%d message=%s",
                     GroupId::toHex(download->group()->getGID()).c_str(),
                     added->error.category().name(), added->error.value(),
                     logging::sanitizeText(added->error.message()).c_str()));
    download->impl_->nativeState = BtNativeState::Detached;
    download->setError(added->error.message());
    forgetHandles(download.get());
    impl_->downloads.erase(download->impl_->gid);
    download->shutdownStage_ = BtDownload::ShutdownStage::Complete;
    return;
  }
  const bool removalPending =
      download->impl_->nativeState == BtNativeState::Removing;
  download->impl_->handle = added->handle;
  download->impl_->nativeState = BtNativeState::Attached;
  download->impl_->appliedTrackerRevision = download->impl_->trackerRevision;
  impl_->handles[added->handle] = download;
  A2_LOG_INFO(fmt("component=bittorrent event=task_attached gid=%s "
                  "metadata=%s",
                  GroupId::toHex(download->group()->getGID()).c_str(),
                  download->hasMetadata() ? "ready" : "pending"));
  if (removalPending) {
    const auto managed = impl_->downloads.find(download->impl_->gid);
    if (managed != impl_->downloads.end()) {
      beginNativeDelete(managed->second, DeleteIntent::Permanent);
    }
    return;
  }
  if (download->stopRequested()) {
    const auto managed = impl_->downloads.find(download->impl_->gid);
    if (managed != impl_->downloads.end()) {
      requestStop(managed->second, download->stopReason());
    }
  }
  else if (download->impl_->recheckAfterAdd) {
    download->impl_->initialRecheckStarted = true;
    download->beginProgressVerification();
    download->snapshot_.state = BtSnapshot::State::Recovering;
    download->impl_->handle.force_recheck();
  }
  else {
    bool filePriorityUpdatePending = false;
    if (!download->awaitingFileSelection() && !download->fileSelectionReady()) {
      filePriorityUpdatePending = synchronizeSelection(download.get());
    }
    if (download->impl_->runRequested) {
      if (filePriorityUpdatePending) {
        download->impl_->resumeAfterFilePriorityUpdate = true;
      }
      else {
        const bool selectionApplying = download->fileSelectionApplying();
        download->completeFileSelectionApply();
        if (selectionApplying) {
          requestProgressRefresh(download.get());
        }
        resumeTorrent(download.get());
      }
    }
  }
}

void BtSession::handleAlert(lt::torrent_checked_alert* checked)
{
  auto download = findDownload(checked->handle);
  if (download) {
    requestProgressRefresh(download.get());
    if (download->impl_->recheckAfterAdd) {
      download->impl_->recheckAfterAdd = false;
      const bool resume = download->impl_->resumeAfterRecheck;
      download->impl_->resumeAfterRecheck = false;
      download->clearError();
      download->completeFileSelectionApply();
      if (resume && download->group() &&
          !download->group()->isPauseRequested() &&
          !download->group()->isHaltRequested()) {
        resumeTorrent(download.get());
      }
      else {
        download->snapshot_.state = BtSnapshot::State::Paused;
      }
    }
    if (download->group() && download->group()->isPauseRequested()) {
      download->stopReason_ = BtDownload::StopReason::Pause;
      download->shutdownStage_ = BtDownload::ShutdownStage::Complete;
      download->snapshot_.state = BtSnapshot::State::Paused;
    }
  }
}

void BtSession::handleAlert(lt::fastresume_rejected_alert* rejected)
{
  auto download = findDownload(rejected->handle);
  if (download) {
    download->beginProgressVerification();
  }
  A2_LOG_WARN(rejected->message());
}

void BtSession::handleAlert(lt::hash_failed_alert* failed)
{
  auto download = findDownload(failed->handle);
  if (download) {
    download->beginProgressVerification();
  }
}

void BtSession::handleAlert(lt::state_changed_alert* changed)
{
  auto download = findDownload(changed->handle);
  if (download &&
      (changed->state == lt::torrent_status::checking_files ||
       changed->state == lt::torrent_status::checking_resume_data)) {
    download->beginProgressVerification();
  }
}

void BtSession::handleAlert(lt::metadata_received_alert* metadata)
{
  auto download = findDownload(metadata->handle);
  if (download && download->group()) {
    const bool pauseForSelection = download->shouldPauseAfterMetadata();
    auto info = metadata->handle.torrent_file();
    if (info) {
      download->impl_->params.ti = std::make_shared<lt::torrent_info>(*info);
      download->impl_->params.info_hashes = info->info_hashes();
      for (const auto& tracker : metadata->handle.trackers()) {
        if (download->impl_->trackerOverride) {
          break;
        }
        if (download->trackerSource(tracker.url) == "global") {
          continue;
        }
        const auto found = std::find_if(download->impl_->sourceTrackers.begin(),
                                        download->impl_->sourceTrackers.end(),
                                        [&tracker](const BtTrackerSpec& entry) {
                                          return entry.url == tracker.url;
                                        });
        if (found == download->impl_->sourceTrackers.end()) {
          download->impl_->sourceTrackers.push_back(
              {tracker.url, tracker.tier, BtTrackerOrigin::Metainfo});
        }
      }
      updateDownloadContext(download.get(), download->group());
      auto managed = impl_->downloads.at(download->group()->getGID());
      const bool removalPending = download->group()->isHaltRequested() &&
                                  !download->group()->isPauseRequested();
      const bool awaitSelection =
          pauseForSelection && !removalPending &&
          download->stopReason() != BtDownload::StopReason::Stop;
      if (awaitSelection) {
        download->beginFileSelectionPause();
      }
      A2_LOG_INFO(fmt("component=bittorrent event=metadata_received gid=%s "
                      "files=%lu selection=%s",
                      gidFor(download).c_str(),
                      static_cast<unsigned long>(info->layout().num_files()),
                      awaitSelection ? "awaiting" : "ready"));
      try {
        applyDownloadOptions(managed, download->group()->getOption().get());
      }
      catch (RecoverableException& error) {
        download->setError(error.what());
        requestStop(managed, BtDownload::StopReason::Stop);
        return;
      }
      if (awaitSelection) {
        download->group()->setHaltRequested(true, RequestGroup::NONE);
        download->group()->setPauseRequested(true);
        requestStop(managed, BtDownload::StopReason::FileSelection);
      }
      else if (!download->stopRequested()) {
        requestResumeCheckpoint(download.get(), true);
      }
    }
  }
}

void BtSession::handleAlert(lt::torrent_finished_alert* finished)
{
  auto download = findDownload(finished->handle);
  if (download) {
    A2_LOG_INFO(fmt("component=bittorrent event=payload_finished gid=%s",
                    gidFor(download).c_str()));
    download->impl_->handle.post_file_progress({});
    requestResumeCheckpoint(download.get(), true);
  }
}

} // namespace aria2
