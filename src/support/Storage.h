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
#ifndef D_STORAGE_H
#define D_STORAGE_H

#include "common.h"
#include "support/Numbers.h"
#include <memory>
#include <string>
#include <vector>

namespace aria2 {
class FileEntry;
class BinaryStream;
} // namespace aria2

namespace aria2::util {

// Parses string which specifies the range of piece index for higher
// priority and appends those indexes into result.  The input string
// src can contain 2 keywords "head" and "tail".  To include both
// keywords, they must be separated by comma.  "head" means the pieces
// where the first byte of each file sits.  "tail" means the pieces
// where the last byte of each file sits.  These keywords can take one
// parameter, SIZE. For example, if "head=SIZE" is specified, pieces
// in the range of first SIZE bytes of each file get higher
// priority. SIZE can include K or M(1K = 1024, 1M = 1024K).
// If SIZE is omitted, SIZE=defaultSize is used.
//
// sample: head=512K,tail=512K
void parsePrioritizePieceRange(
    std::vector<size_t>& result, const std::string& src,
    const std::vector<std::shared_ptr<FileEntry>>& fileEntries,
    size_t pieceLength, int64_t defaultSize = 1048576 /* 1MiB */);
template <typename InputIterator, typename Output>
void toStream(InputIterator first, InputIterator last, Output& os)
{
  os.printf("%s\n"
            "idx|path/length\n"
            "===+=============================================================="
            "=============\n",
            _("Files:"));
  int32_t count = 1;
  for (; first != last; ++first, ++count) {
    os.printf("%3d|%s\n"
              "   |%sB (%s)\n"
              "---+------------------------------------------------------------"
              "---------------\n",
              count, (*first)->getPath().c_str(),
              util::abbrevSize((*first)->getLength()).c_str(),
              util::uitos((*first)->getLength(), true).c_str());
  }
}
// binaryStream has to be opened before calling this function.
std::string toString(const std::shared_ptr<BinaryStream>& binaryStream);

#ifdef HAVE_POSIX_MEMALIGN
void* allocateAlignedMemory(size_t alignment, size_t size);
#endif // HAVE_POSIX_MEMALIGN

} // namespace aria2::util

#endif // D_STORAGE_H
