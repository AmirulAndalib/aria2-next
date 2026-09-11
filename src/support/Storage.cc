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
#include "support/Storage.h"
#include <algorithm>
#include <cstdint>
#include <iterator>
#include <memory>
#include <string>
#include <vector>
#include "common.h" // IWYU pragma: keep
#include "support/Text.h"
#include "support/Numbers.h"
#include "platform/Process.h"
#include "BinaryStream.h"
#include "FileEntry.h"
#include "FatalException.h"
#include "DlAbortEx.h"
#include "fmt.h"
#include "a2iterator.h"
#include "message.h"
#include "a2functional.h"
#include <sstream>
#include <cstdlib>

namespace aria2::util {

namespace {
void computeHeadPieces(
    std::vector<size_t>& indexes,
    const std::vector<std::shared_ptr<FileEntry>>& fileEntries,
    size_t pieceLength, int64_t head)
{
  if (head == 0) {
    return;
  }
  for (const auto& fi : fileEntries) {
    if (fi->getLength() == 0) {
      continue;
    }
    const size_t lastIndex =
        (fi->getOffset() + std::min(head, fi->getLength()) - 1) / pieceLength;
    for (size_t idx = fi->getOffset() / pieceLength; idx <= lastIndex; ++idx) {
      indexes.push_back(idx);
    }
  }
}
} // namespace

namespace {
void computeTailPieces(
    std::vector<size_t>& indexes,
    const std::vector<std::shared_ptr<FileEntry>>& fileEntries,
    size_t pieceLength, int64_t tail)
{
  if (tail == 0) {
    return;
  }
  for (const auto& fi : fileEntries) {
    if (fi->getLength() == 0) {
      continue;
    }
    int64_t endOffset = fi->getLastOffset();
    size_t fromIndex =
        (endOffset - 1 - (std::min(tail, fi->getLength()) - 1)) / pieceLength;
    const size_t toIndex = (endOffset - 1) / pieceLength;
    while (fromIndex <= toIndex) {
      indexes.push_back(fromIndex++);
    }
  }
}
} // namespace

void parsePrioritizePieceRange(
    std::vector<size_t>& result, const std::string& src,
    const std::vector<std::shared_ptr<FileEntry>>& fileEntries,
    size_t pieceLength, int64_t defaultSize)
{
  std::vector<size_t> indexes;
  std::vector<Scip> parts;
  splitIter(src.begin(), src.end(), std::back_inserter(parts), ',', true);
  for (const auto& i : parts) {
    if (util::streq(i.first, i.second, "head")) {
      computeHeadPieces(indexes, fileEntries, pieceLength, defaultSize);
    }
    else if (util::startsWith(i.first, i.second, "head=")) {
      std::string sizestr(i.first + 5, i.second);
      computeHeadPieces(indexes, fileEntries, pieceLength,
                        std::max((int64_t)0, getRealSize(sizestr)));
    }
    else if (util::streq(i.first, i.second, "tail")) {
      computeTailPieces(indexes, fileEntries, pieceLength, defaultSize);
    }
    else if (util::startsWith(i.first, i.second, "tail=")) {
      std::string sizestr(i.first + 5, i.second);
      computeTailPieces(indexes, fileEntries, pieceLength,
                        std::max((int64_t)0, getRealSize(sizestr)));
    }
    else {
      throw DL_ABORT_EX(
          fmt("Unrecognized token %s", std::string(i.first, i.second).c_str()));
    }
  }
  std::sort(indexes.begin(), indexes.end());
  indexes.erase(std::unique(indexes.begin(), indexes.end()), indexes.end());
  result.insert(result.end(), indexes.begin(), indexes.end());
}
std::string toString(const std::shared_ptr<BinaryStream>& binaryStream)
{
  std::stringstream strm;
  char data[2048];
  while (1) {
    int32_t dataLength = binaryStream->readData(
        reinterpret_cast<unsigned char*>(data), sizeof(data), strm.tellp());
    strm.write(data, dataLength);
    if (dataLength == 0) {
      break;
    }
  }
  return strm.str();
}

#ifdef HAVE_POSIX_MEMALIGN
/**
 * In linux 2.6, alignment and size should be a multiple of 512.
 */
void* allocateAlignedMemory(size_t alignment, size_t size)
{
  void* buffer;
  int res;
  if ((res = posix_memalign(&buffer, alignment, size)) != 0) {
    throw FATAL_EXCEPTION(
        fmt("Error in posix_memalign: %s", util::safeStrerror(res).c_str()));
  }
  return buffer;
}
#endif // HAVE_POSIX_MEMALIGN

} // namespace aria2::util
