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
#include "support/Network.h"
#include "TLSContext.h"
#include "a2netcompat.h"
#include <cstdint>
#include <string>
#include "common.h" // IWYU pragma: keep
#include "platform/SocketAddress.h"
#include "support/Text.h"
#include "SocketCore.h"
#include "bitfield.h"
#include "DlAbortEx.h"
#include "fmt.h"
#include "prefs.h"
#include <array>
#include <cstdlib>

namespace aria2::util {

bool isNumericHost(const std::string& name)
{
  std::array<unsigned char, 16> address;
  return net::getBinAddr(address.data(), name) != 0;
}
Endpoint getNumericNameInfo(const struct sockaddr* sockaddr, socklen_t len)
{
  char host[NI_MAXHOST];
  char service[NI_MAXSERV];
  int s = getnameinfo(sockaddr, len, host, NI_MAXHOST, service, NI_MAXSERV,
                      NI_NUMERICHOST | NI_NUMERICSERV);
  if (s != 0) {
    throw DL_ABORT_EX(
        fmt("Failed to get hostname and port. cause: %s", gai_strerror(s)));
  }
  return {host, sockaddr->sa_family,
          static_cast<uint16_t>(strtoul(service, nullptr, 10))};
}
// Returns true is given numeric ipv4addr is in Private Address Space.
//
// From Section.3 RFC1918
// 10.0.0.0        -   10.255.255.255  (10/8 prefix)
// 172.16.0.0      -   172.31.255.255  (172.16/12 prefix)
// 192.168.0.0     -   192.168.255.255 (192.168/16 prefix)
bool inPrivateAddress(const std::string& ipv4addr)
{
  if (util::startsWith(ipv4addr, "10.") ||
      util::startsWith(ipv4addr, "192.168.")) {
    return true;
  }
  if (util::startsWith(ipv4addr, "172.")) {
    for (int i = 16; i <= 31; ++i) {
      std::string t(fmt("%d.", i));
      if (util::startsWith(ipv4addr.begin() + 4, ipv4addr.end(), t.begin(),
                           t.end())) {
        return true;
      }
    }
  }
  return false;
}
bool inSameCidrBlock(const std::string& ip1, const std::string& ip2,
                     size_t bits)
{
  unsigned char s1[16], s2[16];
  size_t len1, len2;
  if ((len1 = net::getBinAddr(s1, ip1)) == 0 ||
      (len2 = net::getBinAddr(s2, ip2)) == 0 || len1 != len2) {
    return false;
  }
  if (bits == 0) {
    return true;
  }
  if (bits > 8 * len1) {
    bits = 8 * len1;
  }
  int last = (bits - 1) / 8;
  for (int i = 0; i < last; ++i) {
    if (s1[i] != s2[i]) {
      return false;
    }
  }
  unsigned char mask = bitfield::lastByteMask(bits);
  return (s1[last] & mask) == (s2[last] & mask);
}
bool noProxyDomainMatch(const std::string& hostname, const std::string& domain)
{
  if (!domain.empty() && domain[0] == '.' && !util::isNumericHost(hostname)) {
    return util::endsWith(hostname, domain);
  }
  return hostname == domain;
}
#ifdef ENABLE_SSL
TLSVersion toTLSVersion(const std::string& ver)
{
  if (ver == A2_V_TLS11) {
    return TLS_PROTO_TLS11;
  }
  if (ver == A2_V_TLS12) {
    return TLS_PROTO_TLS12;
  }
  if (ver == A2_V_TLS13) {
    return TLS_PROTO_TLS13;
  }
  return TLS_PROTO_TLS12;
}
#endif // ENABLE_SSL

} // namespace aria2::util
