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
#include "ed2k_search.h"
#include <cstddef>
#include <vector>
#include "Ed2kAttribute.h"
#include <algorithm>

namespace aria2 {

bool matchesSearchQuery(const ed2k::SearchResultEntry& entry,
                        const ed2k::SearchQuery& query)
{
  if (query.minSize > 0 && entry.size > 0 && entry.size < query.minSize) {
    return false;
  }
  if (query.maxSize > 0 && entry.size > query.maxSize) {
    return false;
  }
  if (query.minSourceCount > 0 && entry.sourceCount < query.minSourceCount) {
    return false;
  }
  if (query.minCompleteSourceCount > 0 &&
      entry.completeSourceCount < query.minCompleteSourceCount) {
    return false;
  }
  return true;
}

size_t addEd2kSearchResults(Ed2kAttribute* attrs,
                            const std::vector<ed2k::SearchResultEntry>& entries,
                            bool moreResults)
{
  if (!attrs) {
    return 0;
  }
  size_t added = 0;
  for (const auto& entry : entries) {
    if (!matchesSearchQuery(entry, attrs->searchQuery)) {
      continue;
    }
    auto i = std::find_if(
        attrs->searchResults.begin(), attrs->searchResults.end(),
        [&](const ed2k::SearchResultEntry& item) {
          return item.hash == entry.hash && item.size == entry.size;
        });
    if (i == attrs->searchResults.end()) {
      attrs->searchResults.push_back(entry);
      ++added;
    }
    else {
      if (i->name.empty()) {
        i->name = entry.name;
      }
      i->sourceCount = std::max(i->sourceCount, entry.sourceCount);
      i->completeSourceCount =
          std::max(i->completeSourceCount, entry.completeSourceCount);
      if (i->fileType.empty()) {
        i->fileType = entry.fileType;
      }
      if (i->extension.empty()) {
        i->extension = entry.extension;
      }
      if (i->mediaArtist.empty()) {
        i->mediaArtist = entry.mediaArtist;
      }
      if (i->mediaAlbum.empty()) {
        i->mediaAlbum = entry.mediaAlbum;
      }
      if (i->mediaTitle.empty()) {
        i->mediaTitle = entry.mediaTitle;
      }
      if (i->mediaLength.empty()) {
        i->mediaLength = entry.mediaLength;
      }
      if (i->mediaBitrate == 0) {
        i->mediaBitrate = entry.mediaBitrate;
      }
      if (i->mediaCodec.empty()) {
        i->mediaCodec = entry.mediaCodec;
      }
      if (i->ed2kLink.empty()) {
        i->ed2kLink = entry.ed2kLink;
      }
      if (i->sourceNetwork.empty()) {
        i->sourceNetwork = entry.sourceNetwork;
      }
      else if (!entry.sourceNetwork.empty() &&
               i->sourceNetwork.find(entry.sourceNetwork) ==
                   std::string::npos) {
        i->sourceNetwork += "|" + entry.sourceNetwork;
      }
    }
  }
  attrs->searchMoreResults = moreResults;
  return added;
}

} // namespace aria2
