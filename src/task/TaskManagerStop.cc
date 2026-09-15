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
#include "GroupId.h"
#include "aria2/aria2.h"
#include "common.h"
#include <cstddef>
#include <memory>
#include <vector>
#include "RequestGroupMan.h"
#include "RecoverableException.h"
#include "DownloadEngine.h"
#include "DownloadContext.h"
#include "Ed2kSession.h"
#include "SegmentMan.h"
#include "PeerStat.h"
#include "ServerStat.h"
#include "Signature.h"
#include "Option.h"
#include "RequestGroupActions.h"
#include "task/TaskEvents.h"
#include "platform/Process.h"
#include "Log.h"
#include "prefs.h"
#include "message.h"
#include "fmt.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <functional>
#include <iomanip>
#include <set>
#include <sstream>

namespace aria2 {

using task::executeStopHook;
using task::notifyDownloadEvent;

void RequestGroupMan::removeStoppedGroup(DownloadEngine* e)
{
  ed2kSession_->detachStoppedDownloads();
  size_t numPrev = requestGroups_.size();
  requestGroups_.remove_if(
      [this, e](const auto& group) { return processStoppedGroup(group, e); });
  size_t numRemoved = numPrev - requestGroups_.size();
  if (numRemoved > 0) {
    A2_LOG_TRACE(fmt("%lu RequestGroup(s) deleted.",
                     static_cast<unsigned long>(numRemoved)));
#ifdef ENABLE_BITTORRENT
    collectBtStateGarbage();
#endif
  }
}

namespace {
void persistEd2kDownload(const std::shared_ptr<RequestGroup>& group,
                         ed2k::Ed2kSession* session)
{
  if (!group->getDownloadContext()->hasAttribute(CTX_ATTR_ED2K)) {
    return;
  }
  const auto discarded =
      group->isUserRequestedHalt() || group->isShareComplete();
  const auto saved = discarded ? session->discardDownload(group.get())
                               : session->checkpointDownload(group.get());
  if (!saved && !session->databasePath().empty()) {
    A2_LOG_ERROR(fmt("Failed to %s ED2K state for GID %s.",
                     discarded ? "discard" : "checkpoint",
                     GroupId::toHex(group->getGID()).c_str()));
  }
}

} // namespace

namespace {
void saveSignature(const std::shared_ptr<RequestGroup>& group)
{
  auto& sig = group->getDownloadContext()->getSignature();
  if (sig && !sig->getBody().empty()) {
    // filename of signature file is the path to download file followed by
    // ".sig".
    std::string signatureFile = group->getFirstFilePath() + ".sig";
    if (sig->save(signatureFile)) {
      A2_LOG_INFO(fmt(MSG_SIGNATURE_SAVED, signatureFile.c_str()));
    }
    else {
      A2_LOG_INFO(fmt(MSG_SIGNATURE_NOT_SAVED, signatureFile.c_str()));
    }
  }
}

} // namespace

namespace {
void collectStat(const std::shared_ptr<RequestGroup>& group,
                 RequestGroupMan& manager)
{
  if (group->getSegmentMan()) {
    bool singleConnection = group->getSegmentMan()->getPeerStats().size() == 1;
    const std::vector<std::shared_ptr<PeerStat>>& peerStats =
        group->getSegmentMan()->getFastestPeerStats();
    for (auto& stat : peerStats) {
      if (stat->getHostname().empty() || stat->getProtocol().empty()) {
        continue;
      }
      int speed = stat->getAvgDownloadSpeed();
      if (speed == 0)
        continue;

      std::shared_ptr<ServerStat> ss = manager.getOrCreateServerStat(
          stat->getHostname(), stat->getProtocol());
      ss->increaseCounter();
      ss->updateDownloadSpeed(speed);
      if (singleConnection) {
        ss->updateSingleConnectionAvgSpeed(speed);
      }
      else {
        ss->updateMultiConnectionAvgSpeed(speed);
      }
    }
  }
}

} // namespace

void RequestGroupMan::finishStoppedFiles(
    const std::shared_ptr<RequestGroup>& group)
{
  group->closeFile();
  if (group->isPauseRequested()) {
    if (!group->isRestartRequested()) {
      A2_LOG_INFO(fmt(_("Download GID#%s paused"),
                      GroupId::toHex(group->getGID()).c_str()));
    }
  }
  else if (group->downloadFinished() &&
           !group->getDownloadContext()->isChecksumVerificationNeeded()) {
    group->applyLastModifiedTimeToLocalFiles();
    group->reportDownloadFinished();
    if (group->allDownloadFinished() &&
        !group->getOption()->getAsBool(PREF_FORCE_SAVE)) {
      saveSignature(group);
    }
    std::vector<std::shared_ptr<RequestGroup>> nextGroups;
    group->postDownloadProcessing(nextGroups);
    if (!nextGroups.empty()) {
      A2_LOG_TRACE(fmt("Adding %lu RequestGroups as a result of"
                       " PostDownloadHandler.",
                       static_cast<unsigned long>(nextGroups.size())));
      insertReservedGroup(0, nextGroups);
    }
  }
  else {
    A2_LOG_INFO(fmt(_("Download GID#%s not complete: %s"),
                    GroupId::toHex(group->getGID()).c_str(),
                    group->getDownloadContext()->getBasePath().c_str()));
  }
  persistEd2kDownload(group, ed2kSession_.get());
}

bool RequestGroupMan::processStoppedGroup(
    const std::shared_ptr<RequestGroup>& group, DownloadEngine* e)
{
  if (group->getNumCommand() != 0) {
    return false;
  }
  collectStat(group, *this);
  const std::shared_ptr<DownloadContext>& dctx = group->getDownloadContext();

  if (group->isSeedOnlyEnabled() && dctx->hasAttribute(CTX_ATTR_ED2K) &&
      !group->isHaltRequested()) {
    return false;
  }

  if (!group->isSeedOnlyEnabled()) {
    decreaseNumActive();
  }

  // Failed and interrupted tasks also need a stop time for result statistics.
  if (dctx->getDownloadStopTime().isZero()) {
    dctx->resetDownloadStopTime();
  }
  try {
    finishStoppedFiles(group);
  }
  catch (RecoverableException& ex) {
    A2_LOG_ERROR_EX(EX_EXCEPTION_CAUGHT, ex);
  }
  if (group->isPauseRequested()) {
    group->setState(RequestGroup::STATE_WAITING);
    reservedGroups_.push_front(group->getGID(), group);
    group->releaseRuntimeResource(e);
    group->setForceHaltRequested(false);

    auto pendingOption = group->getPendingOption();
    if (pendingOption) {
      changeOption(group, *pendingOption, e);
    }

    if (group->isRestartRequested()) {
      group->setPauseRequested(false);
    }
    else {
      util::executeHookByOptName(group, e->getOption(), PREF_ON_DOWNLOAD_PAUSE);
      notifyDownloadEvent(EVENT_ON_DOWNLOAD_PAUSE, group);
    }
  }
  else {
    std::shared_ptr<DownloadResult> dr = group->createDownloadResult();
    addDownloadResult(dr);
    executeStopHook(group, e->getOption(), dr->result);
    group->releaseRuntimeResource(e);
  }

  group->setRestartRequested(false);
  group->setPendingOption(nullptr);

  return true;
}

} // namespace aria2
