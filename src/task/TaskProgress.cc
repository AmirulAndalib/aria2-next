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
#include "ContextAttribute.h"
#include "TransferStat.h"
#include <cstddef>
#include <cstdint>
#include <vector>
#include "RequestGroup.h"
#include "DownloadContext.h"
#include "FileEntry.h"
#include "Option.h"
#include "PieceStorage.h"
#include "CurlDownload.h"
#include "media/MediaDownload.h"
#include "prefs.h"
#include <algorithm>
#include <array>
#include <cassert>

#ifdef ENABLE_BITTORRENT
#  include "BtDownload.h"
#  include "BtSession.h"
#endif

namespace aria2 {

bool RequestGroup::downloadFinished() const
{
  if (mediaDownload_)
    return mediaDownload_->snapshot().state == "complete";
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
  if (mediaDownload_)
    return mediaDownload_->snapshot().state == "complete";
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

int64_t RequestGroup::getTotalLength() const
{
  if (mediaDownload_)
    return mediaDownload_->snapshot().totalLength;
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
  if (mediaDownload_)
    return mediaDownload_->snapshot().completedLength;
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
  if (mediaDownload_) {
    if (!completed.empty())
      completed[0] = mediaDownload_->snapshot().completedLength;
    return completed;
  }
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

int RequestGroup::getNumConnection() const
{
  if (mediaDownload_)
    return mediaDownload_->snapshot().connections;
  int numConnection = curlDownload_ ? curlDownload_->snapshot().connections
                                    : numStreamConnection_;
#ifdef ENABLE_BITTORRENT
  if (btDownload_) {
    numConnection += btDownload_->snapshot().numPeers;
  }
#endif // ENABLE_BITTORRENT
  return numConnection;
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
      (curlDownload_ && curlDownload_->stopped()) ||
      (mediaDownload_ && mediaDownload_->stopped())
#ifdef ENABLE_BITTORRENT
      || (btDownload_ && (btDownload_->stopped() || btDownload_->failed()))
#endif // ENABLE_BITTORRENT
  ) {
    stat.downloadSpeed = 0;
    stat.uploadSpeed = 0;
  }
  return stat;
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

} // namespace aria2
