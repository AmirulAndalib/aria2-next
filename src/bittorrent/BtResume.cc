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
#include <cstdint>
#include <libtorrent/error_code.hpp>
#include <libtorrent/info_hash.hpp>
#include <utility>
#include "BtDownload.h"
#include "BtDownloadImpl.h"
#include "bittorrent/BtDownloadSupport.h"
#include "BtStateStore.h"
#include "DownloadContext.h"
#include "RequestGroup.h"
#include "Log.h"
#include "support/Numbers.h"
#include "prefs.h"
#include "fmt.h"
#include <libtorrent/read_resume_data.hpp>
#include <algorithm>

namespace aria2 {

namespace lt = libtorrent;
using namespace bt_download;

namespace {
bool hashesMatch(const lt::info_hash_t& expected, const lt::info_hash_t& actual)
{
  return (!expected.has_v1() ||
          (actual.has_v1() && expected.v1 == actual.v1)) &&
         (!expected.has_v2() || (actual.has_v2() && expected.v2 == actual.v2));
}

} // namespace

void BtDownload::initialize(RequestGroup* group)
{
  group_ = group;
  impl_->gid = group_->getGID();
  configure(group_->getOption().get());
  BtStateStore stateStore(group_->getOption().get());
  if (impl_->managedMetadataPath.empty() &&
      stateStore.ownsMetadata(impl_->metadataSourcePath)) {
    impl_->managedMetadataPath = impl_->metadataSourcePath;
  }
  if (impl_->resumeLoaded) {
    return;
  }
  impl_->resumeLoaded = true;

  loadResume(stateStore);
}

void BtDownload::restoreMetadataFromResume()
{
  populateDownloadContext(group_->getDownloadContext(),
                          group_->getOption().get());
  auto selected =
      util::parseIntSegments(group_->getOption()->get(PREF_SELECT_FILE));
  selected.normalize();
  group_->getDownloadContext()->setFileFilter(std::move(selected));
  updateFilePaths(group_->getDownloadContext(), group_->getOption().get());
  updateSelection(group_->getDownloadContext());
  restoreResumeProgress();
  completionNotified_ = completionNotified_ || snapshot_.selectedComplete;

  if (group_->isPauseRequested()) {
    snapshot_.state = BtSnapshot::State::Paused;
  }

  if (source_ == Source::Magnet &&
      group_->getOption()->getAsBool(PREF_ENABLE_RPC) &&
      group_->getOption()->getAsBool(PREF_PAUSE_METADATA)) {
    beginFileSelectionPause();
    shutdownStage_ = ShutdownStage::Complete;
    group_->setPauseRequested(true);
  }
}

void BtDownload::loadResume(BtStateStore& stateStore)
{
  const auto& identity = !snapshot_.infoHashV1.empty() ? snapshot_.infoHashV1
                                                       : snapshot_.infoHashV2;
  impl_->resumePath = stateStore.resumePath(identity);
  const auto resumeData = BtStateStore::readResume(impl_->resumePath);
  if (resumeData.empty()) {
    return;
  }
  lt::error_code error;
  auto restored = lt::read_resume_data(resumeData, error);
  if (error) {
    A2_LOG_WARN(fmt("Ignoring BitTorrent resume data %s: %s",
                    impl_->resumePath.c_str(), error.message().c_str()));
    return;
  }

  if (!hashesMatch(impl_->params.info_hashes, restored.info_hashes) ||
      (restored.ti &&
       !hashesMatch(impl_->params.info_hashes, restored.ti->info_hashes()))) {
    A2_LOG_WARN(
        fmt("Ignoring BitTorrent resume data with mismatched hashes: %s",
            impl_->resumePath.c_str()));
    return;
  }

  const bool sourceHasMetadata = static_cast<bool>(impl_->params.ti);
  if (sourceHasMetadata) {
    restored.ti = impl_->params.ti;
  }
  else if (restored.ti) {
    impl_->sourceTrackers = trackerSpecs(restored, BtTrackerOrigin::Resume);
  }
  if (impl_->params.info_hashes.has_v1() ||
      impl_->params.info_hashes.has_v2()) {
    restored.info_hashes = impl_->params.info_hashes;
  }
  restored.save_path = impl_->params.save_path;
  restored.max_connections = impl_->params.max_connections;
  restored.max_uploads = impl_->params.max_uploads;
  restored.upload_limit = impl_->params.upload_limit;
  restored.download_limit = impl_->params.download_limit;
  impl_->params = std::move(restored);
  completionNotified_ = impl_->params.completed_time != 0 ||
                        impl_->params.finished_time > 0 ||
                        impl_->params.seeding_time > 0;
  beginProgressVerification();
  snapshot_.allTimeDownload =
      std::max<int64_t>(0, impl_->params.total_downloaded);
  snapshot_.allTimeUpload = std::max<int64_t>(0, impl_->params.total_uploaded);
  snapshot_.activeTime = std::max(0, impl_->params.active_time);
  snapshot_.finishedTime = std::max(0, impl_->params.finished_time);
  snapshot_.seedingTime = std::max(0, impl_->params.seeding_time);
  snapshot_.complete =
      impl_->params.ti &&
      hasAllPieces(impl_->params.have_pieces, impl_->params.ti->num_pieces());
  snapshot_.selectedComplete =
      impl_->params.completed_time != 0 || snapshot_.complete;
  configure(group_->getOption().get());

  if (!impl_->params.ti) {
    return;
  }

  restoreMetadataFromResume();
}

} // namespace aria2
