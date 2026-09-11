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
#include "ed2k_link.h"
#include "ed2k_peer.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include "common.h" // IWYU pragma: keep

#include "RpcStatus.h"
#include "RpcFields.h"
#include "Ed2kAttribute.h"
#include "Ed2kSession.h"
#include "Ed2kUploadQueue.h"
#include "RequestGroupMan.h"
#include "support/Numbers.h"
#include "support/Encoding.h"

namespace aria2::rpc::detail {

using namespace fields;

size_t countEd2kConnectedServers(const Ed2kAttribute* attrs)
{
  size_t count = 0;
  for (const auto& state : attrs->serverStates) {
    if (state.connected || state.handshakeCompleted) {
      ++count;
    }
  }
  return count;
}

size_t countEd2kQueuedPeers(const Ed2kAttribute* attrs)
{
  size_t count = 0;
  for (const auto& state : attrs->peerStates) {
    if (state.queued) {
      ++count;
    }
  }
  return count;
}

size_t countEd2kAcceptedPeers(const Ed2kAttribute* attrs)
{
  size_t count = 0;
  for (const auto& state : attrs->peerStates) {
    if (state.accepted) {
      ++count;
    }
  }
  return count;
}

size_t countEd2kDeadPeers(const Ed2kAttribute* attrs)
{
  size_t count = 0;
  for (const auto& state : attrs->peerStates) {
    if (state.dead) {
      ++count;
    }
  }
  return count;
}

size_t countEd2kLowIdPeers(const Ed2kAttribute* attrs)
{
  size_t count = 0;
  for (const auto& state : attrs->peerStates) {
    if (state.lowId) {
      ++count;
    }
  }
  return count;
}

size_t countEd2kCallbackWaitingPeers(const Ed2kAttribute* attrs)
{
  size_t count = 0;
  for (const auto& state : attrs->peerStates) {
    if (state.lowIdCallbackState == ed2k::LowIdCallbackState::REQUESTED) {
      ++count;
    }
  }
  return count;
}

std::unique_ptr<Dict> createEd2kStatusEntry(const Ed2kAttribute* attrs,
                                            RequestGroupMan* rgman,
                                            int64_t sharingTime)
{
  auto dict = Dict::g();
  if (!attrs->link.hash.empty()) {
    dict->put(KEY_HASH, util::toHex(attrs->link.hash));
  }
  if (!attrs->link.name.empty()) {
    dict->put(KEY_NAME, attrs->link.name);
  }
  if (attrs->link.size > 0) {
    dict->put(KEY_LENGTH, util::itos(attrs->link.size));
  }
  if (attrs->link.type == ed2k::LinkType::FILE && !attrs->link.hash.empty() &&
      !attrs->link.name.empty() && attrs->link.size > 0) {
    dict->put(KEY_ED2K_LINK, ed2k::toFileLink(attrs->link));
  }
  dict->put("partHashCount", util::uitos(attrs->pieceHashes.size()));
  if (!attrs->aichRootHash.empty()) {
    dict->put("aichRoot", util::toHex(attrs->aichRootHash));
  }
  dict->put("serverCount", util::uitos(attrs->serverStates.size()));
  dict->put("connectedServerCount",
            util::uitos(countEd2kConnectedServers(attrs)));
  dict->put("peerCount", util::uitos(attrs->peerStates.size()));
  dict->put("queuedPeerCount", util::uitos(countEd2kQueuedPeers(attrs)));
  dict->put("acceptedPeerCount", util::uitos(countEd2kAcceptedPeers(attrs)));
  dict->put("deadPeerCount", util::uitos(countEd2kDeadPeers(attrs)));
  dict->put("lowIdPeerCount", util::uitos(countEd2kLowIdPeers(attrs)));
  dict->put("callbackWaitingPeerCount",
            util::uitos(countEd2kCallbackWaitingPeers(attrs)));
  dict->put("kadNodeCount", util::uitos(attrs->kadRoutingTable
                                            ? attrs->kadRoutingTable->liveSize()
                                            : 0));
  dict->put("kadRouterCount",
            util::uitos(attrs->kadRoutingTable
                            ? attrs->kadRoutingTable->getRouterNodes().size()
                            : 0));
  dict->put("kadFirewalled",
            attrs->kadFirewalled ? Bool::gTrue() : Bool::gFalse());
  dict->put("kadObservedAddressCount",
            util::uitos(attrs->kadObservedAddresses.size()));
  dict->put("searchActive",
            attrs->searchActive ? Bool::gTrue() : Bool::gFalse());
  dict->put("searchMoreResults",
            attrs->searchMoreResults ? Bool::gTrue() : Bool::gFalse());
  dict->put("searchResultCount", util::uitos(attrs->searchResults.size()));
  dict->put("sharingTime", util::itos(sharingTime));
  if (rgman && rgman->getEd2kUploadQueue()) {
    auto uploadQueue = rgman->getEd2kUploadQueue();
    dict->put("uploadingPeerCount", util::uitos(uploadQueue->uploadingCount()));
    dict->put("waitingUploadPeerCount",
              util::uitos(uploadQueue->waitingCount()));
    dict->put("peerCreditCount",
              util::uitos(uploadQueue->credits().list().size()));
  }
  return dict;
}

} // namespace aria2::rpc::detail
