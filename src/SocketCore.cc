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
#include "SocketCore.h"
#include "a2netcompat.h"
#include <cstdint>
#include <memory>
#include <vector>
#include "platform/SocketOps.h"
#include "platform/SocketAddress.h"
#include "DlAbortEx.h"
#include "Log.h"
#include "support/Numbers.h"
#include "platform/Process.h"
#include "message.h"
#include "fmt.h"
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

int SocketCore::protocolFamily_ = AF_UNSPEC;
int SocketCore::ipDscp_ = 0;
std::vector<SockAddr> SocketCore::bindAddrs_;
std::vector<std::vector<SockAddr>> SocketCore::bindAddrsList_;
std::vector<std::vector<SockAddr>>::iterator SocketCore::bindAddrsListIt_;
int SocketCore::socketRecvBufferSize_ = 0;

SocketCore::SocketCore(int sockType) : sockType_(sockType), sockfd_(-1)
{
  init();
}

SocketCore::SocketCore(sock_t sockfd, int sockType)
    : sockType_(sockType), sockfd_(sockfd)
{
  init();
}

void SocketCore::init()
{
  blocking_ = true;
  secure_ = TlsState::None;

  wantRead_ = false;
  wantWrite_ = false;
}

SocketCore::~SocketCore() { closeConnection(); }

void SocketCore::create(int family, int protocol)
{
  int errNum;
  closeConnection();
  sock_t fd = socket(family, sockType_, protocol);
  errNum = lastError();
  if (fd == (sock_t)-1) {
    throw DL_ABORT_EX(
        fmt("Failed to create socket. Cause:%s", errorMsg(errNum).c_str()));
  }
  util::make_fd_cloexec(fd);
  int sockopt = 1;
  if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (a2_sockopt_t)&sockopt,
                 sizeof(sockopt)) < 0) {
    errNum = lastError();
    closeSocket(fd);
    throw DL_ABORT_EX(
        fmt("Failed to create socket. Cause:%s", errorMsg(errNum).c_str()));
  }

  applySocketBufferSize(fd);

  sockfd_ = fd;
}

static sock_t bindInternal(int family, int socktype, int protocol,
                           const struct sockaddr* addr, socklen_t addrlen,
                           std::string& error)
{
  int errNum;
  sock_t fd = socket(family, socktype, protocol);
  errNum = lastError();
  if (fd == (sock_t)-1) {
    error = errorMsg(errNum);
    return -1;
  }
  util::make_fd_cloexec(fd);
  int sockopt = 1;
  if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (a2_sockopt_t)&sockopt,
                 sizeof(sockopt)) < 0) {
    errNum = lastError();
    error = errorMsg(errNum);
    closeSocket(fd);
    return -1;
  }
#ifdef IPV6_V6ONLY
  if (family == AF_INET6) {
    int sockopt = 1;
    if (setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, (a2_sockopt_t)&sockopt,
                   sizeof(sockopt)) < 0) {
      errNum = lastError();
      error = errorMsg(errNum);
      closeSocket(fd);
      return -1;
    }
  }
#endif // IPV6_V6ONLY

  applySocketBufferSize(fd);

  if (::bind(fd, addr, addrlen) == -1) {
    errNum = lastError();
    error = errorMsg(errNum);
    closeSocket(fd);
    return -1;
  }
  return fd;
}

static sock_t bindTo(const char* host, uint16_t port, int family, int sockType,
                     int getaddrinfoFlags, std::string& error)
{
  struct addrinfo* res;
  int s = callGetaddrinfo(&res, host, util::uitos(port).c_str(), family,
                          sockType, getaddrinfoFlags, 0);
  if (s) {
    error = gai_strerror(s);
    return -1;
  }
  std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> resDeleter(res,
                                                                freeaddrinfo);
  struct addrinfo* rp;
  for (rp = res; rp; rp = rp->ai_next) {
    sock_t fd = bindInternal(rp->ai_family, rp->ai_socktype, rp->ai_protocol,
                             rp->ai_addr, rp->ai_addrlen, error);
    if (fd != (sock_t)-1) {
      return fd;
    }
  }
  return -1;
}

void SocketCore::bindWithFamily(uint16_t port, int family, int flags)
{
  closeConnection();
  std::string error;
  sock_t fd = bindTo(nullptr, port, family, sockType_, flags, error);
  if (fd == (sock_t)-1) {
    throw DL_ABORT_EX(fmt(EX_SOCKET_BIND, error.c_str()));
  }
  sockfd_ = fd;
}

void SocketCore::bind(const char* addr, uint16_t port, int family, int flags)
{
  closeConnection();
  std::string error;
  const char* addrp;
  if (addr && addr[0]) {
    addrp = addr;
  }
  else {
    addrp = nullptr;
  }
  if (addrp || !(flags & AI_PASSIVE) || bindAddrsList_.empty()) {
    sock_t fd = bindTo(addrp, port, family, sockType_, flags, error);
    if (fd == (sock_t)-1) {
      throw DL_ABORT_EX(fmt(EX_SOCKET_BIND, error.c_str()));
    }
    sockfd_ = fd;
    return;
  }

  std::array<char, NI_MAXHOST> host;
  for (const auto& bindAddrs : bindAddrsList_) {
    for (const auto& a : bindAddrs) {
      if (family != AF_UNSPEC && family != a.su.storage.ss_family) {
        continue;
      }
      auto s = getnameinfo(&a.su.sa, a.suLength, host.data(), NI_MAXHOST,
                           nullptr, 0, NI_NUMERICHOST);
      if (s) {
        error = gai_strerror(s);
        continue;
      }
      if (addrp && strcmp(host.data(), addrp) != 0) {
        error = "Given address and resolved address do not match.";
        continue;
      }
      auto fd = bindTo(host.data(), port, family, sockType_, flags, error);
      if (fd != (sock_t)-1) {
        sockfd_ = fd;
        return;
      }
    }
  }

  if (sockfd_ == (sock_t)-1) {
    throw DL_ABORT_EX(fmt(EX_SOCKET_BIND, error.c_str()));
  }
}

