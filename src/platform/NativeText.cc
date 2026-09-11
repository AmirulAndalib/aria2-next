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
#include "platform/NativeText.h"
#include <iterator>
#include <stdlib.h>
#include <string>
#include <stringapiset.h>
#include <winnls.h>
#include "common.h" // IWYU pragma: keep
#include "a2functional.h"
#include <cstdlib>
#include <algorithm>

namespace aria2 {

#ifdef __MINGW32__
namespace {
int utf8ToWChar(wchar_t* out, size_t outLength, const char* src)
{
  return MultiByteToWideChar(CP_UTF8, 0, src, -1, out, outLength);
}
} // namespace

namespace {
int wCharToUtf8(char* out, size_t outLength, const wchar_t* src)
{
  return WideCharToMultiByte(CP_UTF8, 0, src, -1, out, outLength, nullptr,
                             nullptr);
}
} // namespace

std::wstring utf8ToWChar(const char* src)
{
  int len = utf8ToWChar(nullptr, 0, src);
  if (len <= 0) {
    abort();
  }
  auto buf = make_unique<wchar_t[]>((size_t)len);
  len = utf8ToWChar(buf.get(), len, src);
  if (len <= 0) {
    abort();
  }
  else {
    return buf.get();
  }
}

std::wstring utf8ToWChar(const std::string& src)
{
  return utf8ToWChar(src.c_str());
}

std::string wCharToUtf8(const std::wstring& wsrc)
{
  int len = wCharToUtf8(nullptr, 0, wsrc.c_str());
  if (len <= 0) {
    abort();
  }
  auto buf = make_unique<char[]>((size_t)len);
  len = wCharToUtf8(buf.get(), len, wsrc.c_str());
  if (len <= 0) {
    abort();
  }
  else {
    return buf.get();
  }
}

std::string toForwardSlash(const std::string& src)
{
  auto dst = src;
  std::transform(std::begin(dst), std::end(dst), std::begin(dst),
                 [](char c) { return c == '\\' ? '/' : c; });
  return dst;
}

#endif // __MINGW32__

} // namespace aria2
