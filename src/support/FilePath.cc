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
#include "support/FilePath.h"
#include "a2functional.h"
#include "error_code.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <string>
#include <utility>
#include <vector>
#include "common.h" // IWYU pragma: keep
#include "support/Text.h"
#include "support/Numbers.h"
#include "support/Encoding.h"
#include "platform/NativeText.h"
#include "platform/Process.h"
#include "File.h"
#include "BufferedFile.h"
#include "DlAbortEx.h"
#include "message.h"
#include "fmt.h"
#include <cstdlib>
#include <cerrno>
#include <istream>
#ifdef HAVE_PWD_H
#  include <pwd.h>
#endif

namespace aria2::util {

#ifndef __MINGW32__
std::string getHomeDir()
{
  const char* p = getenv("HOME");
  if (p) {
    return p;
  }
#  ifdef HAVE_PWD_H
  auto pw = getpwuid(geteuid());
  if (pw && pw->pw_dir) {
    return pw->pw_dir;
  }
#  endif // HAVE_PWD_H
  return "";
}

#else  // __MINGW32__

std::string getHomeDir()
{
  auto p = _wgetenv(L"HOME");
  if (p) {
    return toForwardSlash(wCharToUtf8(p));
  }
  p = _wgetenv(L"USERPROFILE");
  if (p) {
    return toForwardSlash(wCharToUtf8(p));
  }
  p = _wgetenv(L"HOMEDRIVE");
  if (p) {
    std::wstring homeDir = p;
    p = _wgetenv(L"HOMEPATH");
    if (p) {
      homeDir += p;
      return toForwardSlash(wCharToUtf8(homeDir));
    }
  }
  return "";
}
#endif // __MINGW32__

std::string getXDGDir(const std::string& environmentVariable,
                      const std::string& fallbackDirectory)
{
  std::string filename;
  const char* p = getenv(environmentVariable.c_str());
  if (p &&
#ifndef __MINGW32__
      p[0] == '/'
#else  // __MINGW32__
      p[0] && p[1] == ':'
#endif // __MINGW32__
  ) {
    filename = p;
  }
  else {
    filename = fallbackDirectory;
  }
  return filename;
}

std::string getConfigFile()
{
  return getXDGDir("XDG_CONFIG_HOME", getHomeDir() + "/.config") +
         "/aria2/aria2.conf";
}
void mkdirs(const std::string& dirpath)
{
  File dir(dirpath);
  if (!dir.mkdirs()) {
    int errNum = errno;
    if (!dir.isDir()) {
      throw DL_ABORT_EX3(
          errNum,
          fmt(EX_MAKE_DIR, dir.getPath().c_str(), safeStrerror(errNum).c_str()),
          error_code::DIR_CREATE_ERROR);
    }
  }
}
std::pair<size_t, std::string> parseIndexPath(const std::string& line)
{
  auto p = divide(std::begin(line), std::end(line), '=');
  uint32_t index;
  if (!parseUIntNoThrow(index, std::string(p.first.first, p.first.second))) {
    throw DL_ABORT_EX("Bad path index");
  }
  if (p.second.first == p.second.second) {
    throw DL_ABORT_EX(fmt("Path with index=%u is empty.", index));
  }
  return std::make_pair(index, std::string(p.second.first, p.second.second));
}

std::vector<std::pair<size_t, std::string>> createIndexPaths(std::istream& i)
{
  std::vector<std::pair<size_t, std::string>> indexPaths;
  std::string line;
  while (getline(i, line)) {
    indexPaths.push_back(parseIndexPath(line));
  }
  return indexPaths;
}
bool saveAs(const std::string& filename, const std::string& data,
            bool overwrite)
{
  if (!overwrite && File(filename).exists()) {
    return false;
  }
  std::string tempFilename = filename;
  tempFilename += "__temp";
  {
    BufferedFile fp(tempFilename.c_str(), BufferedFile::WRITE);
    if (!fp) {
      return false;
    }
    if (fp.write(data.data(), data.size()) != data.size()) {
      return false;
    }
    if (fp.close() == EOF) {
      return false;
    }
  }
  return File(tempFilename).renameTo(filename);
}

std::string applyDir(const std::string& dir, const std::string& relPath)
{
  std::string s;
  if (!relPath.empty() && relPath[0] == '/') {
    s = relPath;
  }
#ifdef __MINGW32__
  else if (relPath.size() >= 3 && util::isAlpha(relPath[0]) &&
           relPath[1] == ':' && (relPath[2] == '/' || relPath[2] == '\\')) {
    s = relPath;
  }
  else if (relPath.size() >= 2 &&
           ((relPath[0] == '/' && relPath[1] == '/') ||
            (relPath[0] == '\\' && relPath[1] == '\\'))) {
    s = relPath;
  }
#endif // __MINGW32__
  else if (dir.empty()) {
    s = "./";
    s += relPath;
  }
  else {
    s = dir;
    if (dir == "/") {
      s += relPath;
    }
    else {
      s += "/";
      s += relPath;
    }
  }
#ifdef __MINGW32__
  for (std::string::iterator i = s.begin(), eoi = s.end(); i != eoi; ++i) {
    if (*i == '\\') {
      *i = '/';
    }
  }
#endif // __MINGW32__
  return s;
}

std::string fixTaintedBasename(const std::string& src)
{
  return escapePath(replace(src, "/", "%2F"));
}
bool detectDirTraversal(const std::string& s)
{
  if (s.empty()) {
    return false;
  }
  for (auto c : s) {
    unsigned char ch = c;
    if (in(ch, 0x00u, 0x1fu) || ch == 0x7fu) {
      return true;
    }
  }
  return s == "." || s == ".." || s[0] == '/' || util::startsWith(s, "./") ||
         util::startsWith(s, "../") || s.find("/../") != std::string::npos ||
         s.find("/./") != std::string::npos || s[s.size() - 1] == '/' ||
         util::endsWith(s, "/.") || util::endsWith(s, "/..");
}

std::string escapePath(const std::string& s)
{
// We don't escape '/' because we use it as a path separator.
#ifdef __MINGW32__
  static const char WIN_INVALID_PATH_CHARS[] = {'"', '*', ':',  '<',
                                                '>', '?', '\\', '|'};
#endif // __MINGW32__
  std::string d;
  for (auto cc : s) {
    unsigned char c = cc;
    if (in(c, 0x00u, 0x1fu) || c == 0x7fu
#ifdef __MINGW32__
        || std::find(std::begin(WIN_INVALID_PATH_CHARS),
                     std::end(WIN_INVALID_PATH_CHARS),
                     c) != std::end(WIN_INVALID_PATH_CHARS)
#endif // __MINGW32__
    ) {
      d += fmt("%%%02X", c);
    }
    else {
      d += c;
    }
  }
  return d;
}
std::string createSafePath(const std::string& dir, const std::string& filename)
{
  return util::applyDir(dir,
                        util::isUtf8(filename)
                            ? util::fixTaintedBasename(filename)
                            : util::escapePath(util::percentEncode(filename)));
}

std::string createSafePath(const std::string& filename)
{
  return util::isUtf8(filename)
             ? util::fixTaintedBasename(filename)
             : util::escapePath(util::percentEncode(filename));
}

} // namespace aria2::util
