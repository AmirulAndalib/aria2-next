/* <!-- copyright */
/*
 * aria2 - The high speed download utility
 *
 * Copyright (C) 2026 The aria2-next contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
/* copyright --> */
#ifndef D_BT_DOWNLOAD_SUPPORT_H
#define D_BT_DOWNLOAD_SUPPORT_H
#include "BtDownloadImpl.h"

namespace aria2 {
struct BtMetadata;
namespace bt_download {
void assignHashes(BtMetadata* attrs, BtSnapshot& snapshot,
                  const libtorrent::info_hash_t& hashes);
std::vector<std::vector<std::string>>
announceList(const libtorrent::add_torrent_params& params);
bool hasAllPieces(
    const libtorrent::typed_bitfield<libtorrent::piece_index_t>& pieces,
    int pieceCount);
std::vector<BtTrackerSpec>
trackerSpecs(const libtorrent::add_torrent_params& params,
             BtTrackerOrigin origin);
} // namespace bt_download
} // namespace aria2
#endif // D_BT_DOWNLOAD_SUPPORT_H
