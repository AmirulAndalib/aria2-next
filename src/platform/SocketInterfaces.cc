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
#include "RecoverableException.h"
#include "a2netcompat.h"
#include <iterator>
#include <memory>
#include <vector>
#include "SocketCore.h"
#include "platform/SocketOps.h"
#include "platform/SocketAddress.h"
#include "DlAbortEx.h"
#include "Log.h"
#include "support/Text.h"
#include "support/Network.h"
#include "message.h"
#include "fmt.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <sstream>

#ifdef HAVE_IFADDRS_H
#  include <ifaddrs.h>
#endif

namespace aria2 {

using namespace socket_ops;

Endpoint SocketCore::getAddrInfo() const
{
  sockaddr_union sockaddr;
  socklen_t len = sizeof(sockaddr);
  getAddrInfo(sockaddr, len);
  return util::getNumericNameInfo(&sockaddr.sa, len);
}

void SocketCore::getAddrInfo(sockaddr_union& sockaddr, socklen_t& len) const
{
  if (getsockname(sockfd_, &sockaddr.sa, &len) == -1) {
    int errNum = lastError();
    throw DL_ABORT_EX(fmt(EX_SOCKET_GET_NAME, errorMsg(errNum).c_str()));
  }
}

int SocketCore::getAddressFamily() const
{
  sockaddr_union sockaddr;
  socklen_t len = sizeof(sockaddr);
  getAddrInfo(sockaddr, len);
  return sockaddr.storage.ss_family;
}

Endpoint SocketCore::getPeerInfo() const
{
  sockaddr_union sockaddr;
  socklen_t len = sizeof(sockaddr);
  if (getpeername(sockfd_, &sockaddr.sa, &len) == -1) {
    int errNum = lastError();
    throw DL_ABORT_EX(fmt(EX_SOCKET_GET_NAME, errorMsg(errNum).c_str()));
  }
  return util::getNumericNameInfo(&sockaddr.sa, len);
}

std::string SocketCore::getSocketError() const
{
  int error;
  socklen_t optlen = sizeof(error);

  if (getsockopt(sockfd_, SOL_SOCKET, SO_ERROR, (a2_sockopt_t)&error,
                 &optlen) == -1) {
    int errNum = lastError();
    throw DL_ABORT_EX(
        fmt("Failed to get socket error: %s", errorMsg(errNum).c_str()));
  }
  if (error != 0) {
    return errorMsg(error);
  }
  return "";
}

void SocketCore::bindAddress(const std::string& iface)
{
  auto bindAddrs = getInterfaceAddress(iface, protocolFamily_);
  if (bindAddrs.empty()) {
    throw DL_ABORT_EX(
        fmt(MSG_INTERFACE_NOT_FOUND, iface.c_str(), "not available"));
  }
  bindAddrs_.swap(bindAddrs);
  for (const auto& a : bindAddrs_) {
    char host[NI_MAXHOST];
    int s;
    s = getnameinfo(&a.su.sa, a.suLength, host, NI_MAXHOST, nullptr, 0,
                    NI_NUMERICHOST);
    if (s == 0) {
      A2_LOG_TRACE(fmt("Sockets will bind to %s", host));
    }
  }
  bindAddrsList_.push_back(bindAddrs_);
  bindAddrsListIt_ = std::begin(bindAddrsList_);
}

void SocketCore::bindAllAddress(const std::string& ifaces)
{
  std::vector<std::vector<SockAddr>> bindAddrsList;
  std::vector<std::string> ifaceList;
  util::split(ifaces.begin(), ifaces.end(), std::back_inserter(ifaceList), ',',
              true);
  if (ifaceList.empty()) {
    throw DL_ABORT_EX(
        "List of interfaces is empty, one or more interfaces is required");
  }
  for (auto& iface : ifaceList) {
    auto bindAddrs = getInterfaceAddress(iface, protocolFamily_);
    if (bindAddrs.empty()) {
      throw DL_ABORT_EX(
          fmt(MSG_INTERFACE_NOT_FOUND, iface.c_str(), "not available"));
    }
    bindAddrsList.push_back(bindAddrs);
    for (const auto& a : bindAddrs) {
      char host[NI_MAXHOST];
      int s;
      s = getnameinfo(&a.su.sa, a.suLength, host, NI_MAXHOST, nullptr, 0,
                      NI_NUMERICHOST);
      if (s == 0) {
        A2_LOG_TRACE(fmt("Sockets will bind to %s", host));
      }
    }
  }
  bindAddrsList_.swap(bindAddrsList);
  bindAddrsListIt_ = bindAddrsList_.begin();
  bindAddrs_ = *bindAddrsListIt_;
}

std::vector<SockAddr> SocketCore::getInterfaceAddress(const std::string& iface,
                                                      int family, int aiFlags)
{
  A2_LOG_TRACE(fmt("Finding interface %s", iface.c_str()));
  std::vector<SockAddr> ifAddrs;
#ifdef HAVE_GETIFADDRS
  // First find interface in interface addresses
  struct ifaddrs* ifaddr = nullptr;
  if (getifaddrs(&ifaddr) == -1) {
    int errNum = lastError();
    A2_LOG_DEBUG(
        fmt(MSG_INTERFACE_NOT_FOUND, iface.c_str(), errorMsg(errNum).c_str()));
  }
  else {
    std::unique_ptr<ifaddrs, decltype(&freeifaddrs)> ifaddrDeleter(ifaddr,
                                                                   freeifaddrs);
    for (ifaddrs* ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
      if (!ifa->ifa_addr) {
        continue;
      }
      int iffamily = ifa->ifa_addr->sa_family;
      if (family == AF_UNSPEC) {
        if (iffamily != AF_INET && iffamily != AF_INET6) {
          continue;
        }
      }
      else if (family == AF_INET) {
        if (iffamily != AF_INET) {
          continue;
        }
      }
      else if (family == AF_INET6) {
        if (iffamily != AF_INET6) {
          continue;
        }
      }
      else {
        continue;
      }
      if (strcmp(iface.c_str(), ifa->ifa_name) == 0) {
        SockAddr soaddr;
        soaddr.suLength =
            iffamily == AF_INET ? sizeof(sockaddr_in) : sizeof(sockaddr_in6);
        memcpy(&soaddr.su, ifa->ifa_addr, soaddr.suLength);
        ifAddrs.push_back(soaddr);
      }
    }
  }
#endif // HAVE_GETIFADDRS
  if (ifAddrs.empty()) {
    addrinfo* res;
    int s;
    s = callGetaddrinfo(&res, iface.c_str(), nullptr, family, SOCK_STREAM,
                        aiFlags, 0);
    if (s) {
      A2_LOG_DEBUG(
          fmt(MSG_INTERFACE_NOT_FOUND, iface.c_str(), gai_strerror(s)));
    }
    else {
      std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> resDeleter(
          res, freeaddrinfo);
      addrinfo* rp;
      for (rp = res; rp; rp = rp->ai_next) {
        // Try to bind socket with this address. If it fails, the
        // address is not for this machine.
        try {
          SocketCore socket;
          socket.bind(rp->ai_addr, rp->ai_addrlen);
          SockAddr soaddr;
          memcpy(&soaddr.su, rp->ai_addr, rp->ai_addrlen);
          soaddr.suLength = rp->ai_addrlen;
          ifAddrs.push_back(soaddr);
        }
        catch (RecoverableException& e) {
          continue;
        }
      }
    }
  }

  return ifAddrs;
}

} // namespace aria2
