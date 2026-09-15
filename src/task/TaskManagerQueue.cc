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
#include "a2functional.h"
#include "aria2/aria2.h"
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <utility>
#include <vector>
#include "RequestGroupMan.h"
#include "DownloadEngine.h"
#include "DownloadContext.h"
#include "CurlSession.h"
#include "media/MediaDownload.h"
#include "UriListParser.h"
#include "Option.h"
#include "Command.h"
#include "RecoverableException.h"
#include "download_helper.h"
#include "Log.h"
#include "task/TaskEvents.h"
#include "support/Numbers.h"
#include "platform/Process.h"
#include "wallclock.h"
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

#ifdef ENABLE_BITTORRENT
#  include "BtDownload.h"
#  include "BtSession.h"
#  include "BtStateStore.h"
#endif

namespace aria2 {

using task::notifyDownloadEvent;

bool RequestGroupMan::setupOptimizeConcurrentDownloads(void)
{
  optimizeConcurrentDownloads_ =
      option_->getAsBool(PREF_OPTIMIZE_CONCURRENT_DOWNLOADS);
  if (optimizeConcurrentDownloads_) {
    if (option_->defined(PREF_OPTIMIZE_CONCURRENT_DOWNLOADS_COEFFA)) {
      optimizeConcurrentDownloadsCoeffA_ = strtod(
          option_->get(PREF_OPTIMIZE_CONCURRENT_DOWNLOADS_COEFFA).c_str(),
          nullptr);
      optimizeConcurrentDownloadsCoeffB_ = strtod(
          option_->get(PREF_OPTIMIZE_CONCURRENT_DOWNLOADS_COEFFB).c_str(),
          nullptr);
    }
  }
  return optimizeConcurrentDownloads_;
}

void RequestGroupMan::fillRequestGroupFromReserver(DownloadEngine* e)
{
  removeStoppedGroup(e);

#ifdef ENABLE_BITTORRENT
  if (btStateStartupCollectionPending_ && !uriListParser_) {
    collectBtStateGarbage();
    btStateStartupCollectionPending_ = false;
  }
#endif

  if (keepRunning_) {
    for (const auto& group : reservedGroups_) {
      if (!group->isPauseRequested()) {
        continue;
      }
      if (group->getMediaDownload())
        group->getMediaDownload()->restore(group.get());
      if (group->getCurlDownload() && e->getCurlSession()) {
        e->getCurlSession()->restorePaused(group->getCurlDownload(),
                                           group.get());
      }
#ifdef ENABLE_BITTORRENT
      if (group->getBtDownload() && e->getBtSession()) {
        e->getBtSession()->restorePaused(group->getBtDownload(), group.get());
      }
#endif
    }
  }

  int maxConcurrentDownloads = optimizeConcurrentDownloads_
                                   ? optimizeConcurrentDownloads()
                                   : maxConcurrentDownloads_;

  if (static_cast<size_t>(maxConcurrentDownloads) <= numActive_) {
    return;
  }
  int count = 0;
  int num = maxConcurrentDownloads - numActive_;
  std::vector<std::shared_ptr<RequestGroup>> pending;

  while (count < num && (uriListParser_ || !reservedGroups_.empty())) {
    if (uriListParser_ && reservedGroups_.empty()) {
      std::vector<std::shared_ptr<RequestGroup>> groups;
      // May throw exception
      bool ok = createRequestGroupFromUriListParser(groups, option_,
                                                    uriListParser_.get());
      if (ok) {
        appendReservedGroups(groups);
      }
      else {
        uriListParser_.reset();
#ifdef ENABLE_BITTORRENT
        if (btStateStartupCollectionPending_) {
          collectBtStateGarbage();
          btStateStartupCollectionPending_ = false;
        }
#endif
        if (reservedGroups_.empty()) {
          break;
        }
      }
    }
    std::shared_ptr<RequestGroup> groupToAdd = *reservedGroups_.begin();
    reservedGroups_.pop_front();
    if (keepRunning_ && groupToAdd->isPauseRequested()) {
      if (groupToAdd->getMediaDownload())
        groupToAdd->getMediaDownload()->restore(groupToAdd.get());
      if (groupToAdd->getCurlDownload() && e->getCurlSession()) {
        e->getCurlSession()->restorePaused(groupToAdd->getCurlDownload(),
                                           groupToAdd.get());
      }
      pending.push_back(groupToAdd);
      continue;
    }
    if (!groupToAdd->isDependencyResolved()) {
      pending.push_back(groupToAdd);
      continue;
    }
    if (activateGroup(groupToAdd, e)) {
      ++count;
    }
  }
  if (!pending.empty()) {
    reservedGroups_.insert(reservedGroups_.begin(),
                           std::mem_fn(&RequestGroup::getGID), pending.begin(),
                           pending.end());
  }
  if (count > 0) {
    e->setNoWait(true);
    e->setRefreshInterval(std::chrono::milliseconds(0));
    A2_LOG_TRACE(fmt("%d RequestGroup(s) added.", count));
  }
}

void RequestGroupMan::reduceActiveDownloadsToLimit(DownloadEngine* e)
{
  removeStoppedGroup(e);

  int maxConcurrentDownloads = optimizeConcurrentDownloads_
                                   ? optimizeConcurrentDownloads()
                                   : maxConcurrentDownloads_;
  if (maxConcurrentDownloads < 0 ||
      numActive_ <= static_cast<size_t>(maxConcurrentDownloads)) {
    return;
  }

  size_t num = numActive_ - static_cast<size_t>(maxConcurrentDownloads);
  auto i = requestGroups_.end();
  while (num > 0 && i != requestGroups_.begin()) {
    --i;
    auto& group = *i;
    if (group->isSeedOnlyEnabled() || group->isHaltRequested() ||
        group->isPauseRequested()) {
      continue;
    }
    group->setHaltRequested(true, RequestGroup::NONE);
    group->setPauseRequested(true);
    group->setRestartRequested(true);
    e->setRefreshInterval(std::chrono::milliseconds(0));
    --num;
  }
}

int RequestGroupMan::optimizeConcurrentDownloads()
{
  // gauge the current speed
  int currentSpeed = getNetStat().calculateDownloadSpeed();

  const auto& now = global::wallclock();
  if (currentSpeed >= optimizationSpeed_) {
    optimizationSpeed_ = currentSpeed;
    optimizationSpeedTimer_ = now;
  }
  else if (std::chrono::duration_cast<std::chrono::seconds>(
               optimizationSpeedTimer_.difference(now)) >= 5_s) {
    // we keep using the reference speed for minimum 5 seconds so reset the
    // timer
    optimizationSpeedTimer_ = now;

    // keep the reference speed as long as the speed tends to augment or to
    // maintain itself within 10%
    if (currentSpeed >= 1.1 * getNetStat().calculateNewestDownloadSpeed(5)) {
      // else assume a possible congestion and record a new optimization speed
      // by dichotomy
      optimizationSpeed_ = (optimizationSpeed_ + currentSpeed) / 2.;
    }
  }

  if (optimizationSpeed_ <= 0) {
    return optimizeConcurrentDownloadsCoeffA_;
  }

  // apply the rule
  if ((maxOverallDownloadSpeedLimit_ > 0) &&
      (optimizationSpeed_ > maxOverallDownloadSpeedLimit_)) {
    optimizationSpeed_ = maxOverallDownloadSpeedLimit_;
  }
  int maxConcurrentDownloads =
      ceil(optimizeConcurrentDownloadsCoeffA_ +
           optimizeConcurrentDownloadsCoeffB_ *
               log10(optimizationSpeed_ * 8. / 1000000.));

  // bring the value in bound between 1 and the defined maximum
  maxConcurrentDownloads =
      std::min(std::max(1, maxConcurrentDownloads), maxConcurrentDownloads_);

  A2_LOG_TRACE(
      fmt("Max concurrent downloads optimized at %d (%lu currently active) "
          "[optimization speed %sB/s, current speed %sB/s]",
          maxConcurrentDownloads, static_cast<unsigned long>(numActive_),
          util::abbrevSize(optimizationSpeed_).c_str(),
          util::abbrevSize(currentSpeed).c_str()));

  return maxConcurrentDownloads;
}

void RequestGroupMan::appendReservedGroups(
    const std::vector<std::shared_ptr<RequestGroup>>& groups)
{
  for (const auto& group : groups) {
    reservedGroups_.push_back(group->getGID(), group);
  }
}

bool RequestGroupMan::activateGroup(const std::shared_ptr<RequestGroup>& group,
                                    DownloadEngine* e)
{
  bool started = false;
  // Drop pieceStorage here because paused download holds its
  // reference.
  group->dropPieceStorage();
  group->setRequestGroupMan(this);
  group->setState(RequestGroup::STATE_ACTIVE);
  ++numActive_;
  requestGroups_.push_back(group->getGID(), group);
  try {
    std::vector<std::unique_ptr<Command>> res;
    group->createInitialCommand(res, e);
    started = true;
    if (res.empty()) {
      requestQueueCheck();
    }
    else {
      e->addCommand(std::move(res));
    }
  }
  catch (RecoverableException& ex) {
    A2_LOG_ERROR_EX(EX_EXCEPTION_CAUGHT, ex);
    A2_LOG_TRACE("Deleting temporal commands.");
    group->setLastErrorCode(ex.getErrorCode(), ex.what());
    // Keep the failed task registered so stop processing can publish its
    // result.
    requestQueueCheck();
  }

  util::executeHookByOptName(group, e->getOption(), PREF_ON_DOWNLOAD_START);
  notifyDownloadEvent(EVENT_ON_DOWNLOAD_START, group);
  return started;
}

} // namespace aria2
