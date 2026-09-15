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
#ifndef D_SOCKET_ADDRESS_H
#define D_SOCKET_ADDRESS_H
#include "common.h"
#include "a2netcompat.h"
#include <cstddef>
#include <string>

namespace aria2 {
// Set default ai_flags. hints.ai_flags is initialized with this
// value.
void setDefaultAIFlags(int flags);

// Wrapper function for getaddrinfo(). The value
// flags|DEFAULT_AI_FLAGS is used as ai_flags.  You can override
// DEFAULT_AI_FLAGS value by calling setDefaultAIFlags() with new
// flags.
int callGetaddrinfo(struct addrinfo** resPtr, const char* host,
                    const char* service, int family, int sockType, int flags,
                    int protocol);

// Provides functionality of inet_ntop using getnameinfo.  The return
// value is the exact value of getnameinfo returns. You can get error
// message using gai_strerror(3).
int inetNtop(int af, const void* src, char* dst, socklen_t size);

// Provides functionality of inet_pton using getBinAddr.  If af is
// AF_INET, dst is assumed to be the pointer to struct in_addr.  If af
// is AF_INET6, dst is assumed to be the pointer to struct in6_addr.
//
// This function returns 0 if it succeeds, or -1.
int inetPton(int af, const char* src, void* dst);

namespace net {

// Stores the binary representation of a numeric IPv4 or IPv6 address.
// Parsing is independent of locally configured network interfaces. dest must
// provide at least 4 bytes for IPv4 or 16 bytes for IPv6. Returns the number
// of bytes written, or 0 if ip is not a numeric address.
size_t getBinAddr(void* dest, const std::string& ip);

// Checks public IP address are configured for each family: IPv4 and
// IPv6. The result can be obtained using getIpv4AddrConfigured() and
// getIpv6AddrConfigured() respectively.
void checkAddrconfig();
bool getIPv4AddrConfigured();
bool getIPv6AddrConfigured();

} // namespace net

} // namespace aria2
#endif // D_SOCKET_ADDRESS_H
