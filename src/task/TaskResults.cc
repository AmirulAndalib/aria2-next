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
#include "error_code.h"
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include "RequestGroup.h"
#include "DownloadResult.h"
#include "DownloadContext.h"
#include "FileEntry.h"
#include "Option.h"
#include "PieceStorage.h"
#include "CurlDownload.h"
#include "media/MediaDownload.h"
#include "support/Encoding.h"
#include "support/Numbers.h"
#include "Log.h"
#include "prefs.h"
#include "message.h"
#include "fmt.h"
#include <algorithm>
#include <array>
#include <cassert>

#ifdef ENABLE_BITTORRENT
#  include "BtDownload.h"
#  include "BtSession.h"
#endif

namespace aria2 {

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

std::shared_ptr<DownloadResult> RequestGroup::createDownloadResult() const
{
  A2_LOG_TRACE(fmt("GID#%s - Creating DownloadResult.", gid_->toHex().c_str()));
  TransferStat st = calculateStat();
  auto res = std::make_shared<DownloadResult>();
  res->gid = gid_;
  if (mediaDownload_)
    res->mediaSnapshot = mediaDownload_->snapshot();
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
    if (!isUserRequestedHalt()) {
      res->btState = btDownload_->stateReference();
    }
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

} // namespace aria2
