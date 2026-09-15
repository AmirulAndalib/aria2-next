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
#include "RequestGroupMan.h"
#include "GroupId.h"
#include "aria2/aria2.h"
#include "error_code.h"
#include <cstddef>
#include <memory>
#include <vector>
#include "ApplicationStatePath.h"
#include "DownloadContext.h"
#include "DownloadEngine.h"
#include "Ed2kSession.h"
#include "Ed2kUploadQueue.h"
#include "OpenedFileCounter.h"
#include "ServerStatMan.h"
#include "WrDiskCache.h"
#include "UriListParser.h"
#include "Option.h"
#include "DlAbortEx.h"
#include "a2functional.h"
#include "prefs.h"
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

namespace {
constexpr int DEFAULT_CORE_OPEN_FILES = 100;
} // namespace

RequestGroupMan::RequestGroupMan(
    std::vector<std::shared_ptr<RequestGroup>> requestGroups,
    int maxConcurrentDownloads, const Option* option)
    : maxConcurrentDownloads_(maxConcurrentDownloads),
      optimizeConcurrentDownloads_(false),
      optimizeConcurrentDownloadsCoeffA_(5.),
      optimizeConcurrentDownloadsCoeffB_(25.),
      optimizationSpeed_(0),
      numActive_(0),
      option_(option),
      serverStatMan_(std::make_shared<ServerStatMan>()),
      maxOverallDownloadSpeedLimit_(
          option->getAsInt(PREF_MAX_OVERALL_DOWNLOAD_LIMIT)),
      maxOverallUploadSpeedLimit_(
          option->getAsInt(PREF_MAX_OVERALL_UPLOAD_LIMIT)),
      keepRunning_(option->getAsBool(PREF_ENABLE_RPC)),
      queueCheck_(true),
      removedErrorResult_(0),
      removedLastErrorResult_(error_code::FINISHED),
      maxDownloadResult_(option->getAsInt(PREF_MAX_DOWNLOAD_RESULT)),
      openedFileCounter_(
          std::make_shared<OpenedFileCounter>(this, DEFAULT_CORE_OPEN_FILES)),
#ifdef ENABLE_BITTORRENT
      btStateStore_(make_unique<BtStateStore>(option)),
#endif
      ed2kUploadQueue_(make_unique<ed2k::UploadQueue>(
          option->getAsInt(PREF_ED2K_UPLOAD_SLOTS))),
      ed2kSession_(make_unique<ed2k::Ed2kSession>(
          ed2kUploadQueue_.get(), state::ed2kDatabaseFile(option))),
      numStoppedTotal_(0)
{
  setupOptimizeConcurrentDownloads();
  ed2kSession_->restoreDownloads(option, requestGroups);
  appendReservedGroups(requestGroups);
}

RequestGroupMan::~RequestGroupMan() { openedFileCounter_->deactivate(); }

bool RequestGroupMan::downloadFinished()
{
  if (keepRunning_) {
    return false;
  }
  return requestGroups_.empty() && reservedGroups_.empty();
}

void RequestGroupMan::addRequestGroup(
    const std::shared_ptr<RequestGroup>& group)
{
  ++numActive_;
  requestGroups_.push_back(group->getGID(), group);
}

void RequestGroupMan::addReservedGroup(
    const std::vector<std::shared_ptr<RequestGroup>>& groups)
{
  requestQueueCheck();
  appendReservedGroups(groups);
}

void RequestGroupMan::addReservedGroup(
    const std::shared_ptr<RequestGroup>& group)
{
  requestQueueCheck();
  reservedGroups_.push_back(group->getGID(), group);
}

void RequestGroupMan::insertReservedGroup(
    size_t pos, const std::vector<std::shared_ptr<RequestGroup>>& groups)
{
  requestQueueCheck();
  pos = std::min(reservedGroups_.size(), pos);
  reservedGroups_.insert(pos, std::mem_fn(&RequestGroup::getGID),
                         groups.begin(), groups.end());
}

void RequestGroupMan::insertReservedGroup(
    size_t pos, const std::shared_ptr<RequestGroup>& group)
{
  requestQueueCheck();
  pos = std::min(reservedGroups_.size(), pos);
  reservedGroups_.insert(pos, group->getGID(), group);
}

size_t RequestGroupMan::countRequestGroup() const
{
  return requestGroups_.size();
}

std::shared_ptr<RequestGroup> RequestGroupMan::findGroup(a2_gid_t gid) const
{
  std::shared_ptr<RequestGroup> rg = requestGroups_.get(gid);
  if (!rg) {
    rg = reservedGroups_.get(gid);
  }
  return rg;
}

size_t RequestGroupMan::changeReservedGroupPosition(a2_gid_t gid, int pos,
                                                    OffsetMode how)
{
  ssize_t dest = reservedGroups_.move(gid, pos, how);
  if (dest == -1) {
    throw DL_ABORT_EX(fmt("GID#%s not found in the waiting queue.",
                          GroupId::toHex(gid).c_str()));
  }
  else {
    return dest;
  }
}

bool RequestGroupMan::removeReservedGroup(a2_gid_t gid)
{
  const auto removed = reservedGroups_.remove(gid);
#ifdef ENABLE_BITTORRENT
  if (removed) {
    collectBtStateGarbage();
  }
#endif
  return removed;
}

void RequestGroupMan::halt()
{
  for (auto& elem : requestGroups_) {
    elem->setHaltRequested(true);
  }
}

void RequestGroupMan::forceHalt()
{
  for (auto& elem : requestGroups_) {
    elem->setForceHaltRequested(true);
  }
}

void RequestGroupMan::setUriListParser(
    const std::shared_ptr<UriListParser>& uriListParser)
{
  uriListParser_ = uriListParser;
}

void RequestGroupMan::initWrDiskCache()
{
  assert(!wrDiskCache_);
  size_t limit = option_->getAsInt(PREF_DISK_CACHE);
  if (limit > 0) {
    wrDiskCache_ = make_unique<WrDiskCache>(limit);
  }
}

void RequestGroupMan::decreaseNumActive()
{
  assert(numActive_ > 0);
  --numActive_;
}

} // namespace aria2
