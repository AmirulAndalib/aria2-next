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
#include "platform/SocketOps.h"
#include "a2netcompat.h"
#include <cstdio>
#include <string>
#include "SocketCore.h"
#include "platform/SocketAddress.h"
#include "Log.h"
#include "support/Text.h"
#include "platform/Process.h"
#include "message.h"
#include "fmt.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <sstream>

namespace aria2::socket_ops {

std::string errorMsg(int errNum)
{
#ifndef __MINGW32__
  return util::safeStrerror(errNum);
#else
  auto msg = util::formatLastError(errNum);
  if (msg.empty()) {
    char buf[256];
    snprintf(buf, sizeof(buf), EX_SOCKET_UNKNOWN_ERROR, errNum, errNum);
    return buf;
  }
  return msg;
#endif // __MINGW32__
}

void applySocketBufferSize(sock_t fd)
{
  auto recvBufSize = SocketCore::getSocketRecvBufferSize();
  if (recvBufSize == 0) {
    return;
  }

  if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, (a2_sockopt_t)&recvBufSize,
                 sizeof(recvBufSize)) < 0) {
    auto errNum = lastError();
    A2_LOG_WARN(fmt("Failed to set socket buffer size. Cause: %s",
                    errorMsg(errNum).c_str()));
  }
}

} // namespace aria2::socket_ops
