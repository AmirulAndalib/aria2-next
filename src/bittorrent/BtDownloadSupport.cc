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
#include "bittorrent/BtDownloadSupport.h"
#include "BtSnapshot.h"
#include <cstddef>
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/bitfield.hpp>
#include <libtorrent/info_hash.hpp>
#include <libtorrent/units.hpp>
#include <utility>
#include <vector>
#include "BtDownload.h"
#include "BtDownloadImpl.h"
#include "BtMetadata.h"
#include "support/Encoding.h"
#include <map>

namespace aria2::bt_download {

namespace lt = libtorrent;

void assignHashes(BtMetadata* attrs, BtSnapshot& snapshot,
                  const lt::info_hash_t& hashes)
{
  if (hashes.has_v1()) {
    attrs->infoHash = hashes.v1.to_string();
    snapshot.infoHashV1 = util::toHex(hashes.v1.to_string());
  }
  if (hashes.has_v2()) {
    attrs->infoHashV2 = hashes.v2.to_string();
    snapshot.infoHashV2 = util::toHex(hashes.v2.to_string());
    if (attrs->infoHash.empty()) {
      attrs->infoHash = attrs->infoHashV2;
    }
  }
}

std::vector<std::vector<std::string>>
announceList(const lt::add_torrent_params& params)
{
  std::map<int, std::vector<std::string>> tiers;
  for (size_t i = 0; i < params.trackers.size(); ++i) {
    const int tier =
        i < params.tracker_tiers.size() ? params.tracker_tiers[i] : 0;
    tiers[tier].push_back(params.trackers[i]);
  }

  std::vector<std::vector<std::string>> result;
  result.reserve(tiers.size());
  for (auto& entry : tiers) {
    result.push_back(std::move(entry.second));
  }
  return result;
}

bool hasAllPieces(const lt::typed_bitfield<lt::piece_index_t>& pieces,
                  int pieceCount)
{
  if (pieces.size() < pieceCount) {
    return false;
  }
  for (int index = 0; index < pieceCount; ++index) {
    if (!pieces[lt::piece_index_t{index}]) {
      return false;
    }
  }
  return true;
}

std::vector<BtTrackerSpec> trackerSpecs(const lt::add_torrent_params& params,
                                        BtTrackerOrigin origin)
{
  std::vector<BtTrackerSpec> result;
  result.reserve(params.trackers.size());
  for (size_t i = 0; i < params.trackers.size(); ++i) {
    result.push_back(
        {params.trackers[i],
         i < params.tracker_tiers.size() ? params.tracker_tiers[i] : 0,
         origin});
  }
  return result;
}

} // namespace aria2::bt_download
