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
#ifndef D_NETWORK_H
#define D_NETWORK_H

#include "common.h"
#include "a2netcompat.h"
#include <string>
#ifdef ENABLE_SSL
#  include "TLSContext.h"
#endif

namespace aria2::util {

bool isNumericHost(const std::string& name);
Endpoint getNumericNameInfo(const struct sockaddr* sockaddr, socklen_t len);
// Returns true is given numeric ipv4addr is in Private Address Space.
bool inPrivateAddress(const std::string& ipv4addr);
// Returns true if ip1 and ip2 are in the same CIDR block.  ip1 and
// ip2 must be numeric IPv4 or IPv6 address. If either of them or both
// of them is not valid numeric address, then returns false. bits is
// prefix bits. If bits is out of range, then bits is set to the
// length of binary representation of the address*8.
bool inSameCidrBlock(const std::string& ip1, const std::string& ip2,
                     size_t bits);
// This is a bit different from cookie_helper::domainMatch().  If
// hostname is numeric host, then returns true if domain == hostname.
// That is if domain starts with ".", then returns true if domain is a
// suffix of hostname.  If domain does not start with ".", then
// returns true if domain == hostname.  Otherwise returns true.
// For example,
//
// * noProxyDomainMatch("aria2.sf.net", ".sf.net") returns true.
// * noProxyDomainMatch("sf.net", ".sf.net") returns false.
bool noProxyDomainMatch(const std::string& hostname, const std::string& domain);

#ifdef ENABLE_SSL
TLSVersion toTLSVersion(const std::string& ver);
#endif // ENABLE_SSL

} // namespace aria2::util

#endif // D_NETWORK_H
