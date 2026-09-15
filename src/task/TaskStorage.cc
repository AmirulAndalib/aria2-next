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
#include "FileEntry.h"
#include "Option.h"
#include "error_code.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include "RequestGroup.h"
#include "DownloadContext.h"
#include "File.h"
#include "DiskAdaptor.h"
#include "DiskWriterFactory.h"
#include "FileAllocationIterator.h"
#include "PieceStorage.h"
#include "DefaultPieceStorage.h"
#include "UnknownLengthPieceStorage.h"
#include "SegmentMan.h"
#include "RequestGroupMan.h"
#include "DownloadFailureException.h"
#include "DlAbortEx.h"
#include "Log.h"
#include "prefs.h"
#include "message.h"
#include "fmt.h"
#include <cassert>

namespace aria2 {

bool RequestGroup::isCheckIntegrityReady()
{
  return option_->getAsBool(PREF_CHECK_INTEGRITY) &&
         ((downloadContext_->isChecksumVerificationAvailable() &&
           downloadFinishedByFileLength()) ||
          downloadContext_->isPieceHashVerificationAvailable());
}

void RequestGroup::closeFile()
{
  if (pieceStorage_) {
    pieceStorage_->flushWrDiskCacheEntry(true);
    pieceStorage_->getDiskAdaptor()->flushOSBuffers();
    pieceStorage_->getDiskAdaptor()->closeFile();
  }
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

std::string RequestGroup::getFirstFilePath() const
{
  assert(downloadContext_);
  if (inMemoryDownload()) {
    return "[MEMORY]" +
           File(downloadContext_->getFirstFileEntry()->getPath()).getBasename();
  }
  return downloadContext_->getFirstFileEntry()->getPath();
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

void RequestGroup::validateFilename(const std::string& actualFilename) const
{
  validateFilename(downloadContext_->getFileEntries().front()->getBasename(),
                   actualFilename);
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

void RequestGroup::validateTotalLength(int64_t actualTotalLength) const
{
  validateTotalLength(getTotalLength(), actualTotalLength);
}

void RequestGroup::setDiskWriterFactory(
    const std::shared_ptr<DiskWriterFactory>& diskWriterFactory)
{
  diskWriterFactory_ = diskWriterFactory;
}

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

} // namespace aria2
