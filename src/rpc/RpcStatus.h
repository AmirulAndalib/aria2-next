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
#ifndef D_RPC_STATUS_H
#define D_RPC_STATUS_H

#include "common.h"
#include "ValueBase.h"
#include <memory>
#include <string>
#include <vector>

namespace aria2 {
class DownloadEngine;
class RequestGroup;
class RequestGroupMan;
class FileEntry;
struct Ed2kAttribute;
struct DownloadResult;
struct BtSnapshot;
struct BtMetadata;
namespace media {
struct Snapshot;
}
namespace rpc {
// Empty keys select every field. Numeric strings and omission rules are part
// of the RPC wire contract, including projections of stopped tasks.
void gatherProgressCommon(Dict*, const std::shared_ptr<RequestGroup>&,
                          const std::vector<std::string>&);
void gatherStoppedDownload(Dict*, const std::shared_ptr<DownloadResult>&,
                           const std::vector<std::string>&);
namespace detail {
bool requested_key(const std::vector<std::string>& keys,
                   const std::string& key);
void createUriEntry(List*, const std::shared_ptr<FileEntry>&);
void createFileEntry(List*, const std::vector<std::shared_ptr<FileEntry>>&,
                     const std::vector<int64_t>& completedLengths);
std::unique_ptr<Dict> createEd2kStatusEntry(const Ed2kAttribute*,
                                            RequestGroupMan*,
                                            int64_t sharingTime);
void gatherMedia(Dict*, const media::Snapshot&);
void gatherProgress(Dict*, const std::shared_ptr<RequestGroup>&,
                    DownloadEngine*, const std::vector<std::string>&);
#ifdef ENABLE_BITTORRENT
void createBtFileEntry(List*, const BtSnapshot&);
void gatherBitTorrentMetadata(Dict*, const BtSnapshot&, const BtMetadata*);
void gatherProgressBitTorrent(Dict*, const std::shared_ptr<RequestGroup>&,
                              const std::vector<std::string>&);
#endif
} // namespace detail
} // namespace rpc
} // namespace aria2

#endif // D_RPC_STATUS_H
