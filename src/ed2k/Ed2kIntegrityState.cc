/* <!-- copyright */
/*
 * aria2 - The high speed download utility
 *
 * Copyright (C) 2026 aria2-next contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
/* copyright --> */
#include <cstddef>
#include <string>
#include <utility>
#include "Ed2kAttribute.h"
#include "ed2k_hash.h"
#include <algorithm>

namespace aria2 {

bool recordEd2kAichHashVote(Ed2kAttribute* attrs, const std::string& rootHash,
                            const std::string& voter)
{
  if (!attrs || rootHash.size() != ed2k::AICH_HASH_LENGTH || voter.empty()) {
    return false;
  }
  if (!attrs->link.aichHash.empty()) {
    if (rootHash != attrs->link.aichHash) {
      return false;
    }
    attrs->aichRootHash = rootHash;
    attrs->aichRootTrusted = true;
    return true;
  }
  for (const auto& vote : attrs->aichHashVotes) {
    if (std::find(vote.voters.begin(), vote.voters.end(), voter) !=
        vote.voters.end()) {
      return attrs->aichRootTrusted && attrs->aichRootHash == rootHash;
    }
  }
  auto vote = std::find_if(
      attrs->aichHashVotes.begin(), attrs->aichHashVotes.end(),
      [&](const Ed2kAichHashVote& item) { return item.rootHash == rootHash; });
  if (vote == attrs->aichHashVotes.end()) {
    Ed2kAichHashVote item;
    item.rootHash = rootHash;
    item.voters.push_back(voter);
    attrs->aichHashVotes.push_back(std::move(item));
  }
  else {
    vote->voters.push_back(voter);
  }

  size_t total = 0;
  const Ed2kAichHashVote* best = nullptr;
  for (const auto& item : attrs->aichHashVotes) {
    total += item.voters.size();
    if (!best || item.voters.size() > best->voters.size()) {
      best = &item;
    }
  }
  if (!best) {
    return false;
  }
  attrs->aichRootHash = best->rootHash;
  attrs->aichRootTrusted =
      best->voters.size() >= 10 && best->voters.size() * 100 / total >= 92;
  return attrs->aichRootTrusted && attrs->aichRootHash == rootHash;
}

} // namespace aria2
