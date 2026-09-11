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
#include "RpcMethodFactory.h"
#include <memory>
#include <string>
#include <vector>
#include "rpc/RpcMethods.h"

#include <algorithm>
#include <iterator>
#include <map>

namespace aria2::rpc {
namespace {
struct MethodEntry {
  const char* name;
  std::unique_ptr<RpcMethod> (*create)();
};

template <typename T> MethodEntry method()
{
  return {T::getMethodName(),
          []() -> std::unique_ptr<RpcMethod> { return std::make_unique<T>(); }};
}

// Enumeration and dispatch share this catalogue; order is part of listMethods.
const MethodEntry methods[] = {
    method<AddUriRpcMethod>(),
    method<FinishMediaRpcMethod>(),
    method<Ed2kSearchRpcMethod>(),
    method<GetEd2kSearchResultsRpcMethod>(),
#ifdef ENABLE_BITTORRENT
    method<AddTorrentRpcMethod>(),
    method<InspectTorrentRpcMethod>(),
    method<GetPeersRpcMethod>(),
    method<GetBtTrackersRpcMethod>(),
    method<GetBtSessionStatusRpcMethod>(),
    method<ForceBtRecheckRpcMethod>(),
    method<ForceBtAnnounceRpcMethod>(),
    method<ReplaceBtTrackersRpcMethod>(),
    method<ReplaceBtWebSeedsRpcMethod>(),
    method<AddBtPeersRpcMethod>(),
    method<SetBtPeerBlocklistRpcMethod>(),
#endif // ENABLE_BITTORRENT
#ifdef ENABLE_METALINK
    method<AddMetalinkRpcMethod>(),
#endif // ENABLE_METALINK
    method<RemoveRpcMethod>(),
    method<PauseRpcMethod>(),
    method<ForcePauseRpcMethod>(),
    method<PauseAllRpcMethod>(),
    method<ForcePauseAllRpcMethod>(),
    method<UnpauseRpcMethod>(),
    method<UnpauseAllRpcMethod>(),
    method<ForceRemoveRpcMethod>(),
    method<ChangePositionRpcMethod>(),
    method<TellStatusRpcMethod>(),
    method<GetUrisRpcMethod>(),
    method<GetFilesRpcMethod>(),
    method<GetServersRpcMethod>(),
    method<TellActiveRpcMethod>(),
    method<TellWaitingRpcMethod>(),
    method<TellStoppedRpcMethod>(),
    method<GetOptionRpcMethod>(),
    method<ChangeUriRpcMethod>(),
    method<ChangeOptionRpcMethod>(),
    method<GetGlobalOptionRpcMethod>(),
    method<ChangeGlobalOptionRpcMethod>(),
    method<PurgeDownloadResultRpcMethod>(),
    method<RemoveDownloadResultRpcMethod>(),
    method<GetVersionRpcMethod>(),
    method<GetSessionInfoRpcMethod>(),
    method<ShutdownRpcMethod>(),
    method<ForceShutdownRpcMethod>(),
    method<GetGlobalStatRpcMethod>(),
    method<SaveSessionRpcMethod>(),
    method<SystemMulticallRpcMethod>(),
    method<SystemListMethodsRpcMethod>(),
    method<SystemListNotificationsRpcMethod>(),
};
std::map<std::string, std::unique_ptr<RpcMethod>> cache;
std::unique_ptr<RpcMethod> noSuchRpcMethod;
} // namespace

const std::vector<std::string>& allMethodNames()
{
  static const auto names = [] {
    std::vector<std::string> result;
    result.reserve(std::size(methods));
    for (const auto& method : methods) {
      result.emplace_back(method.name);
    }
    return result;
  }();
  return names;
}

const std::vector<std::string>& allNotificationsNames()
{
  static const std::vector<std::string> names = {
      "aria2.onDownloadStart",      "aria2.onDownloadPause",
      "aria2.onDownloadStop",       "aria2.onDownloadComplete",
      "aria2.onDownloadError",
#ifdef ENABLE_BITTORRENT
      "aria2.onBtDownloadComplete",
#endif // ENABLE_BITTORRENT
  };
  return names;
}

RpcMethod* getMethod(const std::string& methodName)
{
  const auto cached = cache.find(methodName);
  if (cached != cache.end()) {
    return cached->second.get();
  }
  const auto entry =
      std::find_if(std::begin(methods), std::end(methods),
                   [&](const auto& value) { return methodName == value.name; });
  if (entry != std::end(methods)) {
    return cache.emplace(methodName, entry->create()).first->second.get();
  }
  if (!noSuchRpcMethod) {
    noSuchRpcMethod = std::make_unique<NoSuchMethodRpcMethod>();
  }
  return noSuchRpcMethod.get();
}
} // namespace aria2::rpc
