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
#include "task/TaskEvents.h"
#include "aria2/aria2.h"
#include "error_code.h"
#include <memory>
#include "RequestGroup.h"
#include "Option.h"
#include "Notifier.h"
#include "SingletonHolder.h"
#include "platform/Process.h"
#include "prefs.h"
#include <cassert>

namespace aria2::task {

void notifyDownloadEvent(DownloadEvent event,
                         const std::shared_ptr<RequestGroup>& group)
{
  // The notifier is optional when no event consumers are installed.
  if (SingletonHolder<Notifier>::instance()) {
    SingletonHolder<Notifier>::instance()->notifyDownloadEvent(event, group);
  }
}

void executeStopHook(const std::shared_ptr<RequestGroup>& group,
                     const Option* option, error_code::Value result)
{
  PrefPtr hookPref = nullptr;
  if (!option->blank(PREF_ON_DOWNLOAD_STOP)) {
    hookPref = PREF_ON_DOWNLOAD_STOP;
  }
  if (result == error_code::FINISHED) {
    if (!option->blank(PREF_ON_DOWNLOAD_COMPLETE)) {
      hookPref = PREF_ON_DOWNLOAD_COMPLETE;
    }
  }
  else if (result != error_code::IN_PROGRESS && result != error_code::REMOVED) {
    if (!option->blank(PREF_ON_DOWNLOAD_ERROR)) {
      hookPref = PREF_ON_DOWNLOAD_ERROR;
    }
  }
  if (hookPref) {
    util::executeHookByOptName(group, option, hookPref);
  }

  if (result == error_code::FINISHED) {
    notifyDownloadEvent(EVENT_ON_DOWNLOAD_COMPLETE, group);
  }
  else if (result != error_code::IN_PROGRESS && result != error_code::REMOVED) {
    notifyDownloadEvent(EVENT_ON_DOWNLOAD_ERROR, group);
  }
  else {
    notifyDownloadEvent(EVENT_ON_DOWNLOAD_STOP, group);
  }
}

} // namespace aria2::task
