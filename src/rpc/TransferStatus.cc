/* <!-- copyright */
/*
 * aria2 - The high speed download utility
 *
 * Copyright (C) 2009 Tatsuhiro Tsujikawa
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
#include "GroupId.h"
#include "TransferStat.h"
#include "ValueBase.h"
#include "error_code.h"
#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "common.h" // IWYU pragma: keep

#include "RpcStatus.h"
#include "RpcFields.h"
#include "DownloadEngine.h"
#include "DownloadResult.h"
#include "RequestGroup.h"
#include "DownloadContext.h"
#include "PieceStorage.h"
#include "FileEntry.h"
#include "Option.h"
#include "prefs.h"
#include "Ed2kAttribute.h"
#include "media/MediaDownload.h"
#include "CheckIntegrityEntry.h"
#include "support/Numbers.h"
#include "support/Encoding.h"
#include "a2functional.h"
#include "fmt.h"
#ifdef ENABLE_BITTORRENT
#  include "BtDownload.h"
#  include "BtSession.h"
#  include "BtMetadata.h"
#endif

namespace aria2::rpc {

using namespace fields;
using namespace detail;

namespace detail {
bool requested_key(const std::vector<std::string>& keys, const std::string& k)
{
  return keys.empty() || std::find(keys.begin(), keys.end(), k) != keys.end();
}

void gatherMedia(Dict* entry, const media::Snapshot& snapshot)
{
  auto info = Dict::g();
  info->put("state", snapshot.state);
  info->put("protocol", snapshot.protocol);
  info->put("live", snapshot.live ? VLB_TRUE : VLB_FALSE);
  info->put("duration", util::itos(snapshot.duration));
  info->put("completedDuration", util::itos(snapshot.completedDuration));
  info->put("downloadedLength", util::itos(snapshot.downloadedLength));
  if (!snapshot.live && snapshot.duration > 0)
    info->put("progress", fmt("%.6f", snapshot.progress()));
  info->put("lengthKnown", snapshot.totalLength > 0 ? VLB_TRUE : VLB_FALSE);
  info->put("error", snapshot.error);
  info->put("errorCode", media::failureCodeName(snapshot.errorCode));
  auto tracks = List::g();
  for (const auto& track : snapshot.tracks) {
    auto item = Dict::g();
    item->put("id", track.id);
    item->put("type", track.type);
    item->put("language", track.language);
    item->put("codec", track.codec);
    item->put("width", util::itos(track.width));
    item->put("height", util::itos(track.height));
    item->put("bandwidth", util::itos(track.bandwidth));
    item->put("frameRate", fmt("%.6f", track.frameRate));
    item->put("selected", track.selected ? VLB_TRUE : VLB_FALSE);
    tracks->append(std::move(item));
  }
  info->put("tracks", std::move(tracks));
  entry->put("media", std::move(info));
}
void gatherProgress(Dict* entryDict, const std::shared_ptr<RequestGroup>& group,
                    DownloadEngine* e, const std::vector<std::string>& keys)
{
  gatherProgressCommon(entryDict, group, keys);
  if (group->getMediaDownload() && requested_key(keys, "media"))
    gatherMedia(entryDict, group->getMediaDownload()->snapshot());
#ifdef ENABLE_BITTORRENT
  if (group->getDownloadContext()->hasAttribute(CTX_ATTR_BT)) {
    gatherProgressBitTorrent(entryDict, group, keys);
  }
#endif // ENABLE_BITTORRENT
  if (e->getCheckIntegrityMan()) {
    if (e->getCheckIntegrityMan()->isPicked(
            [&group](const CheckIntegrityEntry& ent) {
              return ent.getRequestGroup() == group.get();
            })) {
      entryDict->put(
          KEY_VERIFIED_LENGTH,
          util::itos(
              e->getCheckIntegrityMan()->getPickedEntry()->getCurrentLength()));
    }
    if (e->getCheckIntegrityMan()->isQueued(
            [&group](const CheckIntegrityEntry& ent) {
              return ent.getRequestGroup() == group.get();
            })) {
      entryDict->put(KEY_VERIFY_PENDING, VLB_TRUE);
    }
  }
}

} // namespace detail

void gatherProgressCommon(Dict* entryDict,
                          const std::shared_ptr<RequestGroup>& group,
                          const std::vector<std::string>& keys)
{
  auto& ps = group->getPieceStorage();
  if (requested_key(keys, KEY_GID)) {
    entryDict->put(KEY_GID, GroupId::toHex(group->getGID()).c_str());
  }
  if (requested_key(keys, KEY_TOTAL_LENGTH)) {
    // This is "filtered" total length if --select-file is used.
    entryDict->put(KEY_TOTAL_LENGTH, util::itos(group->getTotalLength()));
  }
  if (requested_key(keys, KEY_COMPLETED_LENGTH)) {
    // This is "filtered" total length if --select-file is used.
    entryDict->put(KEY_COMPLETED_LENGTH,
                   util::itos(group->getCompletedLength()));
  }
  TransferStat stat = group->calculateStat();
  if (requested_key(keys, KEY_DOWNLOAD_SPEED)) {
    entryDict->put(KEY_DOWNLOAD_SPEED, util::itos(stat.downloadSpeed));
  }
  if (requested_key(keys, KEY_UPLOAD_SPEED)) {
    entryDict->put(KEY_UPLOAD_SPEED, util::itos(stat.uploadSpeed));
  }
  if (requested_key(keys, KEY_UPLOAD_LENGTH)) {
    entryDict->put(KEY_UPLOAD_LENGTH, util::itos(stat.allTimeUploadLength));
  }
  if (requested_key(keys, KEY_SEEDER)) {
    entryDict->put(KEY_SEEDER, group->isSeeder() ? VLB_TRUE : VLB_FALSE);
  }
  if (requested_key(keys, KEY_CONNECTIONS)) {
    entryDict->put(KEY_CONNECTIONS, util::itos(group->getNumConnection()));
  }
  if (requested_key(keys, KEY_BITFIELD)) {
#ifdef ENABLE_BITTORRENT
    if (group->getBtDownload() &&
        !group->getBtDownload()->snapshot().bitfield.empty()) {
      entryDict->put(KEY_BITFIELD, group->getBtDownload()->snapshot().bitfield);
    }
    else
#endif
        if (ps) {
      if (ps->getBitfieldLength() > 0) {
        entryDict->put(KEY_BITFIELD,
                       util::toHex(ps->getBitfield(), ps->getBitfieldLength()));
      }
    }
  }
  auto& dctx = group->getDownloadContext();
  if (requested_key(keys, KEY_PIECE_LENGTH)) {
    entryDict->put(KEY_PIECE_LENGTH, util::itos(dctx->getPieceLength()));
  }
  if (requested_key(keys, KEY_NUM_PIECES)) {
    entryDict->put(KEY_NUM_PIECES, util::uitos(dctx->getNumPieces()));
  }
  if (requested_key(keys, KEY_FOLLOWED_BY)) {
    if (!group->followedBy().empty()) {
      auto list = List::g();
      // The element is GID.
      for (auto& gid : group->followedBy()) {
        list->append(GroupId::toHex(gid));
      }
      entryDict->put(KEY_FOLLOWED_BY, std::move(list));
    }
  }
  if (requested_key(keys, KEY_FOLLOWING)) {
    if (group->following()) {
      entryDict->put(KEY_FOLLOWING, GroupId::toHex(group->following()));
    }
  }
  if (requested_key(keys, KEY_BELONGS_TO)) {
    if (group->belongsTo()) {
      entryDict->put(KEY_BELONGS_TO, GroupId::toHex(group->belongsTo()));
    }
  }
  if (requested_key(keys, KEY_FILES)) {
    auto files = List::g();
#ifdef ENABLE_BITTORRENT
    if (group->getBtDownload()) {
      createBtFileEntry(files.get(), group->getBtDownload()->snapshot());
    }
    else
#endif // ENABLE_BITTORRENT
    {
      createFileEntry(files.get(), dctx->getFileEntries(),
                      group->getFileCompletedLengths());
    }
    entryDict->put(KEY_FILES, std::move(files));
  }
  if (requested_key(keys, KEY_DIR)) {
    entryDict->put(KEY_DIR, group->getOption()->get(PREF_DIR));
  }
  if (requested_key(keys, KEY_ED2K)) {
    if (dctx->hasAttribute(CTX_ATTR_ED2K)) {
      auto attrs = getEd2kAttrs(dctx);
      entryDict->put(KEY_ED2K,
                     createEd2kStatusEntry(attrs, group->getRequestGroupMan(),
                                           group->getEd2kSharingTime()));
    }
  }
}

void gatherStoppedDownload(Dict* entryDict,
                           const std::shared_ptr<DownloadResult>& ds,
                           const std::vector<std::string>& keys)
{
  if (!ds->mediaSnapshot.protocol.empty() && requested_key(keys, "media"))
    gatherMedia(entryDict, ds->mediaSnapshot);
  if (requested_key(keys, KEY_GID)) {
    entryDict->put(KEY_GID, ds->gid->toHex());
  }
  if (requested_key(keys, KEY_ERROR_CODE)) {
    entryDict->put(KEY_ERROR_CODE, util::itos(static_cast<int>(ds->result)));
  }
  if (requested_key(keys, KEY_ERROR_MESSAGE)) {
    entryDict->put(KEY_ERROR_MESSAGE, ds->resultMessage);
  }
  if (requested_key(keys, KEY_STATUS)) {
    if (ds->result == error_code::REMOVED) {
      entryDict->put(KEY_STATUS, VLB_REMOVED);
    }
    else if (ds->result == error_code::FINISHED) {
      entryDict->put(KEY_STATUS, VLB_COMPLETE);
    }
    else {
      entryDict->put(KEY_STATUS, VLB_ERROR);
    }
  }
  if (requested_key(keys, KEY_FOLLOWED_BY)) {
    if (!ds->followedBy.empty()) {
      auto list = List::g();
      // The element is GID.
      for (auto gid : ds->followedBy) {
        list->append(GroupId::toHex(gid));
      }
      entryDict->put(KEY_FOLLOWED_BY, std::move(list));
    }
  }
  if (requested_key(keys, KEY_FOLLOWING)) {
    if (ds->following) {
      entryDict->put(KEY_FOLLOWING, GroupId::toHex(ds->following));
    }
  }
  if (requested_key(keys, KEY_BELONGS_TO)) {
    if (ds->belongsTo) {
      entryDict->put(KEY_BELONGS_TO, GroupId::toHex(ds->belongsTo));
    }
  }
  if (requested_key(keys, KEY_FILES)) {
    auto files = List::g();
#ifdef ENABLE_BITTORRENT
    if (!ds->btSnapshot.files.empty()) {
      createBtFileEntry(files.get(), ds->btSnapshot);
    }
    else
#endif
    {
      createFileEntry(files.get(), ds->fileEntries, ds->fileCompletedLengths);
    }
    entryDict->put(KEY_FILES, std::move(files));
  }
  if (requested_key(keys, KEY_TOTAL_LENGTH)) {
    entryDict->put(KEY_TOTAL_LENGTH, util::itos(ds->totalLength));
  }
  if (requested_key(keys, KEY_COMPLETED_LENGTH)) {
    entryDict->put(KEY_COMPLETED_LENGTH, util::itos(ds->completedLength));
  }
  if (requested_key(keys, KEY_UPLOAD_LENGTH)) {
    entryDict->put(KEY_UPLOAD_LENGTH, util::itos(ds->uploadLength));
  }
  if (requested_key(keys, KEY_BITFIELD)) {
    if (!ds->bitfield.empty()) {
      entryDict->put(KEY_BITFIELD, util::toHex(ds->bitfield));
    }
  }
  if (requested_key(keys, KEY_DOWNLOAD_SPEED)) {
    entryDict->put(KEY_DOWNLOAD_SPEED, VLB_ZERO);
  }
  if (requested_key(keys, KEY_UPLOAD_SPEED)) {
    entryDict->put(KEY_UPLOAD_SPEED, VLB_ZERO);
  }
  if (!ds->infoHash.empty()) {
    if (requested_key(keys, KEY_INFO_HASH)) {
      entryDict->put(KEY_INFO_HASH, util::toHex(ds->infoHash));
    }
    if (requested_key(keys, KEY_NUM_SEEDERS)) {
      entryDict->put(KEY_NUM_SEEDERS, VLB_ZERO);
    }
  }
  if (requested_key(keys, KEY_PIECE_LENGTH)) {
    entryDict->put(KEY_PIECE_LENGTH, util::itos(ds->pieceLength));
  }
  if (requested_key(keys, KEY_NUM_PIECES)) {
    entryDict->put(KEY_NUM_PIECES, util::uitos(ds->numPieces));
  }
  if (requested_key(keys, KEY_CONNECTIONS)) {
    entryDict->put(KEY_CONNECTIONS, VLB_ZERO);
  }
  if (requested_key(keys, KEY_DIR)) {
    entryDict->put(KEY_DIR, ds->dir);
  }

#ifdef ENABLE_BITTORRENT
  if (ds->attrs.size() > CTX_ATTR_BT && ds->attrs[CTX_ATTR_BT]) {
    const auto attrs = static_cast<BtMetadata*>(ds->attrs[CTX_ATTR_BT].get());
    if (requested_key(keys, KEY_BITTORRENT)) {
      auto btDict = Dict::g();
      gatherBitTorrentMetadata(btDict.get(), ds->btSnapshot, attrs);
      entryDict->put(KEY_BITTORRENT, std::move(btDict));
    }
  }
#endif // ENABLE_BITTORRENT
}

} // namespace aria2::rpc
