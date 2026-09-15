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
#include "support/Encoding.h"
#include "support/Text.h"
#include <cstddef>
#include <iterator>
#include <string>
#include "common.h" // IWYU pragma: keep
#include "a2functional.h"
#include "fmt.h"
#include <algorithm>

namespace aria2::util {

bool inRFC3986ReservedChars(const char c)
{
  static const char reserved[] = {':', '/',  '?', '#', '[', ']', '@', '!', '$',
                                  '&', '\'', '(', ')', '*', '+', ',', ';', '='};
  return std::find(std::begin(reserved), std::end(reserved), c) !=
         std::end(reserved);
}

bool inRFC3986UnreservedChars(const char c)
{
  static const char unreserved[] = {'-', '.', '_', '~'};
  return isAlpha(c) || isDigit(c) ||
         std::find(std::begin(unreserved), std::end(unreserved), c) !=
             std::end(unreserved);
}

bool inRFC2978MIMECharset(const char c)
{
  static const char chars[] = {'!', '#', '$', '%', '&', '\'', '+',
                               '-', '^', '_', '`', '{', '}',  '~'};
  return isAlpha(c) || isDigit(c) ||
         std::find(std::begin(chars), std::end(chars), c) != std::end(chars);
}

bool inRFC2616HttpToken(const char c)
{
  static const char chars[] = {'!', '#', '$', '%', '&', '\'', '*', '+',
                               '-', '.', '^', '_', '`', '|',  '~'};
  return isAlpha(c) || isDigit(c) ||
         std::find(std::begin(chars), std::end(chars), c) != std::end(chars);
}

bool inRFC5987AttrChar(const char c)
{
  return inRFC2616HttpToken(c) && c != '*' && c != '\'' && c != '%';
}

// Returns nonzero if |c| is in ISO/IEC 8859-1 character set.
bool isIso8859p1(unsigned char c)
{
  return (0x20u <= c && c <= 0x7eu) || 0xa0u <= c;
}

namespace {
bool isUtf8Tail(unsigned char ch) { return in(ch, 0x80u, 0xbfu); }

bool inPercentEncodeMini(const unsigned char c)
{
  return c > 0x20 && c < 0x7fu &&
         // Chromium escapes following characters. Firefox4 escapes more.
         c != '"' && c != '<' && c != '>';
}
} // namespace

bool isUtf8(const std::string& str)
{
  for (std::string::const_iterator s = str.begin(), eos = str.end(); s != eos;
       ++s) {
    unsigned char firstChar = *s;
    // See ABNF in http://tools.ietf.org/search/rfc3629#section-4
    if (in(firstChar, 0x20u, 0x7eu) || firstChar == 0x08u || // \b
        firstChar == 0x09u ||                                // \t
        firstChar == 0x0au ||                                // \n
        firstChar == 0x0cu ||                                // \f
        firstChar == 0x0du                                   // \r
    ) {
      // UTF8-1 (without ctrl chars)
    }
    else if (in(firstChar, 0xc2u, 0xdfu)) {
      // UTF8-2
      if (++s == eos || !isUtf8Tail(*s)) {
        return false;
      }
    }
    else if (0xe0u == firstChar) {
      // UTF8-3
      if (++s == eos || !in(static_cast<unsigned char>(*s), 0xa0u, 0xbfu) ||
          ++s == eos || !isUtf8Tail(*s)) {
        return false;
      }
    }
    else if (in(firstChar, 0xe1u, 0xecu) || in(firstChar, 0xeeu, 0xefu)) {
      // UTF8-3
      if (++s == eos || !isUtf8Tail(*s) || ++s == eos || !isUtf8Tail(*s)) {
        return false;
      }
    }
    else if (0xedu == firstChar) {
      // UTF8-3
      if (++s == eos || !in(static_cast<unsigned char>(*s), 0x80u, 0x9fu) ||
          ++s == eos || !isUtf8Tail(*s)) {
        return false;
      }
    }
    else if (0xf0u == firstChar) {
      // UTF8-4
      if (++s == eos || !in(static_cast<unsigned char>(*s), 0x90u, 0xbfu) ||
          ++s == eos || !isUtf8Tail(*s) || ++s == eos || !isUtf8Tail(*s)) {
        return false;
      }
    }
    else if (in(firstChar, 0xf1u, 0xf3u)) {
      // UTF8-4
      if (++s == eos || !isUtf8Tail(*s) || ++s == eos || !isUtf8Tail(*s) ||
          ++s == eos || !isUtf8Tail(*s)) {
        return false;
      }
    }
    else if (0xf4u == firstChar) {
      // UTF8-4
      if (++s == eos || !in(static_cast<unsigned char>(*s), 0x80u, 0x8fu) ||
          ++s == eos || !isUtf8Tail(*s) || ++s == eos || !isUtf8Tail(*s)) {
        return false;
      }
    }
    else {
      return false;
    }
  }
  return true;
}

std::string percentEncode(const unsigned char* target, size_t len)
{
  std::string dest;
  for (size_t i = 0; i < len; ++i) {
    if (inRFC3986UnreservedChars(target[i])) {
      dest += target[i];
    }
    else {
      dest.append(fmt("%%%02X", target[i]));
    }
  }
  return dest;
}

std::string percentEncode(const std::string& target)
{
  if (std::find_if_not(target.begin(), target.end(),
                       inRFC3986UnreservedChars) == target.end()) {
    return target;
  }
  return percentEncode(reinterpret_cast<const unsigned char*>(target.c_str()),
                       target.size());
}

std::string percentEncodeMini(const std::string& src)
{
  if (std::find_if_not(src.begin(), src.end(), inPercentEncodeMini) ==
      src.end()) {
    return src;
  }
  std::string result;
  for (auto c : src) {
    if (!inPercentEncodeMini(c)) {
      result += fmt("%%%02X", static_cast<unsigned char>(c));
    }
    else {
      result += c;
    }
  }
  return result;
}

std::string torrentPercentEncode(const unsigned char* target, size_t len)
{
  std::string dest;
  for (size_t i = 0; i < len; ++i) {
    if (isAlpha(target[i]) || isDigit(target[i])) {
      dest += target[i];
    }
    else {
      dest.append(fmt("%%%02X", target[i]));
    }
  }
  return dest;
}

std::string torrentPercentEncode(const std::string& target)
{
  return torrentPercentEncode(
      reinterpret_cast<const unsigned char*>(target.c_str()), target.size());
}

std::string percentDecode(std::string::const_iterator first,
                          std::string::const_iterator last)
{
  std::string result;
  for (; first != last; ++first) {
    if (*first == '%') {
      if (first + 1 != last && first + 2 != last && isHexDigit(*(first + 1)) &&
          isHexDigit(*(first + 2))) {
        result +=
            hexCharToUInt(*(first + 1)) * 16 + hexCharToUInt(*(first + 2));
        first += 2;
      }
      else {
        result += *first;
      }
    }
    else {
      result += *first;
    }
  }
  return result;
}

std::string toHex(const unsigned char* src, size_t len)
{
  std::string out(len * 2, '\0');
  std::string::iterator o = out.begin();
  const unsigned char* last = src + len;
  for (const unsigned char* i = src; i != last; ++i) {
    *o = (*i >> 4);
    *(o + 1) = (*i) & 0x0fu;
    for (int j = 0; j < 2; ++j) {
      if (*o < 10) {
        *o += '0';
      }
      else {
        *o += 'a' - 10;
      }
      ++o;
    }
  }
  return out;
}

std::string toHex(const char* src, size_t len)
{
  return toHex(reinterpret_cast<const unsigned char*>(src), len);
}

std::string toHex(const std::string& src)
{
  return toHex(reinterpret_cast<const unsigned char*>(src.c_str()), src.size());
}

unsigned int hexCharToUInt(unsigned char ch)
{
  if ('a' <= ch && ch <= 'f') {
    ch -= 'a';
    ch += 10;
  }
  else if ('A' <= ch && ch <= 'F') {
    ch -= 'A';
    ch += 10;
  }
  else if ('0' <= ch && ch <= '9') {
    ch -= '0';
  }
  else {
    ch = 255;
  }
  return ch;
}
// Converts ISO/IEC 8859-1 string to UTF-8 string.  If there is a
// character not in ISO/IEC 8859-1, returns empty string.
std::string iso8859p1ToUtf8(const char* src, size_t len)
{
  std::string dest;
  for (const char *p = src, *last = src + len; p != last; ++p) {
    unsigned char c = *p;
    if (0xa0u <= c) {
      if (c <= 0xbfu) {
        dest += 0xc2u;
      }
      else {
        dest += 0xc3u;
      }
      dest += c & (~0x40u);
    }
    else if (0x80u <= c && c <= 0x9fu) {
      return "";
    }
    else {
      dest += c;
    }
  }
  return dest;
}

std::string iso8859p1ToUtf8(const std::string& src)
{
  return iso8859p1ToUtf8(src.c_str(), src.size());
}
std::string htmlEscape(const std::string& src)
{
  std::string dest;
  dest.reserve(src.size());
  auto j = std::begin(src);
  for (auto i = std::begin(src); i != std::end(src); ++i) {
    char ch = *i;
    const char* repl;
    if (ch == '<') {
      repl = "&lt;";
    }
    else if (ch == '>') {
      repl = "&gt;";
    }
    else if (ch == '&') {
      repl = "&amp;";
    }
    else if (ch == '\'') {
      repl = "&#39;";
    }
    else if (ch == '"') {
      repl = "&quot;";
    }
    else {
      continue;
    }
    dest.append(j, i);
    j = i + 1;
    dest += repl;
  }
  dest.append(j, std::end(src));
  return dest;
}
std::string encodeNonUtf8(const std::string& s)
{
  return util::isUtf8(s) ? s : util::percentEncode(s);
}

} // namespace aria2::util
