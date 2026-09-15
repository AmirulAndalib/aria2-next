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
#include "ContextAttribute.h"
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <utility>
#include <vector>
#include "BtDownload.h"
#include "BtDownloadImpl.h"
#include "bittorrent/BtDownloadSupport.h"
#include "BtMetadata.h"
#include "DownloadContext.h"
#include "RequestGroup.h"
#include "Option.h"
#include "Log.h"
#include "support/Text.h"
#include "prefs.h"
#include "fmt.h"
#include <libtorrent/magnet_uri.hpp>
#include <algorithm>
#include <array>
#include <limits>

namespace aria2 {

namespace lt = libtorrent;
using namespace bt_download;

namespace {
bool unsupportedTracker(const std::string& url)
{
  return util::startsWith(url, "ws://") || util::startsWith(url, "wss://");
}

} // namespace

namespace {
size_t trackerProtocolGroup(const std::string& url)
{
  if (util::startsWith(url, "https://")) {
    return 2;
  }
  if (util::startsWith(url, "http://")) {
    return 1;
  }
  return 0;
}

} // namespace

namespace {
const char* trackerOriginName(BtTrackerOrigin origin)
{
  switch (origin) {
  case BtTrackerOrigin::Metainfo:
    return "metainfo";
  case BtTrackerOrigin::Magnet:
    return "magnet";
  case BtTrackerOrigin::Resume:
    return "resume";
  case BtTrackerOrigin::Global:
    return "global";
  case BtTrackerOrigin::Rpc:
    return "rpc";
  }
  return "unknown";
}

} // namespace

namespace {
size_t normalizeTrackerTiers(std::vector<BtTrackerSpec>& trackers,
                             int highestTier)
{
  if (trackers.empty()) {
    return 0;
  }

  std::vector<int> declaredTiers;
  declaredTiers.reserve(trackers.size());
  for (const auto& tracker : trackers) {
    declaredTiers.push_back(tracker.tier);
  }
  std::sort(declaredTiers.begin(), declaredTiers.end());
  declaredTiers.erase(std::unique(declaredTiers.begin(), declaredTiers.end()),
                      declaredTiers.end());

  const auto highestRank = static_cast<size_t>(highestTier);
  for (auto& tracker : trackers) {
    const auto rank = static_cast<size_t>(
        std::lower_bound(declaredTiers.begin(), declaredTiers.end(),
                         tracker.tier) -
        declaredTiers.begin());
    tracker.tier = static_cast<int>(std::min(rank, highestRank));
  }
  return declaredTiers.size();
}

} // namespace

namespace {
std::vector<BtTrackerSpec>
filterSourceTrackers(std::vector<BtTrackerSpec> effectiveTrackers,
                     const Option* option)
{
  std::vector<std::string> excludedTrackers;
  const auto& excluded = option->get(PREF_BT_EXCLUDE_TRACKER);
  util::split(excluded.begin(), excluded.end(),
              std::back_inserter(excludedTrackers), ',', true);
  if (std::find(excludedTrackers.begin(), excludedTrackers.end(), "*") !=
      excludedTrackers.end()) {
    effectiveTrackers.clear();
  }
  effectiveTrackers.erase(
      std::remove_if(effectiveTrackers.begin(), effectiveTrackers.end(),
                     [&excludedTrackers](const BtTrackerSpec& tracker) {
                       return std::find(excludedTrackers.begin(),
                                        excludedTrackers.end(),
                                        tracker.url) != excludedTrackers.end();
                     }),
      effectiveTrackers.end());
  std::vector<BtTrackerSpec> uniqueTrackers;
  uniqueTrackers.reserve(effectiveTrackers.size());
  for (auto& tracker : effectiveTrackers) {
    if (tracker.url.empty() || unsupportedTracker(tracker.url) ||
        std::find_if(uniqueTrackers.begin(), uniqueTrackers.end(),
                     [&tracker](const BtTrackerSpec& entry) {
                       return entry.url == tracker.url;
                     }) != uniqueTrackers.end()) {
      continue;
    }
    uniqueTrackers.push_back(std::move(tracker));
  }
  return uniqueTrackers;
}

} // namespace

namespace {
std::vector<std::string>
additionalTrackers(const std::vector<BtTrackerSpec>& effectiveTrackers,
                   const Option* option, bool privateTorrent)
{
  std::vector<std::string> addedTrackers;
  const auto& added = option->get(PREF_BT_TRACKER);
  util::split(added.begin(), added.end(), std::back_inserter(addedTrackers),
              ',', true);
  std::vector<std::string> usableTrackers;
  if (!privateTorrent) {
    for (auto tracker : addedTrackers) {
      tracker = util::strip(tracker);
      if (tracker.empty() ||
          std::find_if(effectiveTrackers.begin(), effectiveTrackers.end(),
                       [&tracker](const BtTrackerSpec& entry) {
                         return entry.url == tracker;
                       }) != effectiveTrackers.end()) {
        continue;
      }
      if (unsupportedTracker(tracker)) {
        A2_LOG_DEBUG(fmt("Ignoring unsupported WebTorrent tracker: %s",
                         logging::sanitizeUri(tracker).c_str()));
        continue;
      }
      if (std::find(usableTrackers.begin(), usableTrackers.end(), tracker) ==
          usableTrackers.end()) {
        usableTrackers.push_back(std::move(tracker));
      }
    }
  }
  return usableTrackers;
}

} // namespace

namespace {
void appendGlobalTrackers(std::vector<BtTrackerSpec>& effectiveTrackers,
                          const std::vector<std::string>& usableTrackers,
                          int maxNativeTier)
{
  if (!usableTrackers.empty()) {
    const int baseTier =
        effectiveTrackers.empty()
            ? 0
            : std::max_element(
                  effectiveTrackers.begin(), effectiveTrackers.end(),
                  [](const BtTrackerSpec& lhs, const BtTrackerSpec& rhs) {
                    return lhs.tier < rhs.tier;
                  })->tier +
                  1;
    std::array<int, 3> protocolTiers{{-1, -1, -1}};
    auto nextTier = baseTier;
    for (const auto& tracker : usableTrackers) {
      const auto group = trackerProtocolGroup(tracker);
      if (protocolTiers[group] < 0) {
        protocolTiers[group] = nextTier;
        nextTier = std::min(nextTier + 1, maxNativeTier);
      }
      effectiveTrackers.push_back(
          {tracker, protocolTiers[group], BtTrackerOrigin::Global});
    }
  }
}

} // namespace

std::string BtDownload::trackerSource(const std::string& url) const
{
  const auto found = std::find_if(
      impl_->effectiveTrackers.begin(), impl_->effectiveTrackers.end(),
      [&url](const BtTrackerSpec& entry) { return entry.url == url; });
  return found == impl_->effectiveTrackers.end()
             ? "unknown"
             : trackerOriginName(found->origin);
}

void BtDownload::applyTrackers(std::vector<BtTrackerSpec> effectiveTrackers)
{
  std::vector<std::string> trackers;
  std::vector<int> trackerTiers;
  trackers.reserve(effectiveTrackers.size());
  trackerTiers.reserve(effectiveTrackers.size());
  for (const auto& tracker : effectiveTrackers) {
    trackers.push_back(tracker.url);
    trackerTiers.push_back(tracker.tier);
  }
  if (trackers != impl_->params.trackers ||
      trackerTiers != impl_->params.tracker_tiers) {
    impl_->params.trackers = std::move(trackers);
    impl_->params.tracker_tiers = std::move(trackerTiers);
    ++impl_->trackerRevision;
  }
  impl_->effectiveTrackers = std::move(effectiveTrackers);
  snapshot_.announceList = announceList(impl_->params);
  snapshot_.magnetLink = lt::make_magnet_uri(impl_->params);
  snapshot_.webSeeds = impl_->params.url_seeds;
  if (group_ && group_->getDownloadContext()->hasAttribute(CTX_ATTR_BT)) {
    static_cast<BtMetadata*>(
        group_->getDownloadContext()->getAttribute(CTX_ATTR_BT).get())
        ->announceList = snapshot_.announceList;
  }
}

void BtDownload::configureTrackers(const Option* option)
{
  auto effectiveTrackers = impl_->sourceTrackers;

  if (!impl_->trackerOverride) {
    effectiveTrackers =
        filterSourceTrackers(std::move(effectiveTrackers), option);
    const auto usableTrackers =
        additionalTrackers(effectiveTrackers, option,
                           impl_->params.ti && impl_->params.ti->priv());

    constexpr int maxNativeTier = std::numeric_limits<std::uint8_t>::max();
    const int highestSourceTier =
        usableTrackers.empty() ? maxNativeTier : maxNativeTier - 1;
    const auto declaredTierCount =
        normalizeTrackerTiers(effectiveTrackers, highestSourceTier);
    const auto nativeSourceTierCount =
        static_cast<size_t>(highestSourceTier) + 1;
    if (declaredTierCount > nativeSourceTierCount &&
        !impl_->trackerTierCompressionReported) {
      A2_LOG_WARN(fmt("Compressed %lu source tracker tiers to %lu to fit "
                      "libtorrent's native tier range",
                      static_cast<unsigned long>(declaredTierCount),
                      static_cast<unsigned long>(nativeSourceTierCount)));
      impl_->trackerTierCompressionReported = true;
    }
    appendGlobalTrackers(effectiveTrackers, usableTrackers, maxNativeTier);
  }
  else {
    normalizeTrackerTiers(effectiveTrackers,
                          std::numeric_limits<std::uint8_t>::max());
  }
  applyTrackers(std::move(effectiveTrackers));
}

} // namespace aria2
