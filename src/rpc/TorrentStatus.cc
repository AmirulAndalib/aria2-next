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
#include "BtSnapshot.h"
#include "ContextAttribute.h"
#include "ValueBase.h"
#include "aria2/aria2.h"
#include <memory>
#include <utility>
#include <vector>
#include "common.h" // IWYU pragma: keep

#include "RpcStatus.h"
#include "RpcFields.h"
#include "RequestGroup.h"
#include "DownloadContext.h"
#include "BtDownload.h"
#include "BtMetadata.h"
#include "support/Numbers.h"
#include "fmt.h"

namespace aria2::rpc::detail {

using namespace fields;

void gatherBitTorrentMetadata(Dict* btDict, const BtSnapshot& snapshot,
                              const BtMetadata* attrs)
{
  if (!snapshot.magnetLink.empty()) {
    btDict->put(KEY_MAGNET_LINK, snapshot.magnetLink);
  }
  if (attrs && !attrs->comment.empty()) {
    btDict->put(KEY_COMMENT, attrs->comment);
  }
  if (attrs && attrs->creationDate) {
    btDict->put(KEY_CREATION_DATE, Integer::g(attrs->creationDate));
  }
  if (attrs && attrs->mode != BT_FILE_MODE_NONE) {
    btDict->put(KEY_MODE,
                attrs->mode == BT_FILE_MODE_MULTI ? "multi" : "single");
  }
  auto announceList = List::g();
  for (const auto& tier : snapshot.announceList) {
    auto outputTier = List::g();
    for (const auto& tracker : tier) {
      outputTier->append(tracker);
    }
    announceList->append(std::move(outputTier));
  }
  btDict->put(KEY_ANNOUNCE_LIST, std::move(announceList));
  auto webSeeds = List::g();
  for (const auto& webSeed : snapshot.webSeeds) {
    webSeeds->append(webSeed);
  }
  btDict->put("webSeeds", std::move(webSeeds));
  btDict->put(KEY_PRIVATE_TORRENT,
              snapshot.privateTorrent ? VLB_TRUE : VLB_FALSE);
  btDict->put("state", btStateName(snapshot.state));
  btDict->put("fileSelectionState",
              btFileSelectionStateName(snapshot.fileSelectionState));
  if (snapshot.error.present) {
    auto error = Dict::g();
    error->put("code", util::itos(snapshot.error.code));
    error->put("kind", snapshot.error.kind);
    error->put("category", snapshot.error.category);
    error->put("message", snapshot.error.message);
    error->put("recoverable",
               snapshot.error.recoverable ? VLB_TRUE : VLB_FALSE);
    if (!snapshot.error.operation.empty()) {
      error->put("operation", snapshot.error.operation);
    }
    if (!snapshot.error.file.empty()) {
      error->put("file", snapshot.error.file);
    }
    btDict->put("error", std::move(error));
  }
  if (!snapshot.infoHashV1.empty()) {
    btDict->put("infoHashV1", snapshot.infoHashV1);
  }
  if (!snapshot.infoHashV2.empty()) {
    btDict->put("infoHashV2", snapshot.infoHashV2);
  }
  if (!snapshot.currentTracker.empty()) {
    btDict->put("currentTracker", snapshot.currentTracker);
  }
  btDict->put("numPeers", util::itos(snapshot.numPeers));
  btDict->put("connectingPeers", util::itos(snapshot.connectingPeers));
  btDict->put("handshakingPeers", util::itos(snapshot.handshakingPeers));
  btDict->put("numSeeds", util::itos(snapshot.numSeeds));
  btDict->put("numComplete", util::itos(snapshot.numComplete));
  btDict->put("numIncomplete", util::itos(snapshot.numIncomplete));
  btDict->put("progress", fmt("%.6f", snapshot.progressPpm / 1000000.0));
  if (snapshot.availabilityPpm >= 0) {
    btDict->put("availability",
                fmt("%.6f", snapshot.availabilityPpm / 1000000.0));
  }
  btDict->put("failedLength", util::itos(snapshot.failedBytes));
  btDict->put("redundantLength", util::itos(snapshot.redundantBytes));
  btDict->put("activeTime", util::itos(snapshot.activeTime));
  btDict->put("finishedTime", util::itos(snapshot.finishedTime));
  btDict->put("seedingTime", util::itos(snapshot.seedingTime));
  btDict->put("connectCandidates", util::itos(snapshot.connectCandidates));
  btDict->put("uploadingPeers", util::itos(snapshot.numUploads));
  if (snapshot.hasMetadata) {
    auto info = Dict::g();
    info->put(KEY_NAME, snapshot.name);
    btDict->put(KEY_INFO, std::move(info));
  }
}

void gatherProgressBitTorrent(Dict* entryDict,
                              const std::shared_ptr<RequestGroup>& group,
                              const std::vector<std::string>& keys)
{
  const auto& download = group->getBtDownload();
  if (!download) {
    return;
  }
  const auto& snapshot = download->snapshot();
  if (requested_key(keys, KEY_INFO_HASH)) {
    entryDict->put(KEY_INFO_HASH, !snapshot.infoHashV1.empty()
                                      ? snapshot.infoHashV1
                                      : snapshot.infoHashV2);
  }
  if (requested_key(keys, KEY_BITTORRENT)) {
    auto bt = Dict::g();
    auto attrs = static_cast<BtMetadata*>(
        group->getDownloadContext()->getAttribute(CTX_ATTR_BT).get());
    gatherBitTorrentMetadata(bt.get(), snapshot, attrs);
    entryDict->put(KEY_BITTORRENT, std::move(bt));
  }
  if (requested_key(keys, KEY_NUM_SEEDERS)) {
    entryDict->put(KEY_NUM_SEEDERS, util::itos(snapshot.numSeeds));
  }
}

} // namespace aria2::rpc::detail
