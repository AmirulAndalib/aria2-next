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
#ifndef D_ENCODING_H
#define D_ENCODING_H

#include "common.h"
#include "support/Text.h"
#include <string>

namespace aria2::util {

std::string percentEncode(const unsigned char* target, size_t len);

std::string percentEncode(const std::string& target);

std::string percentEncodeMini(const std::string& target);

bool inRFC3986ReservedChars(const char c);

bool inRFC3986UnreservedChars(const char c);

bool inRFC2978MIMECharset(const char c);

bool inRFC2616HttpToken(const char c);

bool inRFC5987AttrChar(const char c);

// Returns true if |c| is in ISO/IEC 8859-1 character set.
bool isIso8859p1(unsigned char c);

bool isUtf8(const std::string& str);

std::string percentDecode(std::string::const_iterator first,
                          std::string::const_iterator last);

std::string torrentPercentEncode(const unsigned char* target, size_t len);

std::string torrentPercentEncode(const std::string& target);

std::string toHex(const unsigned char* src, size_t len);

std::string toHex(const char* src, size_t len);

std::string toHex(const std::string& src);

unsigned int hexCharToUInt(unsigned char ch);

// Converts hexadecimal ascii characters in [first, last) into packed
// binary form and return the result. If characters in given range is
// not well formed, then empty string is returned.
template <typename InputIterator>
std::string fromHex(InputIterator first, InputIterator last)
{
  std::string dest;
  size_t len = last - first;
  if (len % 2) {
    return dest;
  }
  for (; first != last; first += 2) {
    unsigned char high = hexCharToUInt(*first);
    unsigned char low = hexCharToUInt(*(first + 1));
    if (high == 255 || low == 255) {
      dest.clear();
      return dest;
    }
    dest += (high * 16 + low);
  }
  return dest;
}
// Converts ISO/IEC 8859-1 string src to utf-8.
std::string iso8859p1ToUtf8(const char* src, size_t len);
std::string iso8859p1ToUtf8(const std::string& src);
std::string htmlEscape(const std::string& src);
std::string encodeNonUtf8(const std::string& s);

} // namespace aria2::util

#endif // D_ENCODING_H
