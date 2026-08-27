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

#include <cassert>
#include <algorithm>
#include <array>

#include "PostDownloadHandler.h"
#include "DownloadEngine.h"
#include "SegmentMan.h"
#include "Dependency.h"
#include "prefs.h"
#include "File.h"
#include "message.h"
#include "util.h"
#include "Log.h"
#include "DiskAdaptor.h"
#include "DiskWriterFactory.h"
#include "RecoverableException.h"
#include "CheckIntegrityCommand.h"
#include "UnknownLengthPieceStorage.h"
#include "DownloadContext.h"
#include "DlAbortEx.h"
#include "DownloadFailureException.h"
#include "RequestGroupMan.h"
#include "DefaultPieceStorage.h"
#include "download_handlers.h"
#include "Ed2kAttribute.h"
#include "Ed2kCommand.h"
#include "Ed2kListenCommand.h"
#include "CurlDownload.h"
#include "CurlSession.h"
#include "Ed2kSession.h"
#include "Ed2kKadCommand.h"
#include "ed2k_hash.h"
#include "SeedCheckCommand.h"
#include "SeedCriteria.h"
#include "ShareRatioSeedCriteria.h"
#include "Ed2kSharingTimeSeedCriteria.h"
#include "UnionSeedCriteria.h"
#include "wallclock.h"
#include "MemoryBufferPreDownloadHandler.h"
#include "DownloadHandlerConstants.h"
#include "Option.h"
#include "FileEntry.h"
#include "Request.h"
#include "FileAllocationIterator.h"
#include "fmt.h"
#include "PieceSelector.h"
#include "a2functional.h"
#include "SocketCore.h"
#include "SimpleRandomizer.h"
#include "Segment.h"
#include "SocketRecvBuffer.h"
#include "RequestGroupCriteria.h"
#include "CheckIntegrityCommand.h"
#include "ChecksumCheckIntegrityEntry.h"
#ifdef ENABLE_BITTORRENT
#  include "BtDownload.h"
#  include "BtSession.h"
#endif // ENABLE_BITTORRENT
#ifdef ENABLE_METALINK
#  include "MetalinkPostDownloadHandler.h"
#endif // ENABLE_METALINK

