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
#include "GroupId.h"
#include "RpcRequest.h"
#include "ValueBase.h"
#include <cstddef>
#include <memory>
#include <vector>
#include "common.h" // IWYU pragma: keep

#include "RpcMethods.h"
#include "RpcRequestHelpers.h"
#include "DownloadEngine.h"
#include "RequestGroup.h"
#include "fmt.h"
#include "DlAbortEx.h"
#include "BtDownload.h"
#include "BtSession.h"

namespace aria2::rpc {

using namespace detail;

namespace {
std::shared_ptr<BtDownload> requireBtDownload(const RpcRequest& req,
                                              DownloadEngine* e)
{
  const String* gidParam = checkRequiredParam<String>(req, 0);
  const auto gid = str2Gid(gidParam);
  const auto group = e->getRequestGroupMan()->findGroup(gid);
  if (!group || !group->getBtDownload()) {
    throw DL_ABORT_EX(fmt("No BitTorrent task is available for GID#%s",
                          GroupId::toHex(gid).c_str()));
  }
  return group->getBtDownload();
}
} // namespace

std::unique_ptr<ValueBase>
ForceBtRecheckRpcMethod::process(const RpcRequest& req, DownloadEngine* e)
{
  const auto download = requireBtDownload(req, e);
  e->getBtSession()->forceRecheck(download);
  return createGIDResponse(download->group()->getGID());
}

std::unique_ptr<ValueBase>
ForceBtAnnounceRpcMethod::process(const RpcRequest& req, DownloadEngine* e)
{
  const auto download = requireBtDownload(req, e);
  e->getBtSession()->forceAnnounce(download);
  return createGIDResponse(download->group()->getGID());
}

std::unique_ptr<ValueBase>
ReplaceBtTrackersRpcMethod::process(const RpcRequest& req, DownloadEngine* e)
{
  const auto download = requireBtDownload(req, e);
  const List* trackersParam = checkRequiredParam<List>(req, 1);
  std::vector<BtTrackerConfig> trackers;
  trackers.reserve(trackersParam->size());
  size_t index = 0;
  for (const auto& value : *trackersParam) {
    const auto entry = downcast<Dict>(value);
    const auto url = entry ? downcast<String>(entry->get("url")) : nullptr;
    const auto tier = entry ? downcast<Integer>(entry->get("tier")) : nullptr;
    if (!url || !tier) {
      throw DL_ABORT_EX(fmt("The tracker at index %lu must contain string "
                            "url and integer tier fields.",
                            static_cast<unsigned long>(index)));
    }
    trackers.push_back({url->s(), static_cast<int>(tier->i())});
    ++index;
  }
  e->getBtSession()->replaceTrackers(download, trackers);
  return createGIDResponse(download->group()->getGID());
}

std::unique_ptr<ValueBase>
ReplaceBtWebSeedsRpcMethod::process(const RpcRequest& req, DownloadEngine* e)
{
  const auto download = requireBtDownload(req, e);
  const List* webSeedsParam = checkRequiredParam<List>(req, 1);
  std::vector<std::string> webSeeds;
  webSeeds.reserve(webSeedsParam->size());
  size_t index = 0;
  for (const auto& value : *webSeedsParam) {
    const auto webSeed = downcast<String>(value);
    if (!webSeed) {
      throw DL_ABORT_EX(fmt("The web seed at index %lu has wrong type.",
                            static_cast<unsigned long>(index)));
    }
    webSeeds.push_back(webSeed->s());
    ++index;
  }
  e->getBtSession()->replaceWebSeeds(download, webSeeds);
  return createGIDResponse(download->group()->getGID());
}

std::unique_ptr<ValueBase> AddBtPeersRpcMethod::process(const RpcRequest& req,
                                                        DownloadEngine* e)
{
  const auto download = requireBtDownload(req, e);
  const List* peersParam = checkRequiredParam<List>(req, 1);
  std::vector<std::string> peers;
  peers.reserve(peersParam->size());
  size_t index = 0;
  for (const auto& value : *peersParam) {
    const auto peer = downcast<String>(value);
    if (!peer) {
      throw DL_ABORT_EX(fmt("The peer at index %lu has wrong type.",
                            static_cast<unsigned long>(index)));
    }
    peers.push_back(peer->s());
    ++index;
  }
  const auto result = e->getBtSession()->addPeers(download, peers);
  auto response = Dict::g();
  response->put("added", Integer::g(result.first));
  response->put("failed", Integer::g(result.second));
  return response;
}

std::unique_ptr<ValueBase>
SetBtPeerBlocklistRpcMethod::process(const RpcRequest& req, DownloadEngine* e)
{
  const List* rulesParam = checkRequiredParam<List>(req, 0);
  std::vector<std::string> rules;
  rules.reserve(rulesParam->size());
  size_t index = 0;
  for (const auto& value : *rulesParam) {
    const auto rule = downcast<String>(value);
    if (!rule) {
      throw DL_ABORT_EX(fmt("The blocklist rule at index %lu has wrong type.",
                            static_cast<unsigned long>(index)));
    }
    rules.push_back(rule->s());
    ++index;
  }

  std::string error;
  if (!e->getBtSession()->replaceIpFilter(rules, error) && !error.empty()) {
    throw DL_ABORT_EX(error);
  }
  auto result = Dict::g();
  result->put("ruleCount", Integer::g(e->getBtSession()->ipFilterRuleCount()));
  result->put("revision", Integer::g(e->getBtSession()->ipFilterRevision()));
  return result;
}

} // namespace aria2::rpc
