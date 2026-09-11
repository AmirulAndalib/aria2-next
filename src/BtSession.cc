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
#include "wallclock.h"
#include <libtorrent/session.hpp>
#include "BtSession.h"
#include "ApplicationStatePath.h"
#include "BtDownload.h"
#include "BtDownloadCommand.h"
#include "BtDownloadImpl.h"
#include "BtSettings.h"
#include "Command.h"
#include "DownloadEngine.h"
#include "GroupId.h"
#include "Log.h"
#include "Option.h"
#include "RequestGroup.h"
#include "a2functional.h"
#include "fmt.h"
#include "prefs.h"
#include <chrono>
#include <libtorrent/alert.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/session_stats.hpp>
#include <libtorrent/settings_pack.hpp>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "bittorrent/BtSessionInternal.h"

namespace aria2 {
using namespace bt_session;

BtSession::Impl::Impl(const Option* option)
    : option(option),
      config(makeBtConfig(option)),
      loggingRevision(logging::revision()),
      downloadRateLimit(option->getAsInt(PREF_MAX_OVERALL_DOWNLOAD_LIMIT)),
      sessionStateFile(state::btSessionFile(option))
{
  metrics.peerSockets = lt::find_metric_idx("peer.num_peers_connected");
  metrics.halfOpenPeers = lt::find_metric_idx("peer.num_peers_half_open");
  metrics.tcpPeers = lt::find_metric_idx("peer.num_tcp_peers");
  metrics.utpPeers = lt::find_metric_idx("peer.num_utp_peers");
  metrics.queuedTrackerAnnounces =
      lt::find_metric_idx("tracker.num_queued_tracker_announces");
  metrics.connectionAttempts = lt::find_metric_idx("peer.connection_attempts");
  metrics.connectionTimeouts = lt::find_metric_idx("peer.connect_timeouts");
  metrics.payloadDownloaded = lt::find_metric_idx("net.recv_payload_bytes");
  metrics.payloadUploaded = lt::find_metric_idx("net.sent_payload_bytes");
  metrics.trackerDownloaded = lt::find_metric_idx("net.recv_tracker_bytes");
  metrics.trackerUploaded = lt::find_metric_idx("net.sent_tracker_bytes");
  metrics.ipOverheadDownloaded =
      lt::find_metric_idx("net.recv_ip_overhead_bytes");
  metrics.ipOverheadUploaded =
      lt::find_metric_idx("net.sent_ip_overhead_bytes");
  metrics.dhtDownloaded = lt::find_metric_idx("dht.dht_bytes_in");
  metrics.dhtUploaded = lt::find_metric_idx("dht.dht_bytes_out");
  metrics.diskBlocksInUse = lt::find_metric_idx("disk.disk_blocks_in_use");
  metrics.queuedDiskJobs = lt::find_metric_idx("disk.queued_disk_jobs");
  metrics.diskWriteJobs = lt::find_metric_idx("disk.num_write_ops");
  metrics.diskReadJobs = lt::find_metric_idx("disk.num_read_ops");
  metrics.diskHashJobs = lt::find_metric_idx("disk.num_blocks_hashed");
  metrics.diskJobTime = lt::find_metric_idx("disk.disk_job_time");
  metrics.diskRequestLatency = lt::find_metric_idx("disk.request_latency");
  metrics.diskReadWaitingPeers = lt::find_metric_idx("peer.num_peers_up_disk");
  metrics.diskWriteWaitingPeers =
      lt::find_metric_idx("peer.num_peers_down_disk");
  metrics.dhtNodes = lt::find_metric_idx("dht.dht_nodes");
  metrics.dhtReplacements = lt::find_metric_idx("dht.dht_node_cache");
}

BtSession::Impl::~Impl() = default;

void BtSession::reopenNetworkSockets()
{
  impl_->session->reopen_network_sockets();
  impl_->listenEndpoints.clear();
  impl_->listenPort = 0;
  impl_->announcePort = 0;
  for (const auto& entry : impl_->downloads) {
    const auto& handle = entry.second->impl_->handle;
    if (!handle.is_valid() || !handle.in_session()) {
      continue;
    }
    handle.force_reannounce();
    handle.force_dht_announce();
    handle.force_lsd_announce();
  }
  A2_LOG_INFO("component=bittorrent event=network_sockets_reopened");
}

BtSession::BtSession(const Option* option) : impl_(make_unique<Impl>(option))
{
  auto params =
      makeSessionParams(option, impl_->config, impl_->lastSessionState);
  configureBtDiskIo(params, option);
  impl_->session = make_unique<lt::session>(std::move(params));
  impl_->session->set_alert_notify(
      [impl = impl_.get()]() { impl->alertsPending.store(true); });
  impl_->lastSessionStateSave = global::wallclock();
}

BtSession::~BtSession()
{
  impl_->session->set_alert_notify({});
  saveSessionState(impl_.get());
}

std::unique_ptr<Command>
BtSession::start(const std::shared_ptr<BtDownload>& download,
                 RequestGroup* group, DownloadEngine* engine)
{
  download->prepareStart();
  attach(download, group, AttachMode::Running);
  return make_unique<BtDownloadCommand>(engine->newCUID(), download, this,
                                        group, engine);
}

void BtSession::restorePaused(const std::shared_ptr<BtDownload>& download,
                              RequestGroup* group)
{
  if (!download || !group) {
    return;
  }
  if (download->impl_->nativeState == BtNativeState::Adding ||
      download->impl_->nativeState == BtNativeState::Attached) {
    download->impl_->runRequested = false;
    download->snapshot_.state = BtSnapshot::State::Paused;
    return;
  }
  attach(download, group, AttachMode::RestorePaused);
}

void BtSession::poll()
{
  const auto logRevision = logging::revision();
  if (impl_->loggingRevision != logRevision) {
    impl_->loggingRevision = logRevision;
    lt::settings_pack settings;
    settings.set_int(lt::settings_pack::alert_mask, btAlertMask());
    impl_->session->apply_settings(std::move(settings));
    A2_LOG_DEBUG(fmt("component=bittorrent event=diagnostics_reconfigured "
                     "alert_mask=%d",
                     btAlertMask()));
  }
  const auto now = std::chrono::system_clock::now();
  const bool resumedAfterSleep =
      now - impl_->lastPoll > std::chrono::seconds(100);
  if (resumedAfterSleep) {
    reopenNetworkSockets();
  }
  impl_->lastPoll = now;

  std::vector<lt::alert*> alerts;
  if (impl_->alertsPending.exchange(false)) {
    impl_->session->pop_alerts(&alerts);
  }
  for (auto* alert : alerts)
    dispatchAlert(alert);
  refreshNativeStatus();
}

// Native alert IDs select typed handlers without retaining pointers beyond
// this drain. libtorrent owns the alert storage until the next pop_alerts().
void BtSession::dispatchAlert(lt::alert* alert)
{
  switch (alert->type()) {
  case lt::add_torrent_alert::alert_type:
    return handleAlert(lt::alert_cast<lt::add_torrent_alert>(alert));
  case lt::log_alert::alert_type:
    return handleAlert(lt::alert_cast<lt::log_alert>(alert));
  case lt::torrent_log_alert::alert_type:
    return handleAlert(lt::alert_cast<lt::torrent_log_alert>(alert));
  case lt::peer_error_alert::alert_type:
    return handleAlert(lt::alert_cast<lt::peer_error_alert>(alert));
  case lt::peer_connect_alert::alert_type:
    return handleAlert(lt::alert_cast<lt::peer_connect_alert>(alert));
  case lt::peer_disconnected_alert::alert_type:
    return handleAlert(lt::alert_cast<lt::peer_disconnected_alert>(alert));
  case lt::tracker_announce_alert::alert_type:
    return handleAlert(lt::alert_cast<lt::tracker_announce_alert>(alert));
  case lt::tracker_reply_alert::alert_type:
    return handleAlert(lt::alert_cast<lt::tracker_reply_alert>(alert));
  case lt::dht_bootstrap_alert::alert_type:
    return handleAlert(lt::alert_cast<lt::dht_bootstrap_alert>(alert));
  case lt::torrent_checked_alert::alert_type:
    return handleAlert(lt::alert_cast<lt::torrent_checked_alert>(alert));
  case lt::fastresume_rejected_alert::alert_type:
    return handleAlert(lt::alert_cast<lt::fastresume_rejected_alert>(alert));
  case lt::hash_failed_alert::alert_type:
    return handleAlert(lt::alert_cast<lt::hash_failed_alert>(alert));
  case lt::state_changed_alert::alert_type:
    return handleAlert(lt::alert_cast<lt::state_changed_alert>(alert));
  case lt::session_stats_alert::alert_type:
    return handleAlert(lt::alert_cast<lt::session_stats_alert>(alert));
  case lt::state_update_alert::alert_type:
    return handleAlert(lt::alert_cast<lt::state_update_alert>(alert));
  case lt::peer_info_alert::alert_type:
    return handleAlert(lt::alert_cast<lt::peer_info_alert>(alert));
  case lt::file_progress_alert::alert_type:
    return handleAlert(lt::alert_cast<lt::file_progress_alert>(alert));
  case lt::file_prio_alert::alert_type:
    return handleAlert(lt::alert_cast<lt::file_prio_alert>(alert));
  case lt::file_priorities_alert::alert_type:
    return handleAlert(lt::alert_cast<lt::file_priorities_alert>(alert));
  case lt::metadata_received_alert::alert_type:
    return handleAlert(lt::alert_cast<lt::metadata_received_alert>(alert));
  case lt::torrent_finished_alert::alert_type:
    return handleAlert(lt::alert_cast<lt::torrent_finished_alert>(alert));
  case lt::save_resume_data_alert::alert_type:
    return handleAlert(lt::alert_cast<lt::save_resume_data_alert>(alert));
  case lt::save_resume_data_failed_alert::alert_type:
    return handleAlert(
        lt::alert_cast<lt::save_resume_data_failed_alert>(alert));
  case lt::torrent_removed_alert::alert_type:
    return handleAlert(lt::alert_cast<lt::torrent_removed_alert>(alert));
  case lt::torrent_deleted_alert::alert_type:
    return handleAlert(lt::alert_cast<lt::torrent_deleted_alert>(alert));
  case lt::torrent_delete_failed_alert::alert_type:
    return handleAlert(lt::alert_cast<lt::torrent_delete_failed_alert>(alert));
  case lt::file_error_alert::alert_type:
    return handleAlert(lt::alert_cast<lt::file_error_alert>(alert));
  case lt::torrent_error_alert::alert_type:
    return handleAlert(lt::alert_cast<lt::torrent_error_alert>(alert));
  case lt::storage_moved_alert::alert_type:
    return handleAlert(lt::alert_cast<lt::storage_moved_alert>(alert));
  case lt::storage_moved_failed_alert::alert_type:
    return handleAlert(lt::alert_cast<lt::storage_moved_failed_alert>(alert));
  case lt::file_rename_failed_alert::alert_type:
    return handleAlert(lt::alert_cast<lt::file_rename_failed_alert>(alert));
  case lt::tracker_error_alert::alert_type:
    return handleAlert(lt::alert_cast<lt::tracker_error_alert>(alert));
  case lt::tracker_warning_alert::alert_type:
    return handleAlert(lt::alert_cast<lt::tracker_warning_alert>(alert));
  case lt::listen_succeeded_alert::alert_type:
    return handleAlert(lt::alert_cast<lt::listen_succeeded_alert>(alert));
  case lt::listen_failed_alert::alert_type:
    return handleAlert(lt::alert_cast<lt::listen_failed_alert>(alert));
  case lt::external_ip_alert::alert_type:
    return handleAlert(lt::alert_cast<lt::external_ip_alert>(alert));
  case lt::portmap_alert::alert_type:
    return handleAlert(lt::alert_cast<lt::portmap_alert>(alert));
  case lt::portmap_error_alert::alert_type:
    return handleAlert(lt::alert_cast<lt::portmap_error_alert>(alert));
  case lt::dht_stats_alert::alert_type:
    return handleAlert(lt::alert_cast<lt::dht_stats_alert>(alert));
  case lt::alerts_dropped_alert::alert_type:
    return handleAlert(lt::alert_cast<lt::alerts_dropped_alert>(alert));
  case lt::socks5_alert::alert_type:
    return handleAlert(lt::alert_cast<lt::socks5_alert>(alert));
  case lt::performance_alert::alert_type:
    return handleAlert(lt::alert_cast<lt::performance_alert>(alert));
  case lt::ip_ban_alert::alert_type:
    return handleAlert(lt::alert_cast<lt::ip_ban_alert>(alert));
  default:
    return;
  }
}

std::shared_ptr<BtDownload>
BtSession::findDownload(const lt::torrent_handle& handle) const
{
  const auto found = impl_->handles.find(handle);
  return found == impl_->handles.end() ? std::shared_ptr<BtDownload>{}
                                       : found->second.lock();
}

std::string BtSession::gidFor(const std::shared_ptr<BtDownload>& download)
{
  return download && download->group()
             ? GroupId::toHex(download->group()->getGID())
             : "unknown";
}

void BtSession::refreshNativeStatus()
{

  if (impl_->lastUpdate.isZero() ||
      impl_->lastUpdate.difference(global::wallclock()) >= 1_s) {
    impl_->lastUpdate = global::wallclock();
    auto query =
        lt::torrent_handle::query_name | lt::torrent_handle::query_save_path;
    if (impl_->lastPieceUpdate.isZero() ||
        impl_->lastPieceUpdate.difference(global::wallclock()) >= 5_s) {
      query |= lt::torrent_handle::query_pieces;
      impl_->lastPieceUpdate = global::wallclock();
    }
    impl_->session->post_torrent_updates(query);
    const bool updatePeers =
        impl_->lastPeerUpdate.isZero() ||
        impl_->lastPeerUpdate.difference(global::wallclock()) >= 2_s;
    if (updatePeers) {
      impl_->lastPeerUpdate = global::wallclock();
    }
    for (const auto& entry : impl_->downloads) {
      const auto& download = entry.second;
      if (download->impl_->handle.is_valid() && download->active()) {
        if (download->hasMetadata() && !download->snapshot_.selectedComplete) {
          download->impl_->handle.post_file_progress({});
        }
        if (updatePeers) {
          download->impl_->handle.post_peer_info();
        }
        if (download->impl_->lastTrackerUpdate.isZero() ||
            download->impl_->lastTrackerUpdate.difference(
                global::wallclock()) >= 10_s) {
          download->snapshot_.trackers =
              makeTrackers(download->impl_->handle, download.get());
          download->impl_->lastTrackerUpdate = global::wallclock();
        }
        requestResumeCheckpoint(download.get());
      }
    }
  }

  if (impl_->lastSessionStateSave.difference(global::wallclock()) >=
      std::chrono::minutes(1)) {
    saveSessionState(impl_.get());
  }
  if (impl_->lastDhtStats.isZero() ||
      impl_->lastDhtStats.difference(global::wallclock()) >= 10_s) {
    impl_->lastDhtStats = global::wallclock();
    impl_->session->post_dht_stats();
  }
  if (impl_->lastSessionStats.isZero() ||
      impl_->lastSessionStats.difference(global::wallclock()) >= 2_s) {
    impl_->lastSessionStats = global::wallclock();
    impl_->session->post_session_stats();
  }
}

} // namespace aria2
