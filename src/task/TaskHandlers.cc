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
#include <memory>
#include <vector>
#include "RequestGroup.h"
#include "PostDownloadHandler.h"
#include "MemoryBufferPreDownloadHandler.h"
#include "Dependency.h"
#include "Option.h"
#include "DownloadContext.h"
#include "download_handlers.h"
#include "RecoverableException.h"
#include "Log.h"
#include "prefs.h"
#include "message.h"
#include "fmt.h"
#include <algorithm>
#include <array>
#include <cassert>

namespace aria2 {

void RequestGroup::preDownloadProcessing()
{
  A2_LOG_TRACE(fmt("Finding PreDownloadHandler for path %s.",
                   getFirstFilePath().c_str()));
  try {
    for (const auto& pdh : preDownloadHandlers_) {
      if (pdh->canHandle(this)) {
        pdh->execute(this);
        return;
      }
    }
  }
  catch (RecoverableException& ex) {
    A2_LOG_ERROR_EX(EX_EXCEPTION_CAUGHT, ex);
    return;
  }

  A2_LOG_TRACE("No PreDownloadHandler found.");
  return;
}

void RequestGroup::postDownloadProcessing(
    std::vector<std::shared_ptr<RequestGroup>>& groups)
{
  A2_LOG_TRACE(fmt("Finding PostDownloadHandler for path %s.",
                   getFirstFilePath().c_str()));
  try {
    for (const auto& pdh : postDownloadHandlers_) {
      if (pdh->canHandle(this)) {
        pdh->getNextRequestGroups(groups, this);
        return;
      }
    }
  }
  catch (RecoverableException& ex) {
    A2_LOG_ERROR_EX(EX_EXCEPTION_CAUGHT, ex);
  }

  A2_LOG_TRACE("No PostDownloadHandler found.");
}

void RequestGroup::initializePreDownloadHandler()
{
#ifdef ENABLE_BITTORRENT
  if (option_->get(PREF_FOLLOW_TORRENT) == V_MEM) {
    preDownloadHandlers_.push_back(
        download_handlers::getBtPreDownloadHandler());
  }
#endif // ENABLE_BITTORRENT
#ifdef ENABLE_METALINK
  if (option_->get(PREF_FOLLOW_METALINK) == V_MEM) {
    preDownloadHandlers_.push_back(
        download_handlers::getMetalinkPreDownloadHandler());
  }
#endif // ENABLE_METALINK
}

void RequestGroup::initializePostDownloadHandler()
{
#ifdef ENABLE_BITTORRENT
  if (option_->getAsBool(PREF_FOLLOW_TORRENT) ||
      option_->get(PREF_FOLLOW_TORRENT) == V_MEM) {
    postDownloadHandlers_.push_back(
        download_handlers::getBtPostDownloadHandler());
  }
#endif // ENABLE_BITTORRENT
#ifdef ENABLE_METALINK
  if (option_->getAsBool(PREF_FOLLOW_METALINK) ||
      option_->get(PREF_FOLLOW_METALINK) == V_MEM) {
    postDownloadHandlers_.push_back(
        download_handlers::getMetalinkPostDownloadHandler());
  }
#endif // ENABLE_METALINK
}

bool RequestGroup::isDependencyResolved()
{
  if (!dependency_) {
    return true;
  }
  return dependency_->resolve();
}

void RequestGroup::dependsOn(const std::shared_ptr<Dependency>& dep)
{
  dependency_ = dep;
}

void RequestGroup::addPostDownloadHandler(const PostDownloadHandler* handler)
{
  postDownloadHandlers_.push_back(handler);
}

void RequestGroup::addPreDownloadHandler(const PreDownloadHandler* handler)
{
  preDownloadHandlers_.push_back(handler);
}

void RequestGroup::clearPostDownloadHandler() { postDownloadHandlers_.clear(); }

void RequestGroup::clearPreDownloadHandler() { preDownloadHandlers_.clear(); }

} // namespace aria2
