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
#include "BtDownloadImpl.h"
#include "Option.h"
#include "prefs.h"
#include <libtorrent/storage_defs.hpp>
#include <libtorrent/torrent_flags.hpp>

namespace aria2 {

namespace lt = libtorrent;

void BtDownload::configure(const Option* option)
{
  configureTrackers(option);

  impl_->params.save_path = option->get(PREF_DIR);
  impl_->params.max_connections = option->getAsInt(PREF_BT_MAX_PEERS);
  if (impl_->params.max_connections == 0) {
    impl_->params.max_connections = -1;
  }
  impl_->params.max_uploads = option->getAsInt(PREF_BT_MAX_UPLOADS_PER_TORRENT);
  impl_->params.upload_limit = option->getAsInt(PREF_MAX_UPLOAD_LIMIT);
  if (impl_->params.upload_limit == 0) {
    impl_->params.upload_limit = -1;
  }
  impl_->params.download_limit = option->getAsInt(PREF_MAX_DOWNLOAD_LIMIT);
  if (impl_->params.download_limit == 0) {
    impl_->params.download_limit = -1;
  }
  impl_->params.storage_mode = option->get(PREF_FILE_ALLOCATION) == V_PREALLOC
                                   ? lt::storage_mode_allocate
                                   : lt::storage_mode_sparse;

  impl_->params.flags &=
      ~(lt::torrent_flags::auto_managed | lt::torrent_flags::paused |
        lt::torrent_flags::disable_dht | lt::torrent_flags::disable_pex |
        lt::torrent_flags::disable_lsd | lt::torrent_flags::seed_mode |
        lt::torrent_flags::default_dont_download |
        lt::torrent_flags::sequential_download |
        lt::torrent_flags::super_seeding | lt::torrent_flags::stop_when_ready);
  impl_->params.flags |= lt::torrent_flags::duplicate_is_error;
  impl_->params.flags |= lt::torrent_flags::update_subscribe;
  impl_->params.flags |= lt::torrent_flags::apply_ip_filter;
  impl_->params.flags |= lt::torrent_flags::deprecated_override_trackers;

  if (!option->getAsBool(PREF_ENABLE_DHT)) {
    impl_->params.flags |= lt::torrent_flags::disable_dht;
  }
  if (!option->getAsBool(PREF_ENABLE_PEER_EXCHANGE)) {
    impl_->params.flags |= lt::torrent_flags::disable_pex;
  }
  if (!option->getAsBool(PREF_BT_ENABLE_LPD)) {
    impl_->params.flags |= lt::torrent_flags::disable_lsd;
  }
  if (option->getAsBool(PREF_BT_SEED_UNVERIFIED) && impl_->params.ti) {
    impl_->params.flags |= lt::torrent_flags::seed_mode;
  }
  if (option->getAsBool(PREF_FORCE_SEQUENTIAL)) {
    impl_->params.flags |= lt::torrent_flags::sequential_download;
  }
  if (option->getAsBool(PREF_BT_SUPER_SEEDING)) {
    impl_->params.flags |= lt::torrent_flags::super_seeding;
  }
  if (source_ == Source::Magnet && !impl_->params.ti &&
      option->getAsBool(PREF_ENABLE_RPC) &&
      option->getAsBool(PREF_PAUSE_METADATA)) {
    impl_->params.flags |= lt::torrent_flags::default_dont_download;
  }
}

} // namespace aria2
