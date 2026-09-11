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
#include "support/Numbers.h"
#include "SegList.h"
#include <algorithm>
#include <cinttypes>
#include <cstdint>
#include <ctime>
#include <iterator>
#include <limits>
#include <string>
#include "common.h" // IWYU pragma: keep
#include "support/Text.h"
#include "a2functional.h"
#include "DlAbortEx.h"
#include "fmt.h"
#include "message.h"
#include <cstdlib>
#include <cerrno>
#include <cctype>

namespace aria2::util {

std::string itos(int64_t value, bool comma) { return uitos(value, comma); }

namespace {
int64_t parseDecimalSize(const std::string& value, int64_t mult,
                         const std::string& original)
{
  if (value.empty()) {
    throw DL_ABORT_EX(
        fmt("Bad or negative value detected: %s", original.c_str()));
  }

  int64_t whole = 0;
  std::string fraction;
  bool seenDigit = false;
  bool seenDot = false;
  bool seenFractionDigit = false;

  for (char ch : value) {
    if (ch == '.') {
      if (seenDot) {
        throw DL_ABORT_EX(
            fmt("Bad or negative value detected: %s", original.c_str()));
      }
      seenDot = true;
      continue;
    }

    if (!isDigit(ch)) {
      throw DL_ABORT_EX(
          fmt("Bad or negative value detected: %s", original.c_str()));
    }

    seenDigit = true;
    int digit = ch - '0';
    if (!seenDot) {
      if (whole > (INT64_MAX - digit) / 10) {
        throw DL_ABORT_EX(
            fmt(MSG_STRING_INTEGER_CONVERSION_FAILURE, "overflow/underflow"));
      }
      whole = whole * 10 + digit;
    }
    else {
      fraction.push_back(ch);
      seenFractionDigit = true;
    }
  }

  if (!seenDigit || (seenDot && !seenFractionDigit)) {
    throw DL_ABORT_EX(
        fmt("Bad or negative value detected: %s", original.c_str()));
  }

  if (whole > INT64_MAX / mult) {
    throw DL_ABORT_EX(
        fmt(MSG_STRING_INTEGER_CONVERSION_FAILURE, "overflow/underflow"));
  }

  int64_t result = whole * mult;
  if (!fraction.empty()) {
    int64_t fractionResult = 0;
    for (auto i = fraction.rbegin(); i != fraction.rend(); ++i) {
      int64_t product = (*i - '0') * mult + fractionResult;
      fractionResult = product / 10;
    }
    if (INT64_MAX - result < fractionResult) {
      throw DL_ABORT_EX(
          fmt(MSG_STRING_INTEGER_CONVERSION_FAILURE, "overflow/underflow"));
    }
    result += fractionResult;
  }

  return result;
}
} // namespace

std::string secfmt(time_t sec)
{
  time_t tsec = sec;
  std::string str;
  if (sec >= 3600) {
    str = fmt("%" PRId64 "h", static_cast<int64_t>(sec / 3600));
    sec %= 3600;
  }
  if (sec >= 60) {
    str += fmt("%dm", static_cast<int>(sec / 60));
    sec %= 60;
  }
  if (sec || tsec == 0) {
    str += fmt("%ds", static_cast<int>(sec));
  }
  return str;
}

namespace {
template <typename T, typename F>
bool parseLong(T& res, F f, const std::string& s, int base)
{
  if (s.empty()) {
    return false;
  }
  char* endptr;
  errno = 0;
  res = f(s.c_str(), &endptr, base);
  if (errno == ERANGE) {
    return false;
  }
  if (*endptr != '\0') {
    for (const char *i = endptr, *eoi = s.c_str() + s.size(); i < eoi; ++i) {
      if (!isspace(*i)) {
        return false;
      }
    }
  }
  return true;
}
} // namespace

bool parseIntNoThrow(int32_t& res, const std::string& s, int base)
{
  long int t;
  if (parseLong(t, strtol, s, base) &&
      t >= std::numeric_limits<int32_t>::min() &&
      t <= std::numeric_limits<int32_t>::max()) {
    res = t;
    return true;
  }
  else {
    return false;
  }
}

bool parseUIntNoThrow(uint32_t& res, const std::string& s, int base)
{
  long int t;
  if (parseLong(t, strtol, s, base) && t >= 0 &&
      t <= std::numeric_limits<int32_t>::max()) {
    res = t;
    return true;
  }
  else {
    return false;
  }
}

bool parseLLIntNoThrow(int64_t& res, const std::string& s, int base)
{
  int64_t t;
  if (parseLong(t, strtoll, s, base)) {
    res = t;
    return true;
  }
  else {
    return false;
  }
}

bool parseDoubleNoThrow(double& res, const std::string& s)
{
  if (s.empty()) {
    return false;
  }

  errno = 0;
  char* endptr;
  auto d = strtod(s.c_str(), &endptr);

  if (errno == ERANGE) {
    return false;
  }

  if (endptr != s.c_str() + s.size()) {
    for (auto i = std::begin(s) + (endptr - s.c_str()); i != std::end(s); ++i) {
      if (!isspace(*i)) {
        return false;
      }
    }
  }

  res = d;

  return true;
}

SegList<int> parseIntSegments(const std::string& src)
{
  SegList<int> sgl;
  for (std::string::const_iterator i = src.begin(), eoi = src.end();
       i != eoi;) {
    std::string::const_iterator j = std::find(i, eoi, ',');
    if (j == i) {
      ++i;
      continue;
    }
    std::string::const_iterator p = std::find(i, j, '-');
    if (p == j) {
      int a;
      if (parseIntNoThrow(a, std::string(i, j))) {
        sgl.add(a, a + 1);
      }
      else {
        throw DL_ABORT_EX(fmt("Bad range %s", std::string(i, j).c_str()));
      }
    }
    else if (p == i || p + 1 == j) {
      throw DL_ABORT_EX(fmt(MSG_INCOMPLETE_RANGE, std::string(i, j).c_str()));
    }
    else {
      int a, b;
      if (parseIntNoThrow(a, std::string(i, p)) &&
          parseIntNoThrow(b, (std::string(p + 1, j)))) {
        sgl.add(a, b + 1);
      }
      else {
        throw DL_ABORT_EX(fmt("Bad range %s", std::string(i, j).c_str()));
      }
    }
    if (j == eoi) {
      break;
    }
    i = j + 1;
  }
  return sgl;
}
int64_t getRealSize(const std::string& sizeWithUnit)
{
  std::string::size_type p = sizeWithUnit.find_first_of("KMkm");
  std::string size;
  int64_t mult = 1;
  if (p == std::string::npos) {
    size = sizeWithUnit;
  }
  else {
    if (p + 1 != sizeWithUnit.size()) {
      throw DL_ABORT_EX(
          fmt("Bad or negative value detected: %s", sizeWithUnit.c_str()));
    }
    switch (sizeWithUnit[p]) {
    case 'K':
    case 'k':
      mult = 1_k;
      break;
    case 'M':
    case 'm':
      mult = 1_m;
      break;
    }
    size.assign(sizeWithUnit.begin(), sizeWithUnit.begin() + p);
  }
  return parseDecimalSize(size, mult, sizeWithUnit);
}

std::string abbrevSize(int64_t size)
{
  static const char* UNITS[] = {"", "Ki", "Mi", "Gi"};
  int64_t t = size;
  size_t uidx = 0;
  int r = 0;
  while (t >= static_cast<int64_t>(1_k) &&
         uidx + 1 < sizeof(UNITS) / sizeof(UNITS[0])) {
    lldiv_t d = lldiv(t, 1_k);
    t = d.quot;
    r = d.rem;
    ++uidx;
  }
  if (uidx + 1 < sizeof(UNITS) / sizeof(UNITS[0]) && t >= 922) {
    ++uidx;
    r = t;
    t = 0;
  }
  std::string res;
  res += itos(t, true);
  if (t < 10 && uidx > 0) {
    res += ".";
    res += itos(r * 10 / 1_k);
  }
  res += UNITS[uidx];
  return res;
}

} // namespace aria2::util
