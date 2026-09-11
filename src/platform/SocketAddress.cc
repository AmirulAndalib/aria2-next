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
#include "platform/SocketAddress.h"
#include "a2netcompat.h"
#include <cstdint>
#include <in6addr.h>
#include <inaddr.h>
#include <ipifcons.h>
#include <iptypes.h>
#include <memory>
#include <vector>
#include <winerror.h>
#include "SocketCore.h"
#include "platform/SocketOps.h"
#include "Log.h"
#include "a2functional.h"
#include "fmt.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <sstream>

#ifdef HAVE_IFADDRS_H
#  include <ifaddrs.h>
#endif
#ifdef HAVE_IPHLPAPI_H
#  include <iphlpapi.h>
#endif

namespace aria2 {

using namespace socket_ops;

namespace net {
namespace {
bool ipv4AddrConfigured = true;
bool ipv6AddrConfigured = true;
#ifdef __MINGW32__
constexpr uint32_t APIPA_IPV4_BEGIN = 2851995649u; // 169.254.0.1
constexpr uint32_t APIPA_IPV4_END = 2852061183u;   // 169.254.255.255
#endif
} // namespace
} // namespace net
namespace {
int defaultAIFlags = DEFAULT_AI_FLAGS;
int getDefaultAIFlags() { return defaultAIFlags; }

} // namespace

void setDefaultAIFlags(int flags) { defaultAIFlags = flags; }

int callGetaddrinfo(struct addrinfo** resPtr, const char* host,
                    const char* service, int family, int sockType, int flags,
                    int protocol)
{
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = family;
  hints.ai_socktype = sockType;
  hints.ai_flags = getDefaultAIFlags();
  hints.ai_flags |= flags;
  hints.ai_protocol = protocol;
  return getaddrinfo(host, service, &hints, resPtr);
}

int inetNtop(int af, const void* src, char* dst, socklen_t size)
{
  sockaddr_union su;
  memset(&su, 0, sizeof(su));
  if (af == AF_INET) {
    su.in.sin_family = AF_INET;
#ifdef HAVE_SOCKADDR_IN_SIN_LEN
    su.in.sin_len = sizeof(su.in);
#endif // HAVE_SOCKADDR_IN_SIN_LEN
    memcpy(&su.in.sin_addr, src, sizeof(su.in.sin_addr));
    return getnameinfo(&su.sa, sizeof(su.in), dst, size, nullptr, 0,
                       NI_NUMERICHOST);
  }
  if (af == AF_INET6) {
    su.in6.sin6_family = AF_INET6;
#ifdef HAVE_SOCKADDR_IN6_SIN6_LEN
    su.in6.sin6_len = sizeof(su.in6);
#endif // HAVE_SOCKADDR_IN6_SIN6_LEN
    memcpy(&su.in6.sin6_addr, src, sizeof(su.in6.sin6_addr));
    return getnameinfo(&su.sa, sizeof(su.in6), dst, size, nullptr, 0,
                       NI_NUMERICHOST);
  }
  return EAI_FAMILY;
}

int inetPton(int af, const char* src, void* dst)
{
  union {
    uint32_t ipv4_addr;
    unsigned char ipv6_addr[16];
  } binaddr;
  size_t len = net::getBinAddr(binaddr.ipv6_addr, src);
  if (af == AF_INET) {
    if (len != 4) {
      return -1;
    }
    in_addr* addr = reinterpret_cast<in_addr*>(dst);
    addr->s_addr = binaddr.ipv4_addr;
    return 0;
  }
  if (af == AF_INET6) {
    if (len != 16) {
      return -1;
    }
    in6_addr* addr = reinterpret_cast<in6_addr*>(dst);
    memcpy(addr->s6_addr, binaddr.ipv6_addr, sizeof(addr->s6_addr));
    return 0;
  }
  return -1;
}

namespace net {
size_t getBinAddr(void* dest, const std::string& ip)
{
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_flags = AI_NUMERICHOST;

  addrinfo* res = nullptr;
  if (getaddrinfo(ip.c_str(), nullptr, &hints, &res) != 0) {
    return 0;
  }
  std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> resDeleter(res,
                                                                freeaddrinfo);
  for (addrinfo* rp = res; rp; rp = rp->ai_next) {
    if (rp->ai_family == AF_INET && rp->ai_addrlen >= sizeof(sockaddr_in)) {
      const auto* address = reinterpret_cast<const sockaddr_in*>(rp->ai_addr);
      memcpy(dest, &address->sin_addr, sizeof(address->sin_addr));
      return sizeof(address->sin_addr);
    }
    if (rp->ai_family == AF_INET6 && rp->ai_addrlen >= sizeof(sockaddr_in6)) {
      const auto* address = reinterpret_cast<const sockaddr_in6*>(rp->ai_addr);
      memcpy(dest, &address->sin6_addr, sizeof(address->sin6_addr));
      return sizeof(address->sin6_addr);
    }
  }
  return 0;
}

} // namespace net

namespace net {
void checkAddrconfig()
{
#ifdef HAVE_IPHLPAPI_H
  A2_LOG_DEBUG("Checking configured addresses");
  // Microsoft recommends an initial 15 KiB buffer. Interface changes can
  // still increase the required size between calls, so retries stay bounded.
  // https://learn.microsoft.com/windows/win32/api/iphlpapi/nf-iphlpapi-getadaptersaddresses
  ULONG bufsize = 15_k;
  ULONG retval = ERROR_BUFFER_OVERFLOW;
  std::vector<unsigned char> storage;
  IP_ADAPTER_ADDRESSES* buf = nullptr;
  for (int attempt = 0; attempt < 3; ++attempt) {
    storage.resize(bufsize);
    buf = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data());
    retval = GetAdaptersAddresses(AF_UNSPEC, 0, nullptr, buf, &bufsize);
    if (retval != ERROR_BUFFER_OVERFLOW) {
      break;
    }
  }
  if (retval != NO_ERROR) {
    A2_LOG_DEBUG("GetAdaptersAddresses failed. Assume both IPv4 and IPv6 "
                 " addresses are configured.");
    return;
  }
  ipv4AddrConfigured = false;
  ipv6AddrConfigured = false;
  char host[NI_MAXHOST];
  sockaddr_union ad;
  int rv;
  for (IP_ADAPTER_ADDRESSES* p = buf; p; p = p->Next) {
    if (p->IfType == IF_TYPE_TUNNEL) {
      // Skip tunnel interface because Windows7 automatically setup
      // this for IPv6.
      continue;
    }
    PIP_ADAPTER_UNICAST_ADDRESS ucaddr = p->FirstUnicastAddress;
    if (!ucaddr) {
      continue;
    }
    for (PIP_ADAPTER_UNICAST_ADDRESS i = ucaddr; i; i = i->Next) {
      bool found = false;
      switch (i->Address.iSockaddrLength) {
      case sizeof(sockaddr_in): {
        memcpy(&ad.storage, i->Address.lpSockaddr, i->Address.iSockaddrLength);
        uint32_t haddr = ntohl(ad.in.sin_addr.s_addr);
        if (haddr != INADDR_LOOPBACK &&
            (haddr < APIPA_IPV4_BEGIN || APIPA_IPV4_END <= haddr)) {
          ipv4AddrConfigured = true;
          found = true;
        }
        break;
      }
      case sizeof(sockaddr_in6):
        memcpy(&ad.storage, i->Address.lpSockaddr, i->Address.iSockaddrLength);
        if (!IN6_IS_ADDR_LOOPBACK(&ad.in6.sin6_addr) &&
            !IN6_IS_ADDR_LINKLOCAL(&ad.in6.sin6_addr)) {
          ipv6AddrConfigured = true;
          found = true;
        }
        break;
      }
      rv = getnameinfo(i->Address.lpSockaddr, i->Address.iSockaddrLength, host,
                       NI_MAXHOST, 0, 0, NI_NUMERICHOST);
      if (rv == 0) {
        if (found) {
          A2_LOG_TRACE(fmt("Configured address: %s", host));
        }
      }
    }
  }

