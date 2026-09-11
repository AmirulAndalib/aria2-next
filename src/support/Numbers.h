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
#ifndef D_NUMBERS_H
#define D_NUMBERS_H

#include "common.h"
#include "SegList.h"
#include <array>
#include <charconv>
#include <limits>
#include <string>

namespace aria2::util {

// Locale-independent formatting; comma grouping is the CLI wire convention.
template <typename T> std::string uitos(T value, bool comma = false)
{
  std::array<char, std::numeric_limits<T>::digits10 + 3> buffer{};
  const auto result =
      std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
  std::string text(buffer.data(), result.ptr);
  if (comma) {
    const size_t sign = text.front() == '-' ? 1 : 0;
    for (size_t position = text.size(); position > sign + 3;) {
      position -= 3;
      text.insert(position, 1, ',');
    }
  }
  return text;
}
std::string itos(int64_t value, bool comma = false);
std::string secfmt(time_t sec);

bool parseIntNoThrow(int32_t& res, const std::string& s, int base = 10);

// Valid range: [0, INT32_MAX]
bool parseUIntNoThrow(uint32_t& res, const std::string& s, int base = 10);

bool parseLLIntNoThrow(int64_t& res, const std::string& s, int base = 10);

// Parses |s| as floating point number, and stores the result into
// |res|.  This function returns true if it succeeds.
bool parseDoubleNoThrow(double& res, const std::string& s);

SegList<int> parseIntSegments(const std::string& src);
int64_t getRealSize(const std::string& sizeWithUnit);

std::string abbrevSize(int64_t size);

} // namespace aria2::util

#endif // D_NUMBERS_H
