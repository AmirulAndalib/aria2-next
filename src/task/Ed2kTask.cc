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
#include "DiskAdaptor.h"
#include "Ed2kStore.h"
#include "a2netcompat.h"
#include "ed2k_peer.h"
#include "error_code.h"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>
#include "RequestGroup.h"
#include "DownloadEngine.h"
#include "DownloadContext.h"
#include "RequestGroupMan.h"
#include "Ed2kAttribute.h"
#include "Ed2kCommand.h"
#include "Ed2kListenCommand.h"
#include "Ed2kKadCommand.h"
#include "Ed2kSession.h"
#include "PieceStorage.h"
#include "Option.h"
#include "SeedCheckCommand.h"
#include "ShareRatioSeedCriteria.h"
#include "Ed2kSharingTimeSeedCriteria.h"
#include "UnionSeedCriteria.h"
#include "DownloadFailureException.h"
#include "ed2k_hash.h"
#include "a2functional.h"
#include "prefs.h"
#include "wallclock.h"
#include "message.h"
#include "fmt.h"
#include <algorithm>
#include <array>
#include <cassert>

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

} // namespace

namespace {
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

void RequestGroup::restoreEd2kFile(DownloadEngine* e)
{
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
}

void RequestGroup::createEd2kCommands(
    std::vector<std::unique_ptr<Command>>& commands, DownloadEngine* e)
{
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
  restoreEd2kFile(e);
  auto ed2kSession = e->getRequestGroupMan()->getEd2kSession();
  auto attrs = getEd2kAttrs(downloadContext_);
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
    if (listenCommand->bindPort(
            static_cast<uint16_t>(option_->getAsInt(PREF_ED2K_LISTEN_PORT)))) {
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
}

} // namespace aria2
