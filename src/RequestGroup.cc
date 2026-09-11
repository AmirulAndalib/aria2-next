/* <!-- copyright */
/*
 * aria2 - The high speed download utility
 *
 * Copyright (C) 2006 Tatsuhiro Tsujikawa
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * In addition, as a special exception, the copyright holders give
 * permission to link the code of portions of this program with the
 * OpenSSL library under certain conditions as described in each
 * individual source file, and distribute linked combinations
 * including the two.
 * You must obey the GNU General Public License in all respects
 * for all of the code used other than OpenSSL.  If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so.  If you
 * do not wish to do so, delete this exception statement from your
 * version.  If you delete this exception statement from all source
 * files in the program, then also delete it here.
 */
/* copyright --> */
#include "RequestGroup.h"
#include "ContextAttribute.h"
#include "GroupId.h"
#include "TimeA2.h"
#include "TimerA2.h"
#include "error_code.h"
#include <exception>
#include <memory>
#include <utility>
#include <vector>
#include "DownloadEngine.h"
#include "DownloadContext.h"
#include "RequestGroupMan.h"
#include "Option.h"
#include "FileEntry.h"
#include "PieceStorage.h"
#include "SegmentMan.h"
#include "DiskWriterFactory.h"
#include "Dependency.h"
#include "CurlSession.h"
#include "CurlDownload.h"
#include "media/MediaDownload.h"
#include "Log.h"
#include "DownloadFailureException.h"
#include "prefs.h"
#include "fmt.h"
#include <algorithm>
#include <array>
#include <cassert>

#ifdef ENABLE_BITTORRENT
#  include "BtDownload.h"
#  include "BtSession.h"
#endif

