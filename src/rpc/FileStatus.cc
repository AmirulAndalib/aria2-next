/* <!-- copyright */
/*
 * aria2 - The high speed download utility
 *
 * Copyright (C) 2009 Tatsuhiro Tsujikawa
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
#include "ValueBase.h"
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "common.h" // IWYU pragma: keep

#include "RpcStatus.h"
#include "RpcFields.h"
#include "FileEntry.h"
#include "support/Numbers.h"
#include "a2functional.h"
#ifdef ENABLE_BITTORRENT
#  include "BtDownload.h"
#  include "BtSession.h"
#  include "BtMetadata.h"
#endif

namespace aria2::rpc::detail {

using namespace fields;

template <typename InputIterator>
void createUriEntry(List* uriList, InputIterator first, InputIterator last,
                    const std::string& status)
{
  for (; first != last; ++first) {
    auto entry = Dict::g();
    entry->put(KEY_URI, *first);
    entry->put(KEY_STATUS, status);
    uriList->append(std::move(entry));
  }
}

void createUriEntry(List* uriList, const std::shared_ptr<FileEntry>& file)
{
  createUriEntry(uriList, std::begin(file->getSpentUris()),
                 std::end(file->getSpentUris()), VLB_USED);
  createUriEntry(uriList, std::begin(file->getRemainingUris()),
                 std::end(file->getRemainingUris()), VLB_WAITING);
}

void createFileEntry(List* files,
                     const std::vector<std::shared_ptr<FileEntry>>& entries,
                     const std::vector<int64_t>& completedLengths)
{
  size_t index = 1;
  for (const auto& file : entries) {
    auto entry = Dict::g();
    entry->put(KEY_INDEX, util::uitos(index));
    entry->put(KEY_PATH, file->getPath());
    entry->put(KEY_SELECTED, file->isRequested() ? VLB_TRUE : VLB_FALSE);
    entry->put(KEY_LENGTH, util::itos(file->getLength()));
    const auto completedLength =
        index <= completedLengths.size() ? completedLengths[index - 1] : 0;
    entry->put(KEY_COMPLETED_LENGTH, util::itos(completedLength));

    auto uriList = List::g();
    createUriEntry(uriList.get(), file);
    entry->put(KEY_URIS, std::move(uriList));
    files->append(std::move(entry));
    ++index;
  }
}

#ifdef ENABLE_BITTORRENT
void createBtFileEntry(List* files, const BtSnapshot& snapshot)
{
  size_t index = 1;
  for (const auto& file : snapshot.files) {
    auto entry = Dict::g();
    entry->put(KEY_INDEX, util::uitos(index++));
    entry->put(KEY_PATH, file.path);
    entry->put(KEY_SELECTED, file.selected ? VLB_TRUE : VLB_FALSE);
    entry->put(KEY_LENGTH, util::itos(file.length));
    entry->put(KEY_COMPLETED_LENGTH, util::itos(file.completedLength));
    const char* priority = file.priority == 0   ? "off"
                           : file.priority == 6 ? "high"
                           : file.priority == 7 ? "top"
                                                : "normal";
    entry->put(KEY_PRIORITY, priority);
    entry->put(KEY_URIS, List::g());
    files->append(std::move(entry));
  }
}

#endif

} // namespace aria2::rpc::detail
