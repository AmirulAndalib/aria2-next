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
#include <iterator>
#include <memory>
#include <vector>
#include "RequestGroupMan.h"
#include "RecoverableException.h"
#include "DownloadContext.h"
#include "FileEntry.h"
#include "Ed2kSession.h"
#include "Log.h"
#include "message.h"
#include "fmt.h"
#include <algorithm>
#include <cassert>
#include <functional>

namespace aria2 {

void RequestGroupMan::checkpointActiveDownloads()
{
  for (auto& rg : requestGroups_) {
    if (rg->getDownloadContext()->hasAttribute(CTX_ATTR_ED2K)) {
      try {
        if (!ed2kSession_->checkpointDownload(rg.get()) &&
            !ed2kSession_->databasePath().empty()) {
          A2_LOG_ERROR(fmt("Failed to checkpoint ED2K state for GID %s.",
                           GroupId::toHex(rg->getGID()).c_str()));
        }
      }
      catch (RecoverableException& e) {
        A2_LOG_ERROR_EX(EX_EXCEPTION_CAUGHT, e);
      }
    }
  }
}

void RequestGroupMan::closeFile()
{
  for (auto& elem : requestGroups_) {
    elem->closeFile();
  }
}

namespace {
template <typename StringInputIterator, typename FileEntryInputIterator>
bool sameFilePathExists(StringInputIterator sfirst, StringInputIterator slast,
                        FileEntryInputIterator ffirst,
                        FileEntryInputIterator flast)
{
  for (; ffirst != flast; ++ffirst) {
    if (std::binary_search(sfirst, slast, (*ffirst)->getPath())) {
      return true;
    }
  }
  return false;
}

} // namespace

bool RequestGroupMan::isSameFileBeingDownloaded(
    RequestGroup* requestGroup) const
{
  // TODO it may be good to use dedicated method rather than use
  // isPreLocalFileCheckEnabled
  if (!requestGroup->isPreLocalFileCheckEnabled()) {
    return false;
  }
  std::vector<std::string> files;
  for (auto& rg : requestGroups_) {
    if (rg.get() != requestGroup) {
      const std::vector<std::shared_ptr<FileEntry>>& entries =
          rg->getDownloadContext()->getFileEntries();
      std::transform(entries.begin(), entries.end(), std::back_inserter(files),
                     std::mem_fn(&FileEntry::getPath));
    }
  }
  std::sort(files.begin(), files.end());
  const std::vector<std::shared_ptr<FileEntry>>& entries =
      requestGroup->getDownloadContext()->getFileEntries();
  return sameFilePathExists(files.begin(), files.end(), entries.begin(),
                            entries.end());
}

} // namespace aria2