namespace aria2 {

RequestGroup::RequestGroup(const std::shared_ptr<GroupId>& gid,
                           const std::shared_ptr<Option>& option)
    : belongsToGID_(0),
      gid_(gid),
      option_(option),
      requestGroupMan_(nullptr),
      followingGID_(0),
      lastModifiedTime_(Time::null()),
      timeout_(option->getAsInt(PREF_TIMEOUT)),
      state_(STATE_WAITING),
      numConcurrentCommand_(1),
      numStreamConnection_(0),
      numStreamCommand_(0),
      numCommand_(0),
      fileNotFoundCount_(0),
      maxDownloadSpeedLimit_(option->getAsInt(PREF_MAX_DOWNLOAD_LIMIT)),
      maxUploadSpeedLimit_(option->getAsInt(PREF_MAX_UPLOAD_LIMIT)),
      resumeFailureCount_(0),
      haltReason_(RequestGroup::NONE),
      lastErrorCode_(error_code::UNDEFINED),
      preLocalFileCheckEnabled_(true),
      haltRequested_(false),
      forceHaltRequested_(false),
      pauseRequested_(false),
      restartRequested_(false),
      inMemoryDownload_(false),
      seedOnly_(false)
{
  fileAllocationEnabled_ = option_->get(PREF_FILE_ALLOCATION) != V_NONE;
  if (!option_->getAsBool(PREF_DRY_RUN)) {
    initializePreDownloadHandler();
    initializePostDownloadHandler();
  }
}

void RequestGroup::createInitialCommand(
    std::vector<std::unique_ptr<Command>>& commands, DownloadEngine* e)
{
  // Start session timer here.  When file size becomes known, it will
  // be reset again in *FileAllocationEntry, because hash check and
  // file allocation takes a time.  For downloads in which file size
  // is unknown, session timer will not be reset.
  downloadContext_->resetDownloadStartTime();
  if (mediaDownload_) {
    try {
      commands.push_back(mediaDownload_->start(this, e));
    }
    catch (const std::exception& error) {
      throw DOWNLOAD_FAILURE_EXCEPTION(error.what());
    }
    return;
  }
  if (curlDownload_) {
    commands.push_back(e->getCurlSession()->start(curlDownload_, this, e));
    return;
  }
  if (downloadContext_->hasAttribute(CTX_ATTR_ED2K)) {
    createEd2kCommands(commands, e);
    return;
  }
#ifdef ENABLE_BITTORRENT
  if (downloadContext_->hasAttribute(CTX_ATTR_BT)) {
    if (!btDownload_) {
      throw DOWNLOAD_FAILURE_EXCEPTION(
          "BitTorrent download is missing its libtorrent state.");
    }
    if (option_->getAsBool(PREF_DRY_RUN)) {
      throw DOWNLOAD_FAILURE_EXCEPTION(
          "Cancel BitTorrent download in dry-run context.");
    }
    commands.push_back(e->getBtSession()->start(btDownload_, this, e));
    return;
  }
#endif // ENABLE_BITTORRENT
  throw DOWNLOAD_FAILURE_EXCEPTION("Download has no transport backend.");
}

void RequestGroup::increaseStreamCommand() { ++numStreamCommand_; }

void RequestGroup::decreaseStreamCommand() { --numStreamCommand_; }

void RequestGroup::increaseStreamConnection() { ++numStreamConnection_; }

void RequestGroup::decreaseStreamConnection() { --numStreamConnection_; }

void RequestGroup::increaseNumCommand() { ++numCommand_; }

void RequestGroup::decreaseNumCommand()
{
  --numCommand_;
  if (!numCommand_ && requestGroupMan_) {
    A2_LOG_TRACE(fmt("GID#%s - Request queue check", gid_->toHex().c_str()));
    requestGroupMan_->requestQueueCheck();
  }
}

void RequestGroup::setHaltRequested(bool f, HaltReason haltReason)
{
  haltRequested_ = f;
  if (haltRequested_) {
    pauseRequested_ = false;
    haltReason_ = haltReason;
    if (!numCommand_ && requestGroupMan_) {
      A2_LOG_TRACE(fmt("GID#%s - Request queue check", gid_->toHex().c_str()));
      requestGroupMan_->requestQueueCheck();
    }
  }
  synchronizeEd2kSharingTime();
}

void RequestGroup::setForceHaltRequested(bool f, HaltReason haltReason)
{
  setHaltRequested(f, haltReason);
  forceHaltRequested_ = f;
}

void RequestGroup::setPauseRequested(bool f)
{
  pauseRequested_ = f;
  synchronizeEd2kSharingTime();
}

void RequestGroup::setState(int state)
{
  state_ = state;
  synchronizeEd2kSharingTime();
}

void RequestGroup::setRestartRequested(bool f) { restartRequested_ = f; }

void RequestGroup::releaseRuntimeResource(DownloadEngine* e)
{
  if (mediaDownload_ && !mediaDownload_->stopped()) {
    mediaDownload_->stop(isPauseRequested() || isShutdownRequested());
  }
  if (curlDownload_ && !curlDownload_->stopped()) {
    e->getCurlSession()->stop(curlDownload_,
                              isPauseRequested() || isShutdownRequested());
  }
  if (pieceStorage_) {
    pieceStorage_->removeAdvertisedPiece(Timer::zero());
  }
  // Don't reset segmentMan_ and pieceStorage_ here to provide
  // progress information via RPC
  downloadContext_->releaseRuntimeResource();
  // Reset seedOnly_, so that we can handle pause/unpause-ing share-only
  // downloads with --detach-share-only.
  seedOnly_ = false;
}

void RequestGroup::setTimeout(std::chrono::seconds timeout)
{
  timeout_ = std::move(timeout);
}

void RequestGroup::setDownloadContext(
    const std::shared_ptr<DownloadContext>& downloadContext)
{
  downloadContext_ = downloadContext;
  if (downloadContext_) {
    downloadContext_->setOwnerRequestGroup(this);
  }
}

void RequestGroup::enableSeedOnly()
{
  synchronizeEd2kSharingTime();
  if (seedOnly_ || !option_->getAsBool(PREF_DETACH_SHARE_ONLY)) {
    return;
  }

  if (requestGroupMan_) {
    seedOnly_ = true;

    requestGroupMan_->decreaseNumActive();
    requestGroupMan_->requestQueueCheck();
  }
}

void RequestGroup::setPendingOption(std::shared_ptr<Option> option)
{
  pendingOption_ = std::move(option);
}

RequestGroup::~RequestGroup() = default;

} // namespace aria2