  A2_LOG_DEBUG(fmt("IPv4 configured=%d, IPv6 configured=%d", ipv4AddrConfigured,
                   ipv6AddrConfigured));
#elif defined(HAVE_GETIFADDRS)
  A2_LOG_DEBUG("Checking configured addresses");
  ipv4AddrConfigured = false;
  ipv6AddrConfigured = false;
  ifaddrs* ifaddr = nullptr;
  int rv;
  rv = getifaddrs(&ifaddr);
  if (rv == -1) {
    int errNum = lastError();
    A2_LOG_DEBUG(fmt("getifaddrs failed. Cause: %s", errorMsg(errNum).c_str()));
    return;
  }
  std::unique_ptr<ifaddrs, decltype(&freeifaddrs)> ifaddrDeleter(ifaddr,
                                                                 freeifaddrs);
  char host[NI_MAXHOST];
  sockaddr_union ad;
  for (ifaddrs* ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
    if (!ifa->ifa_addr) {
      continue;
    }
    bool found = false;
    size_t addrlen = 0;
    switch (ifa->ifa_addr->sa_family) {
    case AF_INET: {
      addrlen = sizeof(sockaddr_in);
      memcpy(&ad.storage, ifa->ifa_addr, addrlen);
      if (ad.in.sin_addr.s_addr != htonl(INADDR_LOOPBACK)) {
        ipv4AddrConfigured = true;
        found = true;
      }
      break;
    }
    case AF_INET6: {
      addrlen = sizeof(sockaddr_in6);
      memcpy(&ad.storage, ifa->ifa_addr, addrlen);
      if (!IN6_IS_ADDR_LOOPBACK(&ad.in6.sin6_addr) &&
          !IN6_IS_ADDR_LINKLOCAL(&ad.in6.sin6_addr)) {
        ipv6AddrConfigured = true;
        found = true;
      }
      break;
    }
    default:
      continue;
    }
    rv = getnameinfo(ifa->ifa_addr, addrlen, host, NI_MAXHOST, nullptr, 0,
                     NI_NUMERICHOST);
    if (rv == 0) {
      if (found) {
        A2_LOG_TRACE(fmt("Configured address: %s", host));
      }
    }
  }
  A2_LOG_DEBUG(fmt("IPv4 configured=%d, IPv6 configured=%d", ipv4AddrConfigured,
                   ipv6AddrConfigured));
#else  // !HAVE_GETIFADDRS
  A2_LOG_DEBUG("getifaddrs is not available. Assume IPv4 and IPv6 addresses"
               " are configured.");
#endif // !HAVE_GETIFADDRS
}

} // namespace net

namespace net {
bool getIPv4AddrConfigured() { return ipv4AddrConfigured; }

} // namespace net

namespace net {
bool getIPv6AddrConfigured() { return ipv6AddrConfigured; }

} // namespace net

} // namespace aria2
