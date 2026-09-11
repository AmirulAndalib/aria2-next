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
#ifdef _WIN32
#  include <windows.h>
#endif
#include "a2netcompat.h"
#include "gai_strerror.h"
#include <_timeval.h>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <memory>
#include <psdk_inc/_fd_types.h>
#include "SocketCore.h"
#include "platform/SocketOps.h"
#include "platform/SocketAddress.h"
#include "DlRetryEx.h"
#include "DlAbortEx.h"
#include "Log.h"
#include "message.h"
#include "fmt.h"
#include "support/Numbers.h"
#include "support/Network.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <sstream>

#ifdef ENABLE_SSL
#  include "TLSSession.h"
#  include "TLSContext.h"
#endif

namespace aria2 {

using namespace socket_ops;

bool SocketCore::isWritable(time_t timeout)
{
#ifdef HAVE_POLL
  struct pollfd p;
  p.fd = sockfd_;
  p.events = POLLOUT;
  int r;
  while ((r = poll(&p, 1, timeout * 1000)) == -1 && errno == EINTR)
    ;
  int errNum = lastError();
  if (r > 0) {
    return p.revents & (POLLOUT | POLLHUP | POLLERR);
  }
  if (r == 0) {
    return false;
  }
  throw DL_RETRY_EX(fmt(EX_SOCKET_CHECK_WRITABLE, errorMsg(errNum).c_str()));
#else // !HAVE_POLL
#  ifndef __MINGW32__
  if (sockfd_ < 0 || FD_SETSIZE <= sockfd_) {
    A2_LOG_WARN("Detected file descriptor >= FD_SETSIZE or < 0. "
                "Download may slow down or fail.");
    return false;
  }
#  endif // !__MINGW32__
  fd_set fds;
  FD_ZERO(&fds);
  FD_SET(sockfd_, &fds);

  struct timeval tv;
  tv.tv_sec = timeout;
  tv.tv_usec = 0;

  int r = select(sockfd_ + 1, nullptr, &fds, nullptr, &tv);
  int errNum = lastError();
  if (r == 1) {
    return true;
  }
  if (r == 0) {
    // time out
    return false;
  }
  if (errNum == IN_PROGRESS || errNum == INTERRUPTED) {
    return false;
  }
  throw DL_RETRY_EX(fmt(EX_SOCKET_CHECK_WRITABLE, errorMsg(errNum).c_str()));
#endif   // !HAVE_POLL
}

bool SocketCore::isReadable(time_t timeout)
{
#ifdef HAVE_POLL
  struct pollfd p;
  p.fd = sockfd_;
  p.events = POLLIN;
  int r;
  while ((r = poll(&p, 1, timeout * 1000)) == -1 && errno == EINTR)
    ;
  int errNum = lastError();
  if (r > 0) {
    return p.revents & (POLLIN | POLLHUP | POLLERR);
  }
  if (r == 0) {
    return false;
  }
  throw DL_RETRY_EX(fmt(EX_SOCKET_CHECK_READABLE, errorMsg(errNum).c_str()));
#else // !HAVE_POLL
#  ifndef __MINGW32__
  if (sockfd_ < 0 || FD_SETSIZE <= sockfd_) {
    A2_LOG_WARN("Detected file descriptor >= FD_SETSIZE or < 0. "
                "Download may slow down or fail.");
    return false;
  }
#  endif // !__MINGW32__
  fd_set fds;
  FD_ZERO(&fds);
  FD_SET(sockfd_, &fds);

  struct timeval tv;
  tv.tv_sec = timeout;
  tv.tv_usec = 0;

  int r = select(sockfd_ + 1, &fds, nullptr, nullptr, &tv);
  int errNum = lastError();
  if (r == 1) {
    return true;
  }
  if (r == 0) {
    // time out
    return false;
  }
  if (errNum == IN_PROGRESS || errNum == INTERRUPTED) {
    return false;
  }
  throw DL_RETRY_EX(fmt(EX_SOCKET_CHECK_READABLE, errorMsg(errNum).c_str()));
#endif   // !HAVE_POLL
}

ssize_t SocketCore::writeVector(a2iovec* iov, size_t iovcnt)
{
  ssize_t ret = 0;
  wantRead_ = false;
  wantWrite_ = false;
  if (secure_ == TlsState::None) {
#ifdef __MINGW32__
    DWORD nsent;
    int rv = WSASend(sockfd_, iov, iovcnt, &nsent, 0, 0, 0);
    if (rv == 0) {
      ret = nsent;
    }
    else {
      ret = -1;
    }
#else  // !__MINGW32__
    while ((ret = writev(sockfd_, iov, iovcnt)) == -1 &&
           lastError() == INTERRUPTED)
      ;
#endif // !__MINGW32__
    int errNum = lastError();
    if (ret == -1) {
      if (!wouldBlock(errNum)) {
        throw DL_RETRY_EX(fmt(EX_SOCKET_SEND, errorMsg(errNum).c_str()));
      }
      wantWrite_ = true;
      ret = 0;
    }
  }
  else {
    // For SSL/TLS, we could not use writev, so just iterate vector
    // and write the data in normal way.
    for (size_t i = 0; i < iovcnt; ++i) {
      ssize_t rv = writeData(iov[i].A2IOVEC_BASE, iov[i].A2IOVEC_LEN);
      ret += rv;
      // OpenSSL may complete one record while the current buffer has a tail.
      // The returned count must describe a contiguous prefix of the vector.
      if (static_cast<size_t>(rv) < iov[i].A2IOVEC_LEN) {
        break;
      }
    }
  }
  return ret;
}

ssize_t SocketCore::writeData(const void* data, size_t len)
{
  ssize_t ret = 0;
  wantRead_ = false;
  wantWrite_ = false;

  if (secure_ == TlsState::None) {
    // Cast for Windows send()
    while ((ret = send(sockfd_, reinterpret_cast<const char*>(data), len, 0)) ==
               -1 &&
           lastError() == INTERRUPTED)
      ;
    int errNum = lastError();
    if (ret == -1) {
      if (!wouldBlock(errNum)) {
        throw DL_RETRY_EX(fmt(EX_SOCKET_SEND, errorMsg(errNum).c_str()));
      }
      wantWrite_ = true;
      ret = 0;
    }
  }
  else {
#ifdef ENABLE_SSL
    ret = tlsSession_->writeData(data, len);
    if (ret < 0) {
      if (ret != TLS_ERR_WOULDBLOCK) {
        throw DL_RETRY_EX(
            fmt(EX_SOCKET_SEND, tlsSession_->getLastErrorString().c_str()));
      }
      if (tlsSession_->checkDirection() == TLS_WANT_READ) {
        wantRead_ = true;
      }
      else {
        wantWrite_ = true;
      }
      ret = 0;
    }
#endif // ENABLE_SSL
  }
  return ret;
}

void SocketCore::readData(void* data, size_t& len)
{
  ssize_t ret = 0;
  wantRead_ = false;
  wantWrite_ = false;

  if (secure_ == TlsState::None) {
    // Cast for Windows recv()
    while ((ret = recv(sockfd_, reinterpret_cast<char*>(data), len, 0)) == -1 &&
           lastError() == INTERRUPTED)
      ;
    int errNum = lastError();
    if (ret == -1) {
      if (!wouldBlock(errNum)) {
        throw DL_RETRY_EX(fmt(EX_SOCKET_RECV, errorMsg(errNum).c_str()));
      }
      wantRead_ = true;
      ret = 0;
    }
  }
  else {
#ifdef ENABLE_SSL
    ret = tlsSession_->readData(data, len);
    if (ret < 0) {
      if (ret != TLS_ERR_WOULDBLOCK) {
        throw DL_RETRY_EX(
            fmt(EX_SOCKET_RECV, tlsSession_->getLastErrorString().c_str()));
      }
      if (tlsSession_->checkDirection() == TLS_WANT_READ) {
        wantRead_ = true;
      }
      else {
        wantWrite_ = true;
      }
      ret = 0;
    }
#endif // ENABLE_SSL
  }

  len = ret;
}

ssize_t SocketCore::writeData(const void* data, size_t len,
                              const std::string& host, uint16_t port)
{
  wantRead_ = false;
  wantWrite_ = false;

  struct addrinfo* res;
  int s;
  s = callGetaddrinfo(&res, host.c_str(), util::uitos(port).c_str(),
                      protocolFamily_, sockType_, 0, 0);
  if (s) {
    throw DL_ABORT_EX(fmt(EX_SOCKET_SEND, gai_strerror(s)));
  }
  std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> resDeleter(res,
                                                                freeaddrinfo);
  struct addrinfo* rp;
  ssize_t r = -1;
  int errNum = 0;
  for (rp = res; rp; rp = rp->ai_next) {
    // Cast for Windows sendto()
    while ((r = sendto(sockfd_, reinterpret_cast<const char*>(data), len, 0,
                       rp->ai_addr, rp->ai_addrlen)) == -1 &&
           INTERRUPTED == lastError())
      ;
    errNum = lastError();
    if (r == static_cast<ssize_t>(len)) {
      break;
    }
    if (r == -1 && wouldBlock(errNum)) {
      wantWrite_ = true;
      r = 0;
      break;
    }
  }
  if (r == -1) {
    throw DL_ABORT_EX(fmt(EX_SOCKET_SEND, errorMsg(errNum).c_str()));
  }
  return r;
}

ssize_t SocketCore::readDataFrom(void* data, size_t len, Endpoint& sender)
{
  wantRead_ = false;
  wantWrite_ = false;
  sockaddr_union sockaddr;
  socklen_t sockaddrlen = sizeof(sockaddr);
  ssize_t r;
  // Cast for Windows recvfrom()
  while ((r = recvfrom(sockfd_, reinterpret_cast<char*>(data), len, 0,
                       &sockaddr.sa, &sockaddrlen)) == -1 &&
         INTERRUPTED == lastError())
    ;
  int errNum = lastError();
  if (r == -1) {
    if (!wouldBlock(errNum)) {
      throw DL_RETRY_EX(fmt(EX_SOCKET_RECV, errorMsg(errNum).c_str()));
    }
    wantRead_ = true;
    r = 0;
  }
  else {
    sender = util::getNumericNameInfo(&sockaddr.sa, sockaddrlen);
  }

  return r;
}

bool SocketCore::wantRead() const { return wantRead_; }

bool SocketCore::wantWrite() const { return wantWrite_; }

} // namespace aria2
