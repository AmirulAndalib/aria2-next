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
#include "BtDownloadImpl.h"
#include "RequestGroup.h"
#include "BtSession.h"
#include "BtSnapshot.h"
#include "Log.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <libtorrent/alert_types.hpp>
#include <utility>
#include <vector>
#include "bittorrent/BtSessionInternal.h"

namespace aria2 {
using namespace bt_session;

void BtSession::handleAlert(lt::session_stats_alert* stats)
{
  const auto counters = stats->counters();
  auto value = [counters](int index) -> uint64_t {
    return index >= 0 && static_cast<size_t>(index) < counters.size()
               ? static_cast<uint64_t>(std::max<int64_t>(0, counters[index]))
               : 0;
  };
  impl_->peerSockets = value(impl_->metrics.peerSockets);
  impl_->halfOpenPeers = value(impl_->metrics.halfOpenPeers);
  impl_->tcpPeers = value(impl_->metrics.tcpPeers);
  impl_->utpPeers = value(impl_->metrics.utpPeers);
  impl_->queuedTrackerAnnounces = value(impl_->metrics.queuedTrackerAnnounces);
  impl_->connectionAttempts = value(impl_->metrics.connectionAttempts);
  impl_->connectionTimeouts = value(impl_->metrics.connectionTimeouts);
  impl_->payloadDownloaded = value(impl_->metrics.payloadDownloaded);
  impl_->payloadUploaded = value(impl_->metrics.payloadUploaded);
  impl_->trackerDownloaded = value(impl_->metrics.trackerDownloaded);
  impl_->trackerUploaded = value(impl_->metrics.trackerUploaded);
  impl_->ipOverheadDownloaded = value(impl_->metrics.ipOverheadDownloaded);
  impl_->ipOverheadUploaded = value(impl_->metrics.ipOverheadUploaded);
  impl_->dhtDownloaded = value(impl_->metrics.dhtDownloaded);
  impl_->dhtUploaded = value(impl_->metrics.dhtUploaded);
  impl_->diskBlocksInUse = value(impl_->metrics.diskBlocksInUse);
  impl_->queuedDiskJobs = value(impl_->metrics.queuedDiskJobs);
  const auto diskJobs = value(impl_->metrics.diskWriteJobs) +
                        value(impl_->metrics.diskReadJobs) +
                        value(impl_->metrics.diskHashJobs);
  impl_->averageDiskJobTime =
      diskJobs == 0 ? 0 : value(impl_->metrics.diskJobTime) / diskJobs;
  impl_->diskRequestLatency = value(impl_->metrics.diskRequestLatency);
  impl_->diskReadWaitingPeers = value(impl_->metrics.diskReadWaitingPeers);
  impl_->diskWriteWaitingPeers = value(impl_->metrics.diskWriteWaitingPeers);
  impl_->dhtNodes = value(impl_->metrics.dhtNodes);
  impl_->dhtReplacements = value(impl_->metrics.dhtReplacements);
}

void BtSession::handleAlert(lt::state_update_alert* update)
{
  for (const auto& status : update->status) {
    auto download = findDownload(status.handle);
    if (!download) {
      continue;
    }
    auto& snapshot = download->snapshot_;
    const auto transportState = translateState(status);
    const bool verificationInProgress = progressVerificationInProgress(status);
    if (status.errc && !download->failed()) {
      BtErrorSnapshot error;
      error.present = true;
      error.code = status.errc.value();
      error.kind = "status";
      error.category = status.errc.category().name();
      error.message = status.errc.message();
      download->setError(std::move(error));
    }
    download->applyTransportState(transportState);
    snapshot.name = status.name.empty() ? snapshot.name : status.name;
    snapshot.currentTracker = status.current_tracker;
    snapshot.allTimeDownload = status.all_time_download;
    snapshot.allTimeUpload = status.all_time_upload;
    snapshot.failedBytes = status.total_failed_bytes;
    snapshot.redundantBytes = status.total_redundant_bytes;
    if (download->group()) {
      auto context = download->group()->getDownloadContext().get();
      updatePayloadCounter(context, status.total_payload_download,
                           download->impl_->payloadDownloaded, false);
      updatePayloadCounter(context, status.total_payload_upload,
                           download->impl_->payloadUploaded, true);
    }
    snapshot.numComplete = status.num_complete;
    snapshot.numIncomplete = status.num_incomplete;
    if (!status.pieces.empty()) {
      snapshot.bitfield = bitfield(status.pieces);
    }
    snapshot.queuePosition = static_cast<int>(status.queue_position);
    snapshot.seedingTime = static_cast<int>(status.seeding_duration.count());
    snapshot.activeTime = static_cast<int>(status.active_duration.count());
    snapshot.finishedTime = static_cast<int>(status.finished_duration.count());
    snapshot.connectCandidates = status.connect_candidates;
    snapshot.numUploads = status.num_uploads;
    snapshot.availabilityPpm =
        status.distributed_full_copies < 0 || status.distributed_fraction < 0
            ? -1
            : status.distributed_full_copies * 1000000 +
                  status.distributed_fraction * 1000;
    snapshot.hasMetadata = status.has_metadata;
    if (!verificationInProgress &&
        snapshot.fileSelectionState == BtSnapshot::FileSelectionState::None &&
        !snapshot.error.present) {
      download->applyNativeCompletion(status.is_finished, status.is_seeding);
    }
  }
}

void BtSession::handleAlert(lt::peer_info_alert* peers)
{
  auto download = findDownload(peers->handle);
  if (download) {
    download->snapshot_.peers.clear();
    download->snapshot_.peers.reserve(peers->peer_info.size());
    download->snapshot_.numPeers = 0;
    download->snapshot_.connectingPeers = 0;
    download->snapshot_.handshakingPeers = 0;
    download->snapshot_.numSeeds = 0;
    for (const auto& peer : peers->peer_info) {
      auto snapshot = makePeer(peer, download->snapshot_.totalLength);
      if (snapshot.state == "connected") {
        ++download->snapshot_.numPeers;
        if (snapshot.seeder) {
          ++download->snapshot_.numSeeds;
        }
      }
      else if (snapshot.state == "connecting") {
        ++download->snapshot_.connectingPeers;
      }
      else {
        ++download->snapshot_.handshakingPeers;
      }
      download->snapshot_.peers.push_back(std::move(snapshot));
    }
  }
}

void BtSession::handleAlert(lt::file_progress_alert* progress)
{
  auto download = findDownload(progress->handle);
  if (download) {
    std::vector<int64_t> completedLengths;
    completedLengths.reserve(progress->files.size());
    for (const auto completedLength : progress->files) {
      completedLengths.push_back(completedLength);
    }
    download->applyFileProgress(completedLengths);
  }
}

void BtSession::handleAlert(lt::file_prio_alert* priorities)
{
  auto download = findDownload(priorities->handle);
  if (download) {
    finishFilePriorityUpdate(download.get());
  }
}

void BtSession::handleAlert(lt::file_priorities_alert* priorities)
{
  auto download = findDownload(priorities->handle);
  if (download && download->impl_->filePriorityUpdatePending) {
    download->impl_->filePriorityUpdatePending = false;
    download->impl_->appliedFilePriorities = priorities->priorities;
    download->impl_->appliedPiecePriorities.clear();
    continueSelectionSynchronization(download.get());
  }
}

void BtSession::handleAlert(lt::alerts_dropped_alert* dropped)
{
  impl_->droppedAlerts += dropped->dropped_alerts.count();
  if (dropped->dropped_alerts.test(lt::file_prio_alert::alert_type)) {
    for (const auto& entry : impl_->handles) {
      const auto download = entry.second.lock();
      if (download && download->impl_->filePriorityUpdatePending &&
          download->impl_->handle.is_valid() &&
          download->impl_->handle.in_session()) {
        download->impl_->handle.post_file_priorities();
      }
    }
  }
  A2_LOG_ERROR(dropped->message());
}

} // namespace aria2