namespace aria2 {

namespace {

bool validateCompleteEd2kFile(PieceStorage* pieceStorage,
                              const Ed2kAttribute* attrs)
{
  if (!pieceStorage || !attrs || attrs->link.hash.size() != ed2k::HASH_LENGTH ||
      attrs->link.size <= 0) {
    return false;
  }
  auto disk = pieceStorage->getDiskAdaptor();
  if (!disk || disk->size() != attrs->link.size) {
    return false;
  }
  std::vector<std::string> pieceHashes;
  std::array<unsigned char, 64_k> buf;
  int64_t offset = 0;
  while (offset < attrs->link.size) {
    const auto partLength = static_cast<size_t>(
        std::min<int64_t>(ed2k::PIECE_LENGTH, attrs->link.size - offset));
    std::string part;
    part.reserve(partLength);
    size_t partRead = 0;
    while (partRead < partLength) {
      const auto requestLength = std::min(buf.size(), partLength - partRead);
      const auto nread =
          disk->readData(buf.data(), requestLength, offset + partRead);
      if (nread <= 0) {
        return false;
      }
      part.append(reinterpret_cast<const char*>(buf.data()),
                  static_cast<size_t>(nread));
      partRead += static_cast<size_t>(nread);
    }
    pieceHashes.push_back(ed2k::md4Digest(part));
    offset += static_cast<int64_t>(partLength);
  }
  return ed2k::rootHash(pieceHashes) == attrs->link.hash;
}

std::unique_ptr<SeedCriteria>
createEd2kSeedCriteria(const std::shared_ptr<Option>& option,
                       const std::shared_ptr<DownloadContext>& dctx,
                       const std::shared_ptr<PieceStorage>& pieceStorage,
                       RequestGroup* group)
{
  auto unionCri = make_unique<UnionSeedCriteria>();
  if (option->defined(PREF_SEED_TIME)) {
    unionCri->addSeedCriteria(make_unique<Ed2kSharingTimeSeedCriteria>(
        group, std::chrono::seconds(static_cast<int64_t>(
                   option->getAsDouble(PREF_SEED_TIME) * 60))));
  }
  const auto ratio = option->getAsDouble(PREF_SEED_RATIO);
  if (ratio > 0.0) {
    auto ratioCri = make_unique<ShareRatioSeedCriteria>(ratio, dctx);
    ratioCri->setPieceStorage(pieceStorage);
    unionCri->addSeedCriteria(std::move(ratioCri));
  }
  if (unionCri->getSeedCriterion().empty()) {
    return nullptr;
  }
  return std::move(unionCri);
}

} // namespace

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

RequestGroup::~RequestGroup() = default;

bool RequestGroup::isCheckIntegrityReady()
{
  return option_->getAsBool(PREF_CHECK_INTEGRITY) &&
         ((downloadContext_->isChecksumVerificationAvailable() &&
           downloadFinishedByFileLength()) ||
          downloadContext_->isPieceHashVerificationAvailable());
}

bool RequestGroup::downloadFinished() const
{
  if (curlDownload_) {
    return curlDownload_->snapshot().state == CurlSnapshot::State::Complete;
  }
#ifdef ENABLE_BITTORRENT
  if (btDownload_) {
    return btDownload_->snapshot().selectedComplete;
  }
#endif // ENABLE_BITTORRENT
  if (!pieceStorage_) {
    return false;
  }
  return pieceStorage_->downloadFinished();
}

bool RequestGroup::allDownloadFinished() const
{
  if (curlDownload_) {
    return curlDownload_->snapshot().state == CurlSnapshot::State::Complete;
  }
#ifdef ENABLE_BITTORRENT
  if (btDownload_) {
    return btDownload_->snapshot().complete;
  }
#endif // ENABLE_BITTORRENT
  if (!pieceStorage_) {
    return false;
  }
  return pieceStorage_->allDownloadFinished();
}

std::pair<error_code::Value, std::string> RequestGroup::downloadResult() const
{
  if (downloadFinished() && !downloadContext_->isChecksumVerificationNeeded()) {
    return std::make_pair(error_code::FINISHED, "");
  }

  if (haltReason_ == RequestGroup::USER_REQUEST) {
    return std::make_pair(error_code::REMOVED, "");
  }

  if (lastErrorCode_ == error_code::UNDEFINED) {
    if (haltReason_ == RequestGroup::SHUTDOWN_SIGNAL) {
      return std::make_pair(error_code::IN_PROGRESS, "");
    }
    return std::make_pair(error_code::UNKNOWN_ERROR, "");
  }

  return std::make_pair(lastErrorCode_, lastErrorMessage_);
}

void RequestGroup::closeFile()
{
  if (pieceStorage_) {
    pieceStorage_->flushWrDiskCacheEntry(true);
    pieceStorage_->getDiskAdaptor()->flushOSBuffers();
    pieceStorage_->getDiskAdaptor()->closeFile();
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
  if (curlDownload_) {
    commands.push_back(e->getCurlSession()->start(curlDownload_, this, e));
    return;
  }
  if (downloadContext_->hasAttribute(CTX_ATTR_ED2K)) {
    if (option_->getAsBool(PREF_DRY_RUN)) {
      throw DOWNLOAD_FAILURE_EXCEPTION(
          "Cancel ED2K download in dry-run context.");
    }
    if (e->getRequestGroupMan()->isSameFileBeingDownloaded(this)) {
      throw DOWNLOAD_FAILURE_EXCEPTION2(
          fmt(EX_DUPLICATE_FILE_DOWNLOAD,
              downloadContext_->getBasePath().c_str()),
          error_code::DUPLICATE_DOWNLOAD);
    }
    initPieceStorage();
    auto ed2kSession = e->getRequestGroupMan()->getEd2kSession();
    auto attrs = getEd2kAttrs(downloadContext_);
    const auto stateResult = ed2kSession->loadDownloadState(this);
    if (stateResult == ed2k::DownloadStateLoadResult::Loaded) {
      pieceStorage_->getDiskAdaptor()->openFile();
    }
    else if (stateResult == ed2k::DownloadStateLoadResult::Error) {
      throw DOWNLOAD_FAILURE_EXCEPTION(
          "Failed to load persistent ED2K download state.");
    }
    else if (pieceStorage_->getDiskAdaptor()->fileExists()) {
      pieceStorage_->getDiskAdaptor()->enableReadOnly();
      pieceStorage_->getDiskAdaptor()->openExistingFile();
      if (validateCompleteEd2kFile(pieceStorage_.get(), attrs)) {
        pieceStorage_->markAllPiecesDone();
      }
      else {
        pieceStorage_->getDiskAdaptor()->closeFile();
        pieceStorage_->getDiskAdaptor()->disableReadOnly();
        shouldCancelDownloadForSafety();
        pieceStorage_->getDiskAdaptor()->openFile();
      }
    }
    else {
      pieceStorage_->getDiskAdaptor()->openFile();
    }
    const auto hasDiscoveryData =
        !attrs->servers.empty() ||
        (attrs->kadRoutingTable && attrs->kadRoutingTable->liveSize() > 0);
    if (attrs->searchActive && !hasDiscoveryData) {
      throw DOWNLOAD_FAILURE_EXCEPTION("ED2K search requires discovery data.");
    }
    attrs->pieceHashes = attrs->link.pieceHashes;
    attrs->aichRootHash = attrs->link.aichHash;
    attrs->aichRootTrusted = !attrs->aichRootHash.empty();
    for (const auto& source : attrs->link.sources) {
      addEd2kPeer(attrs, source, ed2k::PEER_SOURCE_INLINE);
    }
    ed2kSession->registerDownload(this);
    schedulePendingEd2kServers(this, e);
    for (const auto& peer : attrs->peers) {
      commands.push_back(
          make_unique<Ed2kCommand>(e->newCUID(), this, e, peer, false));
    }
    if (downloadFinished()) {
      enableSeedOnly();
    }
    if (!e->isEd2kTcpListenActive()) {
      auto listenCommand =
          make_unique<Ed2kListenCommand>(e->newCUID(), e, AF_INET);
      if (listenCommand->bindPort(static_cast<uint16_t>(
              option_->getAsInt(PREF_ED2K_LISTEN_PORT)))) {
        e->addCommand(std::move(listenCommand));
      }
    }
    if (!e->isEd2kUdpActive()) {
      commands.push_back(make_unique<Ed2kKadCommand>(e->newCUID(), this, e));
    }
    if (auto seedCriteria = createEd2kSeedCriteria(option_, downloadContext_,
                                                   pieceStorage_, this)) {
      auto seedCheck = make_unique<SeedCheckCommand>(e->newCUID(), this, e,
                                                     std::move(seedCriteria));
      seedCheck->setPieceStorage(pieceStorage_);
      commands.push_back(std::move(seedCheck));
    }
    if (commands.empty()) {
      throw DOWNLOAD_FAILURE_EXCEPTION(
          "ED2K download requires at least one server or source.");
    }
    e->setNoWait(true);
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

void RequestGroup::initPieceStorage()
{
  std::shared_ptr<PieceStorage> tempPieceStorage;
  if (downloadContext_->knowsTotalLength() &&
      // Following conditions are needed for chunked encoding with
      // content-length = 0. Google's dl server used this before.
      downloadContext_->getTotalLength() > 0) {
    auto ps =
        std::make_shared<DefaultPieceStorage>(downloadContext_, option_.get());
    if (requestGroupMan_) {
      ps->setWrDiskCache(requestGroupMan_->getWrDiskCache());
    }
    if (diskWriterFactory_) {
      ps->setDiskWriterFactory(diskWriterFactory_);
    }
    tempPieceStorage = ps;
  }
  else {
    auto ps = std::make_shared<UnknownLengthPieceStorage>(downloadContext_);
    if (diskWriterFactory_) {
      ps->setDiskWriterFactory(diskWriterFactory_);
    }
    tempPieceStorage = ps;
  }
  tempPieceStorage->initStorage();
  if (requestGroupMan_) {
    tempPieceStorage->getDiskAdaptor()->setOpenedFileCounter(
        requestGroupMan_->getOpenedFileCounter());
  }
  segmentMan_ =
      std::make_shared<SegmentMan>(downloadContext_, tempPieceStorage);
  pieceStorage_ = tempPieceStorage;
}

void RequestGroup::dropPieceStorage()
{
  segmentMan_.reset();
  pieceStorage_.reset();
}

bool RequestGroup::downloadFinishedByFileLength()
{
  // Compare the existing payload when no persisted piece map is available.
  if (!isPreLocalFileCheckEnabled() ||
      option_->getAsBool(PREF_ALLOW_OVERWRITE)) {
    return false;
  }
  if (!downloadContext_->knowsTotalLength()) {
    return false;
  }
  File outfile(getFirstFilePath());
  if (outfile.exists() &&
      downloadContext_->getTotalLength() == outfile.size()) {
    return true;
  }
  return false;
}

void RequestGroup::shouldCancelDownloadForSafety()
{
  if (option_->getAsBool(PREF_ALLOW_OVERWRITE)) {
    return;
  }
  File outfile(getFirstFilePath());
  if (!outfile.exists()) {
    return;
  }

  tryAutoFileRenaming();
  A2_LOG_INFO(fmt(MSG_FILE_RENAMED, getFirstFilePath().c_str()));
}

void RequestGroup::tryAutoFileRenaming()
{
  if (!option_->getAsBool(PREF_AUTO_FILE_RENAMING)) {
    throw DOWNLOAD_FAILURE_EXCEPTION2(
        fmt(MSG_FILE_ALREADY_EXISTS, getFirstFilePath().c_str()),
        error_code::FILE_ALREADY_EXISTS);
  }

  std::string filepath = getFirstFilePath();
  if (filepath.empty()) {
    throw DOWNLOAD_FAILURE_EXCEPTION2(
        fmt("File renaming failed: %s", getFirstFilePath().c_str()),
        error_code::FILE_RENAMING_FAILED);
  }
  auto fn = filepath;
  std::string ext;
  const auto idx = fn.find_last_of(".");
  const auto slash = fn.find_last_of("\\/");
  // Do extract the extension, as in "file.ext" = "file" and ".ext",
  // but do not consider ".file" to be a file name without extension instead
  // of a blank file name and an extension of ".file"
  if (idx != std::string::npos &&
      // fn has no path component and starts with a dot, but has no extension
      // otherwise
      idx != 0 &&
      // has a file path component if we found a slash.
      // if slash == idx - 1 this means a form of "*/.*", so the file name
      // starts with a dot, has no extension otherwise, and therefore do not
      // extract an extension either
      (slash == std::string::npos || slash < idx - 1)) {
    ext = fn.substr(idx);
    fn = fn.substr(0, idx);
  }
  for (int i = 1; i < 10000; ++i) {
    auto newfilename = fmt("%s.%d%s", fn.c_str(), i, ext.c_str());
    File newfile(newfilename);
    if (!newfile.exists()) {
      downloadContext_->getFirstFileEntry()->setPath(newfile.getPath());
      return;
    }
  }
  throw DOWNLOAD_FAILURE_EXCEPTION2(
      fmt("File renaming failed: %s", getFirstFilePath().c_str()),
      error_code::FILE_RENAMING_FAILED);
}

void RequestGroup::createNextCommandWithAdj(
    std::vector<std::unique_ptr<Command>>& commands, DownloadEngine* e,
    int numAdj)
{
  int numCommand;
  if (getTotalLength() == 0) {
    numCommand = 1 + numAdj;
  }
  else {
    numCommand = std::min(downloadContext_->getNumPieces(),
                          static_cast<size_t>(numConcurrentCommand_));
    numCommand += numAdj;
  }

  if (numCommand > 0) {
    createNextCommand(commands, e, numCommand);
  }
}

void RequestGroup::createNextCommand(
    std::vector<std::unique_ptr<Command>>& commands, DownloadEngine* e)
{
  int numCommand;
  if (getTotalLength() == 0) {
    if (numStreamCommand_ > 0) {
      numCommand = 0;
    }
    else {
      numCommand = 1;
    }
  }
  else if (numStreamCommand_ >= numConcurrentCommand_) {
    numCommand = 0;
  }
  else {
    numCommand = std::min(
        downloadContext_->getNumPieces(),
        static_cast<size_t>(numConcurrentCommand_ - numStreamCommand_));
  }

  if (numCommand > 0) {
    createNextCommand(commands, e, numCommand);
  }
}

void RequestGroup::createNextCommand(
    std::vector<std::unique_ptr<Command>>& commands, DownloadEngine* e,
    int numCommand)
{
  if (curlDownload_) {
    curlDownload_->synchronizeUris(
        downloadContext_->getFirstFileEntry()->getUris());
  }
  (void)commands;
  (void)e;
  (void)numCommand;
}

std::string RequestGroup::getFirstFilePath() const
{
  assert(downloadContext_);
  if (inMemoryDownload()) {
    return "[MEMORY]" +
           File(downloadContext_->getFirstFileEntry()->getPath()).getBasename();
  }
  return downloadContext_->getFirstFileEntry()->getPath();
}

int64_t RequestGroup::getTotalLength() const
{
  if (curlDownload_) {
    return curlDownload_->snapshot().totalLength;
  }
#ifdef ENABLE_BITTORRENT
  if (btDownload_) {
    return btDownload_->snapshot().totalLength;
  }
#endif // ENABLE_BITTORRENT
  if (!pieceStorage_) {
    return 0;
  }

  if (pieceStorage_->isSelectiveDownloadingMode()) {
    return pieceStorage_->getFilteredTotalLength();
  }

  return pieceStorage_->getTotalLength();
}

int64_t RequestGroup::getCompletedLength() const
{
  if (curlDownload_) {
    return curlDownload_->snapshot().completedLength;
  }
#ifdef ENABLE_BITTORRENT
  if (btDownload_) {
    return btDownload_->snapshot().completedLength;
  }
#endif // ENABLE_BITTORRENT
  if (!pieceStorage_) {
    return 0;
  }

  if (pieceStorage_->isSelectiveDownloadingMode()) {
    return pieceStorage_->getFilteredCompletedLength();
  }

  return pieceStorage_->getCompletedLength();
}

std::vector<int64_t> RequestGroup::getFileCompletedLengths() const
{
  const auto& files = downloadContext_->getFileEntries();
  std::vector<int64_t> completed(files.size(), 0);
  if (curlDownload_) {
    if (!completed.empty()) {
      completed[0] =
          std::max<int64_t>(0, curlDownload_->snapshot().completedLength);
    }
    return completed;
  }
#ifdef ENABLE_BITTORRENT
  if (btDownload_) {
    const auto& snapshots = btDownload_->snapshot().files;
    const auto count = std::min(completed.size(), snapshots.size());
    for (size_t index = 0; index < count; ++index) {
      completed[index] = std::clamp<int64_t>(snapshots[index].completedLength,
                                             0, files[index]->getLength());
    }
    return completed;
  }
#endif // ENABLE_BITTORRENT
  if (!pieceStorage_) {
    return completed;
  }
  for (size_t index = 0; index < files.size(); ++index) {
    completed[index] = pieceStorage_->getCompletedLength(
        files[index]->getOffset(), files[index]->getLength());
  }
  return completed;
}

void RequestGroup::validateFilename(const std::string& expectedFilename,
                                    const std::string& actualFilename) const
{
  if (expectedFilename.empty()) {
    return;
  }

  if (expectedFilename != actualFilename) {
    throw DL_ABORT_EX(fmt(EX_FILENAME_MISMATCH, expectedFilename.c_str(),
                          actualFilename.c_str()));
  }
}

void RequestGroup::validateTotalLength(int64_t expectedTotalLength,
                                       int64_t actualTotalLength) const
{
  if (expectedTotalLength <= 0) {
    return;
  }

  if (expectedTotalLength != actualTotalLength) {
    throw DL_ABORT_EX(
        fmt(EX_SIZE_MISMATCH, expectedTotalLength, actualTotalLength));
  }
}

void RequestGroup::validateFilename(const std::string& actualFilename) const
{
  validateFilename(downloadContext_->getFileEntries().front()->getBasename(),
                   actualFilename);
}

void RequestGroup::validateTotalLength(int64_t actualTotalLength) const
{
  validateTotalLength(getTotalLength(), actualTotalLength);
}

void RequestGroup::increaseStreamCommand() { ++numStreamCommand_; }

void RequestGroup::decreaseStreamCommand() { --numStreamCommand_; }

void RequestGroup::increaseStreamConnection() { ++numStreamConnection_; }

void RequestGroup::decreaseStreamConnection() { --numStreamConnection_; }

int RequestGroup::getNumConnection() const
{
  int numConnection = curlDownload_ ? curlDownload_->snapshot().connections
                                    : numStreamConnection_;
#ifdef ENABLE_BITTORRENT
  if (btDownload_) {
    numConnection += btDownload_->snapshot().numPeers;
  }
#endif // ENABLE_BITTORRENT
  return numConnection;
}

void RequestGroup::increaseNumCommand() { ++numCommand_; }

void RequestGroup::decreaseNumCommand()
{
  --numCommand_;
  if (!numCommand_ && requestGroupMan_) {
    A2_LOG_TRACE(fmt("GID#%s - Request queue check", gid_->toHex().c_str()));
    requestGroupMan_->requestQueueCheck();
  }
}

TransferStat RequestGroup::calculateStat() const
{
  auto stat = downloadContext_->getNetStat().toTransferStat();
  if (curlDownload_) {
    stat.sessionDownloadLength =
        curlDownload_->snapshot().sessionDownloadLength;
  }
#ifdef ENABLE_BITTORRENT
  else if (btDownload_) {
    stat.sessionDownloadLength = btDownload_->snapshot().allTimeDownload;
    stat.sessionUploadLength = btDownload_->snapshot().allTimeUpload;
    stat.allTimeUploadLength = btDownload_->snapshot().allTimeUpload;
  }
#endif // ENABLE_BITTORRENT
  if (state_ != STATE_ACTIVE || haltRequested_ || pauseRequested_ ||
      (curlDownload_ && curlDownload_->stopped())
#ifdef ENABLE_BITTORRENT
      || (btDownload_ && (btDownload_->stopped() || btDownload_->failed()))
#endif // ENABLE_BITTORRENT
  ) {
    stat.downloadSpeed = 0;
    stat.uploadSpeed = 0;
  }
  return stat;
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

void RequestGroup::synchronizeEd2kSharingTime()
{
  auto attrs = getEd2kAttrs(downloadContext_);
  if (!attrs) {
    return;
  }
  const auto active = state_ == STATE_ACTIVE && !haltRequested_ &&
                      !pauseRequested_ && downloadFinished();
  attrs->sharingTime.synchronize(active, global::wallclock());
}

int64_t RequestGroup::getEd2kSharingTime()
{
  auto attrs = getEd2kAttrs(downloadContext_);
  if (!attrs) {
    return 0;
  }
  synchronizeEd2kSharingTime();
  return attrs->sharingTime.seconds(global::wallclock());
}

void RequestGroup::setRestartRequested(bool f) { restartRequested_ = f; }

void RequestGroup::releaseRuntimeResource(DownloadEngine* e)
{
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

void RequestGroup::preDownloadProcessing()
{
  A2_LOG_TRACE(fmt("Finding PreDownloadHandler for path %s.",
                   getFirstFilePath().c_str()));
  try {
    for (const auto& pdh : preDownloadHandlers_) {
      if (pdh->canHandle(this)) {
        pdh->execute(this);
        return;
      }
    }
  }
  catch (RecoverableException& ex) {
    A2_LOG_ERROR_EX(EX_EXCEPTION_CAUGHT, ex);
    return;
  }

  A2_LOG_TRACE("No PreDownloadHandler found.");
  return;
}

void RequestGroup::postDownloadProcessing(
    std::vector<std::shared_ptr<RequestGroup>>& groups)
{
  A2_LOG_TRACE(fmt("Finding PostDownloadHandler for path %s.",
                   getFirstFilePath().c_str()));
  try {
    for (const auto& pdh : postDownloadHandlers_) {
      if (pdh->canHandle(this)) {
        pdh->getNextRequestGroups(groups, this);
        return;
      }
    }
  }
  catch (RecoverableException& ex) {
    A2_LOG_ERROR_EX(EX_EXCEPTION_CAUGHT, ex);
  }

  A2_LOG_TRACE("No PostDownloadHandler found.");
}

void RequestGroup::initializePreDownloadHandler()
{
#ifdef ENABLE_BITTORRENT
  if (option_->get(PREF_FOLLOW_TORRENT) == V_MEM) {
    preDownloadHandlers_.push_back(
        download_handlers::getBtPreDownloadHandler());
  }
#endif // ENABLE_BITTORRENT
#ifdef ENABLE_METALINK
  if (option_->get(PREF_FOLLOW_METALINK) == V_MEM) {
    preDownloadHandlers_.push_back(
        download_handlers::getMetalinkPreDownloadHandler());
  }
#endif // ENABLE_METALINK
}

void RequestGroup::initializePostDownloadHandler()
{
#ifdef ENABLE_BITTORRENT
  if (option_->getAsBool(PREF_FOLLOW_TORRENT) ||
      option_->get(PREF_FOLLOW_TORRENT) == V_MEM) {
    postDownloadHandlers_.push_back(
        download_handlers::getBtPostDownloadHandler());
  }
#endif // ENABLE_BITTORRENT
#ifdef ENABLE_METALINK
  if (option_->getAsBool(PREF_FOLLOW_METALINK) ||
      option_->get(PREF_FOLLOW_METALINK) == V_MEM) {
    postDownloadHandlers_.push_back(
        download_handlers::getMetalinkPostDownloadHandler());
  }
#endif // ENABLE_METALINK
}

bool RequestGroup::isDependencyResolved()
{
  if (!dependency_) {
    return true;
  }
  return dependency_->resolve();
}

void RequestGroup::dependsOn(const std::shared_ptr<Dependency>& dep)
{
  dependency_ = dep;
}

void RequestGroup::setDiskWriterFactory(
    const std::shared_ptr<DiskWriterFactory>& diskWriterFactory)
{
  diskWriterFactory_ = diskWriterFactory;
}

void RequestGroup::addPostDownloadHandler(const PostDownloadHandler* handler)
{
  postDownloadHandlers_.push_back(handler);
}

void RequestGroup::addPreDownloadHandler(const PreDownloadHandler* handler)
{
  preDownloadHandlers_.push_back(handler);
}

void RequestGroup::clearPostDownloadHandler() { postDownloadHandlers_.clear(); }

void RequestGroup::clearPreDownloadHandler() { preDownloadHandlers_.clear(); }

void RequestGroup::setPieceStorage(
    const std::shared_ptr<PieceStorage>& pieceStorage)
{
  pieceStorage_ = pieceStorage;
}

bool RequestGroup::needsFileAllocation() const
{
  return isFileAllocationEnabled() &&
         option_->getAsLLInt(PREF_NO_FILE_ALLOCATION_LIMIT) <=
             getTotalLength() &&
         !pieceStorage_->getDiskAdaptor()->fileAllocationIterator()->finished();
}

std::shared_ptr<DownloadResult> RequestGroup::createDownloadResult() const
{
  A2_LOG_TRACE(fmt("GID#%s - Creating DownloadResult.", gid_->toHex().c_str()));
  TransferStat st = calculateStat();
  auto res = std::make_shared<DownloadResult>();
  res->gid = gid_;
  res->attrs = downloadContext_->getAttributes();
  res->fileEntries = downloadContext_->getFileEntries();
  res->fileCompletedLengths = getFileCompletedLengths();
  res->inMemoryDownload = inMemoryDownload_;
  res->sessionDownloadLength = st.sessionDownloadLength;
  res->sessionTime = std::chrono::duration_cast<std::chrono::milliseconds>(
      downloadContext_->calculateSessionTime());

  auto result = downloadResult();
  res->result = result.first;
  res->resultMessage = result.second;
  res->followedBy = followedByGIDs_;
  res->following = followingGID_;
  res->belongsTo = belongsToGID_;
  res->option = option_;
  res->metadataInfo = metadataInfo_;
  res->totalLength = getTotalLength();
  res->completedLength = getCompletedLength();
  res->uploadLength = st.allTimeUploadLength;
  if (pieceStorage_ && pieceStorage_->getBitfieldLength() > 0) {
    res->bitfield.assign(pieceStorage_->getBitfield(),
                         pieceStorage_->getBitfield() +
                             pieceStorage_->getBitfieldLength());
  }
#ifdef ENABLE_BITTORRENT
  if (downloadContext_->hasAttribute(CTX_ATTR_BT)) {
    const auto& snapshot = btDownload_->snapshot();
    const auto& hash = !snapshot.infoHashV1.empty() ? snapshot.infoHashV1
                                                    : snapshot.infoHashV2;
    res->infoHash = util::fromHex(hash.begin(), hash.end());
    res->bitfield =
        util::fromHex(snapshot.bitfield.begin(), snapshot.bitfield.end());
    res->btSnapshot = snapshot;
  }
#endif // ENABLE_BITTORRENT
  res->pieceLength = downloadContext_->getPieceLength();
  res->numPieces = downloadContext_->getNumPieces();
  res->dir = option_->get(PREF_DIR);
  return res;
}

void RequestGroup::reportDownloadFinished()
{
  A2_LOG_INFO(fmt(MSG_FILE_DOWNLOAD_COMPLETED,
                  inMemoryDownload()
                      ? getFirstFilePath().c_str()
                      : downloadContext_->getBasePath().c_str()));
#ifdef ENABLE_BITTORRENT
  if (downloadContext_->hasAttribute(CTX_ATTR_BT)) {
    TransferStat stat = calculateStat();
    int64_t completedLength = getCompletedLength();
    double shareRatio = completedLength == 0
                            ? 0.0
                            : 1.0 * stat.allTimeUploadLength / completedLength;
    if (btDownload_ && btDownload_->hasMetadata()) {
      A2_LOG_INFO(fmt(MSG_SHARE_RATIO_REPORT, shareRatio,
                      util::abbrevSize(stat.allTimeUploadLength).c_str(),
                      util::abbrevSize(completedLength).c_str()));
    }
  }
#endif // ENABLE_BITTORRENT
}

void RequestGroup::applyLastModifiedTimeToLocalFiles()
{
  if (!pieceStorage_ || !lastModifiedTime_.good()) {
    return;
  }
  A2_LOG_DEBUG(fmt("Applying Last-Modified time: %s",
                   lastModifiedTime_.toHTTPDate().c_str()));
  size_t n = pieceStorage_->getDiskAdaptor()->utime(Time(), lastModifiedTime_);
  A2_LOG_DEBUG(fmt("Last-Modified attrs of %lu files were updated.",
                   static_cast<unsigned long>(n)));
}

void RequestGroup::updateLastModifiedTime(const Time& time)
{
  if (time.good() && lastModifiedTime_ < time) {
    lastModifiedTime_ = time;
  }
}

void RequestGroup::increaseAndValidateFileNotFoundCount()
{
  ++fileNotFoundCount_;
  const int maxCount = option_->getAsInt(PREF_MAX_FILE_NOT_FOUND);
  if (maxCount > 0 && fileNotFoundCount_ >= maxCount &&
      downloadContext_->getNetStat().getSessionDownloadLength() == 0) {
    throw DOWNLOAD_FAILURE_EXCEPTION2(
        fmt("Reached max-file-not-found count=%d", maxCount),
        error_code::MAX_FILE_NOT_FOUND);
  }
}

void RequestGroup::markInMemoryDownload() { inMemoryDownload_ = true; }

void RequestGroup::setTimeout(std::chrono::seconds timeout)
{
  timeout_ = std::move(timeout);
}

bool RequestGroup::doesDownloadSpeedExceed()
{
  int spd = downloadContext_->getNetStat().calculateDownloadSpeed();
  return maxDownloadSpeedLimit_ > 0 && maxDownloadSpeedLimit_ < spd;
}

bool RequestGroup::doesUploadSpeedExceed()
{
  int spd = downloadContext_->getNetStat().calculateUploadSpeed();
  return maxUploadSpeedLimit_ > 0 && maxUploadSpeedLimit_ < spd;
}

void RequestGroup::setDownloadContext(
    const std::shared_ptr<DownloadContext>& downloadContext)
{
  downloadContext_ = downloadContext;
  if (downloadContext_) {
    downloadContext_->setOwnerRequestGroup(this);
  }
}

bool RequestGroup::p2pInvolved() const
{
  if (downloadContext_->hasAttribute(CTX_ATTR_ED2K)) {
    return true;
  }
#ifdef ENABLE_BITTORRENT
  return downloadContext_->hasAttribute(CTX_ATTR_BT);
#else  // !ENABLE_BITTORRENT
  return false;
#endif // !ENABLE_BITTORRENT
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

bool RequestGroup::isSeeder() const
{
  if (downloadContext_->hasAttribute(CTX_ATTR_ED2K) && downloadFinished()) {
    return true;
  }
#ifdef ENABLE_BITTORRENT
  return btDownload_ && btDownload_->hasMetadata() &&
         btDownload_->snapshot().selectedComplete;
#else  // !ENABLE_BITTORRENT
  return false;
#endif // !ENABLE_BITTORRENT
}

void RequestGroup::setPendingOption(std::shared_ptr<Option> option)
{
  pendingOption_ = std::move(option);
}

} // namespace aria2
