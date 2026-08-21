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
#include "BtSession.h"

#include <algorithm>
#include <array>
#include <map>
#include <sstream>
#include <utility>

#include <libtorrent/alert.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/announce_entry.hpp>
#include <libtorrent/download_priority.hpp>
#include <libtorrent/fingerprint.hpp>
#include <libtorrent/ip_filter.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/session_params.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/torrent_flags.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/write_resume_data.hpp>

#include "BtDownload.h"
#include "BtDownloadImpl.h"
#include "BtDownloadCommand.h"
#include "BtPeerBlocklist.h"
#include "BufferedFile.h"
#include "DownloadContext.h"
#include "DownloadEngine.h"
#include "File.h"
#include "FileEntry.h"
#include "Log.h"
#include "Option.h"
#include "RequestGroup.h"
#include "SocketCore.h"
#include "BtMetadata.h"
#include "fmt.h"
#include "prefs.h"
#include "util.h"
#include "wallclock.h"

namespace aria2 {

namespace lt = libtorrent;

struct BtSession::Impl {
  const Option* option;
  std::unique_ptr<lt::session> session;
  std::map<a2_gid_t, std::shared_ptr<BtDownload>> downloads;
  std::map<lt::torrent_handle, BtDownload*> handles;
  BtPeerBlocklist blocklist;
  uint64_t filterRevision = 0;
  uint16_t listenPort = 0;
  uint16_t announcePort = 0;
  std::string externalAddress;
  Timer lastUpdate = Timer::zero();

