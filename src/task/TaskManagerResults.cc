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
#include "GroupId.h"
#include "error_code.h"
#include <exception>
#include <memory>
#include "RequestGroupMan.h"
#include "ApplicationStatePath.h"
#include "media/MediaStore.h"
#include "Option.h"
#include "DlAbortEx.h"
#include "Log.h"
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

RequestGroupMan::DownloadStat RequestGroupMan::getDownloadStat() const
{
  int error = removedErrorResult_;
  int inprogress = 0;
  error_code::Value lastError = removedLastErrorResult_;
  for (auto& dr : downloadResults_) {

    if (dr->belongsTo != 0) {
      continue;
    }
    if (dr->result == error_code::IN_PROGRESS) {
      ++inprogress;
    }
    else if (dr->result != error_code::FINISHED &&
             dr->result != error_code::REMOVED) {
      ++error;
      lastError = dr->result;
    }
  }
  return DownloadStat(error, inprogress, reservedGroups_.size(), lastError);
}

std::shared_ptr<DownloadResult>
RequestGroupMan::findDownloadResult(a2_gid_t gid) const
{
  return downloadResults_.get(gid);
}

namespace {
void discardMediaResult(const std::shared_ptr<DownloadResult>& result)
{
  if (!result || result->mediaSnapshot.protocol.empty())
    return;
  try {
    media::Store::discard(state::mediaDirectory(result->option.get()),
                          result->gid->toHex());
  }
  catch (const std::exception& error) {
    throw DL_ABORT_EX(
        fmt("Cannot discard media recovery state: %s", error.what()));
  }
}

} // namespace

bool RequestGroupMan::removeDownloadResult(a2_gid_t gid)
{
  discardMediaResult(downloadResults_.get(gid));
  const auto removed = downloadResults_.remove(gid);
#ifdef ENABLE_BITTORRENT
  if (removed) {
    collectBtStateGarbage();
  }
#endif
  return removed;
}

void RequestGroupMan::addDownloadResult(
    const std::shared_ptr<DownloadResult>& dr)
{
  ++numStoppedTotal_;
  bool rv = downloadResults_.push_back(dr->gid->getNumericId(), dr);
  assert(rv);
  bool evicted = false;
  while (downloadResults_.size() > maxDownloadResult_) {
    // Save last encountered error code so that we can report it
    // later.
    const auto& dr = downloadResults_[0];
    if (dr->belongsTo == 0 && dr->result != error_code::FINISHED) {
      removedLastErrorResult_ = dr->result;
      ++removedErrorResult_;

      // Keep unfinished download result, so that we can save them by
      // SessionSerializer.
      if (option_->getAsBool(PREF_KEEP_UNFINISHED_DOWNLOAD_RESULT)) {
        if (dr->result != error_code::REMOVED ||
            dr->option->getAsBool(PREF_FORCE_SAVE)) {
          unfinishedDownloadResults_.push_back(dr);
        }
      }
    }
    downloadResults_.pop_front();
    evicted = true;
  }
#ifdef ENABLE_BITTORRENT
  if (evicted) {
    collectBtStateGarbage();
  }
#endif
}

void RequestGroupMan::purgeDownloadResult()
{
  for (const auto& result : downloadResults_)
    discardMediaResult(result);
  downloadResults_.clear();
#ifdef ENABLE_BITTORRENT
  collectBtStateGarbage();
#endif
}

#ifdef ENABLE_BITTORRENT
namespace {
void addBtStateReference(std::set<std::string>& paths,
                         const BtStateReference& reference)
{
  if (!reference.metadataPath.empty()) {
    paths.insert(reference.metadataPath);
  }
  if (!reference.resumePath.empty()) {
    paths.insert(reference.resumePath);
  }
}

} // namespace

#endif

#ifdef ENABLE_BITTORRENT
void RequestGroupMan::collectBtStateGarbage()
{
  if (!btStateStore_ || uriListParser_) {
    return;
  }
  std::set<std::string> referencedPaths;
  for (const auto& group : requestGroups_) {
    if (group->getBtDownload()) {
      addBtStateReference(referencedPaths,
                          group->getBtDownload()->stateReference());
    }
  }
  for (const auto& group : reservedGroups_) {
    if (group->getBtDownload()) {
      addBtStateReference(referencedPaths,
                          group->getBtDownload()->stateReference());
    }
  }
  for (const auto& result : downloadResults_) {
    addBtStateReference(referencedPaths, result->btState);
  }
  for (const auto& result : unfinishedDownloadResults_) {
    addBtStateReference(referencedPaths, result->btState);
  }
  btStateStore_->collect(referencedPaths);
}

#endif

} // namespace aria2