void SocketCore::bind(uint16_t port, int flags)
{
  bind(nullptr, port, protocolFamily_, flags);
}

void SocketCore::bind(const struct sockaddr* addr, socklen_t addrlen)
{
  closeConnection();
  std::string error;
  sock_t fd = bindInternal(addr->sa_family, sockType_, 0, addr, addrlen, error);
  if (fd == (sock_t)-1) {
    throw DL_ABORT_EX(fmt(EX_SOCKET_BIND, error.c_str()));
  }
  sockfd_ = fd;
}

void SocketCore::beginListen()
{
  if (listen(sockfd_, 1024) == -1) {
    int errNum = lastError();
    throw DL_ABORT_EX(fmt(EX_SOCKET_LISTEN, errorMsg(errNum).c_str()));
  }
  setNonBlockingMode();
}

std::shared_ptr<SocketCore> SocketCore::acceptConnection() const
{
  sockaddr_union sockaddr;
  socklen_t len = sizeof(sockaddr);
  sock_t fd;
  while ((fd = accept(sockfd_, &sockaddr.sa, &len)) == (sock_t)-1 &&
         lastError() == INTERRUPTED)
    ;
  int errNum = lastError();
  if (fd == (sock_t)-1) {
    throw DL_ABORT_EX(fmt(EX_SOCKET_ACCEPT, errorMsg(errNum).c_str()));
  }

  applySocketBufferSize(fd);

  auto sock = std::make_shared<SocketCore>(fd, sockType_);
  sock->setNonBlockingMode();
  return sock;
}

void SocketCore::establishConnection(const std::string& host, uint16_t port,
                                     bool tcpNodelay)
{
  closeConnection();
  std::string error;
  struct addrinfo* res;
  int s;
  s = callGetaddrinfo(&res, host.c_str(), util::uitos(port).c_str(),
                      protocolFamily_, sockType_, 0, 0);
  if (s) {
    throw DL_ABORT_EX(fmt(EX_RESOLVE_HOSTNAME, host.c_str(), gai_strerror(s)));
  }
  std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> resDeleter(res,
                                                                freeaddrinfo);
  struct addrinfo* rp;
  int errNum;
  for (rp = res; rp; rp = rp->ai_next) {
    sock_t fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    errNum = lastError();
    if (fd == (sock_t)-1) {
      error = errorMsg(errNum);
      continue;
    }
    util::make_fd_cloexec(fd);
    int sockopt = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (a2_sockopt_t)&sockopt,
                   sizeof(sockopt)) < 0) {
      errNum = lastError();
      error = errorMsg(errNum);
      closeSocket(fd);
      continue;
    }

    applySocketBufferSize(fd);

    if (!bindAddrs_.empty()) {
      bool bindSuccess = false;
      for (const auto& soaddr : bindAddrs_) {
        if (::bind(fd, &soaddr.su.sa, soaddr.suLength) == -1) {
          errNum = lastError();
          error = errorMsg(errNum);
          A2_LOG_TRACE(fmt(EX_SOCKET_BIND, error.c_str()));
        }
        else {
          bindSuccess = true;
          break;
        }
      }
      if (!bindSuccess) {
        closeSocket(fd);
        continue;
      }
    }
    if (!bindAddrsList_.empty()) {
      ++bindAddrsListIt_;
      if (bindAddrsListIt_ == bindAddrsList_.end()) {
        bindAddrsListIt_ = bindAddrsList_.begin();
      }
      bindAddrs_ = *bindAddrsListIt_;
    }

    sockfd_ = fd;
    // make socket non-blocking mode
    setNonBlockingMode();
    if (tcpNodelay) {
      setTcpNodelay(true);
    }
    if (connect(fd, rp->ai_addr, rp->ai_addrlen) == -1 &&
        lastError() != IN_PROGRESS) {
      errNum = lastError();
      error = errorMsg(errNum);
      closeSocket(sockfd_);
      sockfd_ = (sock_t)-1;
      continue;
    }
    // TODO at this point, connection may not be established and it may fail
    // later. In such case, next ai_addr should be tried.
    break;
  }
  if (sockfd_ == (sock_t)-1) {
    throw DL_ABORT_EX(fmt(EX_SOCKET_CONNECT, host.c_str(), error.c_str()));
  }
}

void SocketCore::closeConnection()
{
#ifdef ENABLE_SSL
  if (tlsSession_) {
    tlsSession_->closeConnection();
    tlsSession_.reset();
  }
#endif // ENABLE_SSL

  if (sockfd_ != (sock_t)-1) {
    shutdown(sockfd_, SHUT_WR);
    closeSocket(sockfd_);
    sockfd_ = -1;
  }
}

} // namespace aria2