  explicit Impl(const Option* option) : option(option) {}
};

namespace {

lt::settings_pack makeSettings(const Option* option)
{
  lt::settings_pack settings;
  const auto port = option->getAsInt(PREF_LISTEN_PORT);
  auto interfaces = "0.0.0.0:" + std::to_string(port);
  if (!option->getAsBool(PREF_DISABLE_IPV6)) {
    interfaces += ",[::]:" + std::to_string(port);
  }
  settings.set_str(lt::settings_pack::listen_interfaces, interfaces);
  settings.set_int(lt::settings_pack::max_retry_port_bind, 0);
  settings.set_bool(lt::settings_pack::enable_dht,
                    option->getAsBool(PREF_ENABLE_DHT));
  settings.set_bool(lt::settings_pack::enable_lsd,
                    option->getAsBool(PREF_BT_ENABLE_LPD));
  settings.set_bool(lt::settings_pack::enable_upnp, true);
  settings.set_bool(lt::settings_pack::enable_natpmp, true);
  settings.set_bool(lt::settings_pack::enable_incoming_tcp, true);
  settings.set_bool(lt::settings_pack::enable_outgoing_tcp, true);
  settings.set_bool(lt::settings_pack::enable_incoming_utp, true);
  settings.set_bool(lt::settings_pack::enable_outgoing_utp, true);
  settings.set_str(lt::settings_pack::user_agent,
                   "aria2-next/" PACKAGE_VERSION);
  settings.set_str(lt::settings_pack::peer_fingerprint,
                   lt::generate_fingerprint("A2", PACKAGE_VERSION_MAJOR,
                                            PACKAGE_VERSION_MINOR,
                                            PACKAGE_VERSION_PATCH));
  settings.set_int(lt::settings_pack::alert_queue_size, 4096);
  settings.set_int(
      lt::settings_pack::alert_mask,
      static_cast<int>(static_cast<unsigned int>(
          lt::alert_category::error | lt::alert_category::status |
          lt::alert_category::storage | lt::alert_category::tracker |
          lt::alert_category::peer | lt::alert_category::port_mapping)));
  settings.set_int(lt::settings_pack::upload_rate_limit,
                   option->getAsInt(PREF_MAX_OVERALL_UPLOAD_LIMIT));
  settings.set_int(lt::settings_pack::download_rate_limit,
                   option->getAsInt(PREF_MAX_OVERALL_DOWNLOAD_LIMIT));
  settings.set_int(lt::settings_pack::file_pool_size,
                   option->getAsInt(PREF_BT_MAX_OPEN_FILES));
  if (!option->blank(PREF_BT_EXTERNAL_IP)) {
    std::array<unsigned char, 16> address{};
    if (net::getBinAddr(address.data(), option->get(PREF_BT_EXTERNAL_IP)) ==
        0) {
      throw DL_ABORT_EX("bt-external-ip must be a numeric IP address");
    }
    settings.set_str(lt::settings_pack::announce_ip,
                     option->get(PREF_BT_EXTERNAL_IP));
  }
  settings.set_int(lt::settings_pack::announce_port,
                   option->getAsInt(PREF_BT_EXTERNAL_PORT));
  settings.set_int(lt::settings_pack::tracker_completion_timeout,
                   option->getAsInt(PREF_BT_TRACKER_CONNECT_TIMEOUT));
  settings.set_int(lt::settings_pack::tracker_receive_timeout,
                   option->getAsInt(PREF_BT_TRACKER_TIMEOUT));
  settings.set_int(lt::settings_pack::request_timeout,
                   option->getAsInt(PREF_BT_REQUEST_TIMEOUT));
  settings.set_int(lt::settings_pack::peer_timeout,
                   option->getAsInt(PREF_BT_TIMEOUT));
  const bool encryptionRequired = option->getAsBool(PREF_BT_REQUIRE_CRYPTO) ||
                                  option->getAsBool(PREF_BT_FORCE_ENCRYPTION);
  settings.set_int(lt::settings_pack::out_enc_policy,
                   encryptionRequired ? lt::settings_pack::pe_forced
                                      : lt::settings_pack::pe_enabled);
  settings.set_int(lt::settings_pack::in_enc_policy,
                   encryptionRequired ? lt::settings_pack::pe_forced
                                      : lt::settings_pack::pe_enabled);
  settings.set_int(lt::settings_pack::allowed_enc_level,
                   (option->getAsBool(PREF_BT_FORCE_ENCRYPTION) ||
                    option->get(PREF_BT_MIN_CRYPTO_LEVEL) == V_ARC4)
                       ? lt::settings_pack::pe_rc4
                       : lt::settings_pack::pe_both);
  return settings;
}

BtSnapshot::State translateState(const lt::torrent_status& status)
{
  if (status.errc) {
    return BtSnapshot::State::Error;
  }
  if (status.flags & lt::torrent_flags::paused) {
    return BtSnapshot::State::Paused;
  }
  switch (status.state) {
  case lt::torrent_status::checking_files:
  case lt::torrent_status::checking_resume_data:
    return BtSnapshot::State::Checking;
  case lt::torrent_status::downloading_metadata:
    return BtSnapshot::State::DownloadingMetadata;
  case lt::torrent_status::downloading:
    return BtSnapshot::State::Downloading;
  case lt::torrent_status::finished:
    return BtSnapshot::State::Finished;
  case lt::torrent_status::seeding:
    return BtSnapshot::State::Seeding;
  }
  return BtSnapshot::State::Downloading;
}

std::string bitfield(const lt::typed_bitfield<lt::piece_index_t>& pieces)
{
  std::string data((pieces.size() + 7) / 8, '\0');
  for (int i = 0; i < pieces.size(); ++i) {
    if (pieces[lt::piece_index_t{i}]) {
      data[static_cast<size_t>(i) / 8] |=
          static_cast<char>(0x80 >> (static_cast<unsigned int>(i) % 8));
    }
  }
  return util::toHex(data);
}

std::string peerFlags(const lt::peer_info& peer)
{
  std::string flags;
  auto append = [&flags](char value) {
    if (!flags.empty()) {
      flags += ' ';
    }
    flags += value;
  };
  if (peer.flags & lt::peer_info::interesting) {
    append(peer.flags & lt::peer_info::remote_choked ? 'd' : 'D');
  }
  if (peer.flags & lt::peer_info::remote_interested) {
    append(peer.flags & lt::peer_info::choked ? 'u' : 'U');
  }
  if (peer.flags & lt::peer_info::optimistic_unchoke) {
    append('O');
  }
  if (peer.flags & lt::peer_info::snubbed) {
    append('S');
  }
  if (!(peer.flags & lt::peer_info::outgoing_connection)) {
    append('I');
  }
  return flags;
}

std::vector<std::string> peerSources(const lt::peer_info& peer)
{
  std::vector<std::string> result;
  if (peer.source & lt::peer_info::tracker) {
    result.emplace_back("tracker");
  }
  if (peer.source & lt::peer_info::dht) {
    result.emplace_back("dht");
  }
  if (peer.source & lt::peer_info::pex) {
    result.emplace_back("pex");
  }
  if (peer.source & lt::peer_info::lsd) {
    result.emplace_back("lsd");
  }
  if (peer.source & lt::peer_info::resume_data) {
    result.emplace_back("resume");
  }
  if (peer.source & lt::peer_info::incoming) {
    result.emplace_back("incoming");
  }
  return result;
}

BtPeerSnapshot makePeer(const lt::peer_info& peer, int64_t totalLength)
{
  BtPeerSnapshot result;
  const auto endpoint = peer.remote_endpoint();
  result.peerId.assign(reinterpret_cast<const char*>(peer.pid.data()),
                       peer.pid.size());
  result.clientName = peer.client;
  result.ip = endpoint.address().to_string();
  result.port = endpoint.port();
  result.bitfield = bitfield(peer.pieces);
  result.flags = peerFlags(peer);
  result.transport = peer.flags & lt::peer_info::utp_socket ? "utp" : "tcp";
  if (peer.flags & lt::peer_info::ssl_socket) {
    result.encryption = "tls";
  }
  else if (peer.flags & lt::peer_info::rc4_encrypted) {
    result.encryption = "rc4";
  }
  else if (peer.flags & lt::peer_info::plaintext_encrypted) {
    result.encryption = "encryptedHandshake";
  }
  else {
    result.encryption = "plain";
  }
  result.sources = peerSources(peer);
  result.downloaded = peer.total_download;
  result.uploaded = peer.total_upload;
  result.completedLength = static_cast<int64_t>(
      static_cast<long double>(totalLength) * peer.progress_ppm / 1000000.0L);
  result.downloadSpeed = peer.payload_down_speed;
  result.uploadSpeed = peer.payload_up_speed;
  result.progressPpm = peer.progress_ppm;
  result.amChoking = bool(peer.flags & lt::peer_info::choked);
  result.amInterested = bool(peer.flags & lt::peer_info::interesting);
  result.peerChoking = bool(peer.flags & lt::peer_info::remote_choked);
  result.peerInterested = bool(peer.flags & lt::peer_info::remote_interested);
  result.incoming = !(peer.flags & lt::peer_info::outgoing_connection);
  result.snubbed = bool(peer.flags & lt::peer_info::snubbed);
  result.optimisticUnchoke =
      bool(peer.flags & lt::peer_info::optimistic_unchoke);
  result.handshaking = bool(peer.flags & lt::peer_info::handshake);
  result.seeder = bool(peer.flags & lt::peer_info::seed);
  return result;
}

void saveResume(const std::string& path, const lt::add_torrent_params& params)
{
  const auto data = lt::write_resume_data_buf(params);
  const auto temporary = path + "__temp";
  File directory(File(path).getDirname());
  if (!directory.isDir() && !directory.mkdirs()) {
    throw DL_ABORT_EX("Unable to create BitTorrent resume directory");
  }
  BufferedFile file(temporary.c_str(), BufferedFile::WRITE);
  if (!file || file.write(data.data(), data.size()) != data.size() ||
      file.close() == EOF || !File(temporary).renameTo(path)) {
    throw DL_ABORT_EX("Unable to save BitTorrent resume data");
  }
}

lt::ip_filter makeIpFilter(const BtPeerBlocklist& blocklist)
{
  lt::ip_filter filter;
  for (const auto& range : blocklist.ipv4Ranges()) {
    lt::address_v4::bytes_type first{};
    lt::address_v4::bytes_type last{};
    std::copy_n(range.first.begin(), first.size(), first.begin());
    std::copy_n(range.last.begin(), last.size(), last.begin());
    filter.add_rule(lt::address_v4(first), lt::address_v4(last),
                    lt::ip_filter::blocked);
  }
  for (const auto& range : blocklist.ipv6Ranges()) {
    lt::address_v6::bytes_type first{};
    lt::address_v6::bytes_type last{};
    std::copy_n(range.first.begin(), first.size(), first.begin());
    std::copy_n(range.last.begin(), last.size(), last.begin());
    filter.add_rule(lt::address_v6(first), lt::address_v6(last),
                    lt::ip_filter::blocked);
  }
  return filter;
}

void applySelection(const lt::torrent_handle& handle, RequestGroup* group)
{
  if (!group) {
    return;
  }
  group->getBtDownload()->updateSelection(group->getDownloadContext());
  if (!handle.is_valid()) {
    return;
  }
  std::vector<lt::download_priority_t> priorities;
  const auto info = handle.torrent_file();
  const auto& savePath = group->getOption()->get(PREF_DIR);
  size_t index = 0;
  for (const auto& file : group->getDownloadContext()->getFileEntries()) {
    priorities.push_back(file->isRequested() ? lt::default_priority
                                             : lt::dont_download);
    if (info && index < static_cast<size_t>(info->layout().num_files())) {
      auto storagePath = file->getPath();
      if (!savePath.empty() && storagePath.size() > savePath.size() &&
          storagePath.compare(0, savePath.size(), savePath) == 0 &&
          (storagePath[savePath.size()] == '/' ||
           storagePath[savePath.size()] == '\\')) {
        storagePath.erase(0, savePath.size() + 1);
      }
      if (storagePath !=
          info->layout().file_path(lt::file_index_t{static_cast<int>(index)})) {
        handle.rename_file(lt::file_index_t{static_cast<int>(index)},
                           storagePath);
      }
    }
    ++index;
  }
  if (!priorities.empty()) {
    handle.prioritize_files(priorities);
  }
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

} // namespace

BtSession::BtSession(const Option* option) : impl_(make_unique<Impl>(option))
{
  lt::session_params params(makeSettings(option));
  impl_->session = make_unique<lt::session>(std::move(params));
}

BtSession::~BtSession() = default;

std::unique_ptr<Command>
BtSession::start(const std::shared_ptr<BtDownload>& download,
                 RequestGroup* group, DownloadEngine* engine)
{
  download->prepareStart();
  download->initialize(group);
  download->configure(group->getOption().get());
  download->impl_->params.userdata = download.get();

  if (download->impl_->params.ti) {
    updateDownloadContext(download.get(), group);
    download->impl_->params.file_priorities.clear();
    for (const auto& file : group->getDownloadContext()->getFileEntries()) {
      download->impl_->params.file_priorities.push_back(
          file->isRequested() ? lt::default_priority : lt::dont_download);
    }
  }

  impl_->downloads[group->getGID()] = download;
  impl_->session->async_add_torrent(download->impl_->params);
  return make_unique<BtDownloadCommand>(engine->newCUID(), download, this,
                                        group, engine);
}

void BtSession::poll()
{
  std::vector<lt::alert*> alerts;
  impl_->session->pop_alerts(&alerts);
  for (auto* alert : alerts) {
    if (auto* added = lt::alert_cast<lt::add_torrent_alert>(alert)) {
      auto* download = added->params.userdata.get<BtDownload>();
      if (!download) {
        continue;
      }
      if (added->error) {
        download->snapshot_.state = BtSnapshot::State::Error;
        download->snapshot_.errorMessage = added->error.message();
        download->shutdownStage_ = BtDownload::ShutdownStage::Complete;
        continue;
      }
      download->impl_->handle = added->handle;
      impl_->handles[added->handle] = download;
      if (download->stopRequested()) {
        requestStop(impl_->downloads.at(download->group()->getGID()),
                    download->stopReason());
      }
      else {
        applySelection(download->impl_->handle, download->group());
      }
      continue;
    }

    auto findDownload =
        [this](const lt::torrent_handle& handle) -> BtDownload* {
      auto found = impl_->handles.find(handle);
      return found == impl_->handles.end() ? nullptr : found->second;
    };

    if (auto* update = lt::alert_cast<lt::state_update_alert>(alert)) {
      for (const auto& status : update->status) {
        auto* download = findDownload(status.handle);
        if (!download) {
          continue;
        }
        auto& snapshot = download->snapshot_;
        snapshot.state = translateState(status);
        snapshot.name = status.name.empty() ? snapshot.name : status.name;
        snapshot.currentTracker = status.current_tracker;
        snapshot.totalLength = status.total_wanted;
        snapshot.completedLength = status.total_wanted_done;
        snapshot.allTimeDownload = status.all_time_download;
        snapshot.allTimeUpload = status.all_time_upload;
        snapshot.downloadSpeed = status.download_payload_rate;
        snapshot.uploadSpeed = status.upload_payload_rate;
        snapshot.numPeers = status.num_peers;
        snapshot.numSeeds = status.num_seeds;
        snapshot.numComplete = status.num_complete;
        snapshot.numIncomplete = status.num_incomplete;
        snapshot.progressPpm = status.progress_ppm;
        snapshot.bitfield = bitfield(status.pieces);
        snapshot.queuePosition = static_cast<int>(status.queue_position);
        snapshot.seedingTime =
            static_cast<int>(status.seeding_duration.count());
        snapshot.hasMetadata = status.has_metadata;
        snapshot.finished = status.is_finished;
        snapshot.seeding = status.is_seeding;
        if (status.errc) {
          snapshot.errorMessage = status.errc.message();
        }
      }
      continue;
    }

    if (auto* peers = lt::alert_cast<lt::peer_info_alert>(alert)) {
      auto* download = findDownload(peers->handle);
      if (download) {
        download->snapshot_.peers.clear();
        download->snapshot_.peers.reserve(peers->peer_info.size());
        for (const auto& peer : peers->peer_info) {
          download->snapshot_.peers.push_back(
              makePeer(peer, download->snapshot_.totalLength));
        }
      }
      continue;
    }

    if (auto* progress = lt::alert_cast<lt::file_progress_alert>(alert)) {
      auto* download = findDownload(progress->handle);
      if (download) {
        const auto count =
            std::min(download->snapshot_.files.size(),
                     static_cast<size_t>(progress->files.size()));
        for (size_t i = 0; i < count; ++i) {
          download->snapshot_.files[i].completedLength =
              progress->files[lt::file_index_t{static_cast<int>(i)}];
        }
      }
      continue;
    }

    if (auto* metadata = lt::alert_cast<lt::metadata_received_alert>(alert)) {
      auto* download = findDownload(metadata->handle);
      if (download) {
        const bool pauseForSelection = download->shouldPauseAfterMetadata();
        auto info = metadata->handle.torrent_file();
        if (info) {
          download->impl_->params.ti =
              std::make_shared<lt::torrent_info>(*info);
          download->impl_->params.info_hashes = info->info_hashes();
          for (const auto& tracker : metadata->handle.trackers()) {
            if (std::find(download->impl_->sourceTrackers.begin(),
                          download->impl_->sourceTrackers.end(), tracker.url) ==
                download->impl_->sourceTrackers.end()) {
              download->impl_->sourceTrackers.push_back(tracker.url);
              download->impl_->sourceTrackerTiers.push_back(tracker.tier);
            }
          }
          download->configure(download->group()->getOption().get());
          updateDownloadContext(download, download->group());
          auto managed = impl_->downloads.at(download->group()->getGID());
          if (pauseForSelection) {
            download->group()->setHaltRequested(true, RequestGroup::NONE);
            download->group()->setPauseRequested(true);
            requestStop(managed, BtDownload::StopReason::FileSelection);
          }
          else {
            applyDownloadOptions(managed, download->group()->getOption().get());
          }
        }
      }
      continue;
    }

    if (auto* saved = lt::alert_cast<lt::save_resume_data_alert>(alert)) {
      auto* download = findDownload(saved->handle);
      if (download) {
        try {
          saveResume(download->impl_->resumePath, saved->params);
        }
        catch (RecoverableException& error) {
          A2_LOG_ERROR_EX("Failed to save BitTorrent resume data", error);
        }
        download->beginRemoving();
        impl_->session->remove_torrent(saved->handle);
      }
      continue;
    }

    if (auto* failed =
            lt::alert_cast<lt::save_resume_data_failed_alert>(alert)) {
      auto* download = findDownload(failed->handle);
      if (download) {
        A2_LOG_ERROR(fmt("Failed to save BitTorrent resume data %s: %s",
                         download->impl_->resumePath.c_str(),
                         failed->error.message().c_str()));
        download->beginRemoving();
        impl_->session->remove_torrent(failed->handle);
      }
      continue;
    }

    if (auto* removed = lt::alert_cast<lt::torrent_removed_alert>(alert)) {
      auto* download = findDownload(removed->handle);
      if (download) {
        download->finishRemoving();
        impl_->handles.erase(removed->handle);
      }
      continue;
    }

    if (auto* error = lt::alert_cast<lt::torrent_error_alert>(alert)) {
      auto* download = findDownload(error->handle);
      if (download) {
        download->snapshot_.state = BtSnapshot::State::Error;
        download->snapshot_.errorMessage = error->error.message();
      }
      continue;
    }

    if (auto* listening = lt::alert_cast<lt::listen_succeeded_alert>(alert)) {
      if (listening->socket_type == lt::socket_type_t::tcp ||
          listening->socket_type == lt::socket_type_t::utp) {
        impl_->listenPort = static_cast<uint16_t>(listening->port);
      }
      continue;
    }

    if (auto* failed = lt::alert_cast<lt::listen_failed_alert>(alert)) {
      A2_LOG_ERROR(failed->message());
      continue;
    }

    if (auto* external = lt::alert_cast<lt::external_ip_alert>(alert)) {
      impl_->externalAddress = external->external_address.to_string();
    }
  }

  if (impl_->lastUpdate.isZero() ||
      impl_->lastUpdate.difference(global::wallclock()) >= 1_s) {
    impl_->lastUpdate = global::wallclock();
    impl_->session->post_torrent_updates(lt::torrent_handle::query_name |
                                         lt::torrent_handle::query_save_path |
                                         lt::torrent_handle::query_pieces);
    for (const auto& entry : impl_->downloads) {
      const auto& download = entry.second;
      if (download->impl_->handle.is_valid() && download->active()) {
        download->impl_->handle.post_peer_info();
        download->impl_->handle.post_file_progress(
            lt::torrent_handle::piece_granularity);
      }
    }
  }
}

void BtSession::requestStop(const std::shared_ptr<BtDownload>& download,
                            BtDownload::StopReason reason)
{
  download->requestStop(reason);
  if (download->shutdownStage() != BtDownload::ShutdownStage::PendingHandle) {
    return;
  }
  if (!download->impl_->handle.is_valid()) {
    return;
  }
  download->beginSavingResume();
  download->impl_->handle.pause();
  download->impl_->handle.save_resume_data(
      lt::torrent_handle::save_info_dict |
      lt::torrent_handle::flush_disk_cache);
}

void BtSession::applyGlobalOptions(const Option* option)
{
  impl_->session->apply_settings(makeSettings(option));
}

void BtSession::applyDownloadOptions(
    const std::shared_ptr<BtDownload>& download, const Option* option)
{
  if (!download) {
    return;
  }
  const auto previousSavePath = download->impl_->params.save_path;
  download->configure(option);
  download->updateSelection(download->group()->getDownloadContext());
  const auto& handle = download->impl_->handle;
  if (previousSavePath != download->impl_->params.save_path) {
    if (handle.is_valid()) {
      handle.move_storage(download->impl_->params.save_path);
    }
    auto basePath = download->group()->getDownloadContext()->getBasePath();
    if (basePath.empty()) {
      const auto& snapshot = download->snapshot();
      basePath =
          util::applyDir(download->impl_->params.save_path,
                         !snapshot.infoHashV1.empty() ? snapshot.infoHashV1
                                                      : snapshot.infoHashV2);
    }
    download->impl_->resumePath = basePath + ".aria2";
  }
  if (!handle.is_valid()) {
    return;
  }
  handle.set_max_connections(download->impl_->params.max_connections);
  handle.set_upload_limit(download->impl_->params.upload_limit);
  handle.set_download_limit(download->impl_->params.download_limit);
  const auto mask = lt::torrent_flags::disable_dht |
                    lt::torrent_flags::disable_pex |
                    lt::torrent_flags::disable_lsd;
  handle.set_flags(download->impl_->params.flags & mask, mask);
  std::vector<lt::announce_entry> trackers;
  trackers.reserve(download->impl_->params.trackers.size());
  for (size_t i = 0; i < download->impl_->params.trackers.size(); ++i) {
    lt::announce_entry tracker(download->impl_->params.trackers[i]);
    tracker.tier = i < download->impl_->params.tracker_tiers.size()
                       ? download->impl_->params.tracker_tiers[i]
                       : 0;
    trackers.push_back(std::move(tracker));
  }
  handle.replace_trackers(trackers);
  applySelection(handle, download->group());
}

void BtSession::remove(a2_gid_t gid) { impl_->downloads.erase(gid); }

uint16_t BtSession::listenPort() const { return impl_->listenPort; }

uint16_t BtSession::announcePort() const
{
  return impl_->option->getAsInt(PREF_BT_EXTERNAL_PORT) == 0
             ? impl_->listenPort
             : static_cast<uint16_t>(
                   impl_->option->getAsInt(PREF_BT_EXTERNAL_PORT));
}

std::string BtSession::externalAddress() const
{
  return !impl_->option->blank(PREF_BT_EXTERNAL_IP)
             ? impl_->option->get(PREF_BT_EXTERNAL_IP)
             : impl_->externalAddress;
}

bool BtSession::replaceIpFilter(const std::vector<std::string>& rules,
                                std::string& error)
{
  try {
    if (!impl_->blocklist.replace(rules, "RPC")) {
      return false;
    }
    impl_->session->set_ip_filter(makeIpFilter(impl_->blocklist));
    impl_->filterRevision = impl_->blocklist.revision();
    return true;
  }
  catch (RecoverableException& exception) {
    error = exception.what();
    return false;
  }
}

void BtSession::loadIpFilter(const std::string& path)
{
  impl_->blocklist.load(path);
  impl_->session->set_ip_filter(makeIpFilter(impl_->blocklist));
  impl_->filterRevision = impl_->blocklist.revision();
}

size_t BtSession::ipFilterRuleCount() const { return impl_->blocklist.count(); }

uint64_t BtSession::ipFilterRevision() const { return impl_->filterRevision; }

} // namespace aria2
