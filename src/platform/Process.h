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
#ifndef D_PROCESS_H
#define D_PROCESS_H

#include "common.h"
#include "prefs.h"
#include <memory>
#include <string>
#include <signal.h>

namespace aria2 {
class RequestGroup;
class Option;
} // namespace aria2

namespace aria2::util {

#ifdef HAVE_SIGACTION
using SignalMask = sigset_t;
#else
using SignalMask = int;
#endif

typedef void (*signal_handler_t)(int);
void setGlobalSignalHandler(int signal, SignalMask* mask,
                            signal_handler_t handler, int flags);
// No throw
void executeHookByOptName(const std::shared_ptr<RequestGroup>& group,
                          const Option* option, PrefPtr pref);

// No throw
void executeHookByOptName(const RequestGroup* group, const Option* option,
                          PrefPtr pref);
// This function is basically the same with strerror(errNum) but when
// strerror returns NULL, this function returns empty string.
std::string safeStrerror(int errNum);
#ifdef __MINGW32__
// Formats error message for error code errNum, which is the return
// value of GetLastError().  On error, this function returns empty
// string.
std::string formatLastError(int errNum);
#endif // __MINGW32__

// Sets file descriptor file FD_CLOEXEC to |fd|.  This function is
// noop for Mingw32 build, since we disable inheritance in
// CreateProcess call.
void make_fd_cloexec(int fd);

#ifdef __MINGW32__
bool gainPrivilege(LPCTSTR privName);
#endif // __MINGW32__

} // namespace aria2::util

#endif // D_PROCESS_H
