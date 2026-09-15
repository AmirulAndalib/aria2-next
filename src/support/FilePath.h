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
#ifndef D_FILE_PATH_H
#define D_FILE_PATH_H

#include "common.h"
#include "a2functional.h"
#include <iosfwd>
#include <string>
#include <utility>
#include <vector>

namespace aria2::util {

std::string getHomeDir();

std::string getXDGDir(const std::string& environmentVariable,
                      const std::string& fallbackDirectory);

std::string getConfigFile();
void mkdirs(const std::string& dirpath);
// Joins path element specified in [first, last).  If ".." is found,
// it eats the previous element if it exists.  If "." is found, it
// is just ignored and it is not appeared in the result.
template <typename InputIterator>
std::string joinPath(InputIterator first, InputIterator last)
{
  std::vector<std::string> elements;
  for (; first != last; ++first) {
    if (*first == "..") {
      if (!elements.empty()) {
        elements.pop_back();
      }
    }
    else if (*first == ".") {
      // do nothing
    }
    else {
      elements.push_back(*first);
    }
  }
  return strjoin(elements.begin(), elements.end(), "/");
}

// Parses INDEX=PATH format string. INDEX must be an unsigned
// integer.
std::pair<size_t, std::string> parseIndexPath(const std::string& line);

std::vector<std::pair<size_t, std::string>> createIndexPaths(std::istream& i);
// Writes data unless the destination exists and overwrite is false.
// Returns true only when the write succeeds.
bool saveAs(const std::string& filename, const std::string& data,
            bool overwrite = false);

// Prepend dir to relPath. If dir is empty, it prepends "." to relPath.
//
// dir = "/dir", relPath = "foo" => "/dir/foo"
// dir = "",     relPath = "foo" => "./foo"
// dir = "/",    relPath = "foo" => "/foo"
std::string applyDir(const std::string& dir, const std::string& relPath);

// Accepts a decoded basename. Escapes '/' as %2F before applying escapePath(),
// so an encoded URI separator cannot introduce a directory component.
std::string fixTaintedBasename(const std::string& src);
// Returns true if s contains directory traversal path component such
// as '..' or it contains null or control character which may fool
// user.
bool detectDirTraversal(const std::string& s);

// Percent-encodes control bytes (0x00-0x1f and 0x7f), plus Windows-invalid
// characters on MinGW. Preserves '/' for use as a path separator.
std::string escapePath(const std::string& s);
std::string createSafePath(const std::string& dir, const std::string& filename);

std::string createSafePath(const std::string& filename);

} // namespace aria2::util

#endif // D_FILE_PATH_H
