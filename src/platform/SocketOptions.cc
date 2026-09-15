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
#include <cstdint>
#include "SocketCore.h"
#include "platform/SocketOps.h"
#include "platform/SocketAddress.h"
#include "DlAbortEx.h"
#include "Log.h"
#include "message.h"
#include "fmt.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <sstream>

namespace aria2 {

using namespace socket_ops;

void SocketCore::setSockOpt(int level, int optname, void* optval,
                            socklen_t optlen)
{
  if (setsockopt(sockfd_, level, optname, (a2_sockopt_t)optval, optlen) < 0) {
    int errNum = lastError();
    throw DL_ABORT_EX(fmt(EX_SOCKET_SET_OPT, errorMsg(errNum).c_str()));
  }
}

void SocketCore::setMulticastInterface(const std::string& localAddr)
{
  in_addr addr;
  if (localAddr.empty()) {
    addr.s_addr = htonl(INADDR_ANY);
  }
  else if (inetPton(AF_INET, localAddr.c_str(), &addr) != 0) {
    throw DL_ABORT_EX(
        fmt("%s is not valid IPv4 numeric address", localAddr.c_str()));
  }
  setSockOpt(IPPROTO_IP, IP_MULTICAST_IF, &addr, sizeof(addr));
}

void SocketCore::setMulticastTtl(unsigned char ttl)
{
  setSockOpt(IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
}

void SocketCore::setMulticastLoop(unsigned char loop)
{
  setSockOpt(IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop));
}

void SocketCore::joinMulticastGroup(const std::string& multicastAddr,
                                    uint16_t multicastPort,
                                    const std::string& localAddr)
{
  in_addr multiAddr;
  if (inetPton(AF_INET, multicastAddr.c_str(), &multiAddr) != 0) {
    throw DL_ABORT_EX(
        fmt("%s is not valid IPv4 numeric address", multicastAddr.c_str()));
  }
  in_addr ifAddr;
  if (localAddr.empty()) {
    ifAddr.s_addr = htonl(INADDR_ANY);
  }
  else if (inetPton(AF_INET, localAddr.c_str(), &ifAddr) != 0) {
    throw DL_ABORT_EX(
        fmt("%s is not valid IPv4 numeric address", localAddr.c_str()));
  }
  struct ip_mreq mreq;
  memset(&mreq, 0, sizeof(mreq));
  mreq.imr_multiaddr = multiAddr;
  mreq.imr_interface = ifAddr;
  setSockOpt(IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));
}

void SocketCore::setTcpNodelay(bool f)
{
  int val = f;
  setSockOpt(IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val));
}

void SocketCore::applyIpDscp()
{
  if (ipDscp_ == 0) {
    return;
  }

  try {
    int family = getAddressFamily();
    if (family == AF_INET) {
      setSockOpt(IPPROTO_IP, IP_TOS, &ipDscp_, sizeof(ipDscp_));
    }
#if defined(IPV6_TCLASS) || defined(__linux__) || defined(__FreeBSD__) ||      \
    defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DragonFly__)
    else if (family == AF_INET6) {
      setSockOpt(IPPROTO_IPV6, IPV6_TCLASS, &ipDscp_, sizeof(ipDscp_));
    }
#endif
  }
  catch (RecoverableException& e) {
    A2_LOG_DEBUG_EX("Applying DSCP value failed", e);
  }
}

void SocketCore::setNonBlockingMode()
{
#ifdef __MINGW32__
  static u_long flag = 1;
  if (::ioctlsocket(sockfd_, FIONBIO, &flag) == -1) {
    int errNum = lastError();
    throw DL_ABORT_EX(fmt(EX_SOCKET_NONBLOCKING, errorMsg(errNum).c_str()));
  }
#else
  int flags;
  while ((flags = fcntl(sockfd_, F_GETFL, 0)) == -1 && errno == EINTR)
    ;
  // TODO add error handling
  while (fcntl(sockfd_, F_SETFL, flags | O_NONBLOCK) == -1 && errno == EINTR)
    ;
#endif // __MINGW32__
  blocking_ = false;
}

void SocketCore::setBlockingMode()
{
#ifdef __MINGW32__
  static u_long flag = 0;
  if (::ioctlsocket(sockfd_, FIONBIO, &flag) == -1) {
    int errNum = lastError();
    throw DL_ABORT_EX(fmt(EX_SOCKET_BLOCKING, errorMsg(errNum).c_str()));
  }
#else
  int flags;
  while ((flags = fcntl(sockfd_, F_GETFL, 0)) == -1 && errno == EINTR)
    ;
  // TODO add error handling
  while (fcntl(sockfd_, F_SETFL, flags & (~O_NONBLOCK)) == -1 && errno == EINTR)
    ;
#endif // __MINGW32__
  blocking_ = true;
}

void SocketCore::setSocketRecvBufferSize(int size)
{
  socketRecvBufferSize_ = size;
}

int SocketCore::getSocketRecvBufferSize() { return socketRecvBufferSize_; }

} // namespace aria2
