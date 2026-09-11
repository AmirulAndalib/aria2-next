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
#include "BtSnapshot.h"
#include "GroupId.h"
#include "RpcRequest.h"
#include "ValueBase.h"
#include <memory>
#include <utility>
#include "common.h" // IWYU pragma: keep

#include "RpcMethods.h"
#include "RpcRequestHelpers.h"
#include "RpcFields.h"
#include "DownloadEngine.h"
#include "RequestGroup.h"
#include "support/Numbers.h"
#include "support/Encoding.h"
#include "fmt.h"
#include "DlAbortEx.h"
#include "BtDownload.h"
#include "BtSession.h"

namespace aria2::rpc {

using namespace fields;
using namespace detail;

namespace {
void gatherPeer(List* peers, const BtSnapshot& snapshot)
{
  for (const auto& peer : snapshot.peers) {
    auto entry = Dict::g();
    if (peer.state == "connected") {
      entry->put(KEY_PEER_ID, util::torrentPercentEncode(peer.peerId));
    }
    if (!peer.clientName.empty()) {
      entry->put(KEY_PEER_CLIENT_NAME, peer.clientName);
    }
    entry->put(KEY_IP, peer.ip);
    entry->put(KEY_PORT, util::uitos(peer.port));
    entry->put(KEY_BITFIELD, peer.bitfield);
    entry->put(KEY_AM_CHOKING, peer.amChoking ? VLB_TRUE : VLB_FALSE);
    entry->put(KEY_AM_INTERESTED, peer.amInterested ? VLB_TRUE : VLB_FALSE);
    entry->put(KEY_PEER_CHOKING, peer.peerChoking ? VLB_TRUE : VLB_FALSE);
    entry->put(KEY_PEER_INTERESTED, peer.peerInterested ? VLB_TRUE : VLB_FALSE);
    entry->put(KEY_DOWNLOAD_SPEED, util::itos(peer.downloadSpeed));
    entry->put(KEY_UPLOAD_SPEED, util::itos(peer.uploadSpeed));
    entry->put(KEY_DOWNLOADED, util::itos(peer.downloaded));
    entry->put(KEY_UPLOADED, util::itos(peer.uploaded));
    entry->put(KEY_COMPLETED_LENGTH, util::itos(peer.completedLength));
    entry->put(KEY_PROGRESS, fmt("%.6f", peer.progressPpm / 1000000.0));
    entry->put(KEY_FLAGS, peer.flags);
    entry->put(KEY_INCOMING, peer.incoming ? VLB_TRUE : VLB_FALSE);
    entry->put(KEY_SNUBBED, peer.snubbed ? VLB_TRUE : VLB_FALSE);
    entry->put(KEY_OPTIMISTIC_UNCHOKE,
               peer.optimisticUnchoke ? VLB_TRUE : VLB_FALSE);
    entry->put("state", peer.state);
    entry->put(KEY_SEEDER, peer.seeder ? VLB_TRUE : VLB_FALSE);
    entry->put("transport", peer.transport);
    entry->put("encryption", peer.encryption);
    auto sources = List::g();
    for (const auto& source : peer.sources) {
      sources->append(source);
    }
    entry->put("sources", std::move(sources));
    peers->append(std::move(entry));
  }
}
} // namespace

std::unique_ptr<ValueBase> GetPeersRpcMethod::process(const RpcRequest& req,
                                                      DownloadEngine* e)
{
  const String* gidParam = checkRequiredParam<String>(req, 0);

  a2_gid_t gid = str2Gid(gidParam);
  auto group = e->getRequestGroupMan()->findGroup(gid);
  if (!group) {
    throw DL_ABORT_EX(fmt("No peer data is available for GID#%s",
                          GroupId::toHex(gid).c_str()));
  }
  auto peers = List::g();
  if (group->getBtDownload()) {
    gatherPeer(peers.get(), group->getBtDownload()->snapshot());
  }
  return std::move(peers);
}

std::unique_ptr<ValueBase>
GetBtTrackersRpcMethod::process(const RpcRequest& req, DownloadEngine* e)
{
  const String* gidParam = checkRequiredParam<String>(req, 0);
  const auto gid = str2Gid(gidParam);
  const auto group = e->getRequestGroupMan()->findGroup(gid);
  if (!group || !group->getBtDownload()) {
    throw DL_ABORT_EX(fmt("No tracker data is available for GID#%s",
                          GroupId::toHex(gid).c_str()));
  }

  const auto& snapshot = group->getBtDownload()->snapshot();
  auto trackers = List::g();
  for (const auto& tracker : snapshot.trackers) {
    auto entry = Dict::g();
    entry->put("url", tracker.url);
    entry->put("source", tracker.source);
    entry->put("tier", util::itos(tracker.tier));
    entry->put("status", tracker.status);
    entry->put("failures", util::itos(tracker.failures));
    entry->put("seeders", util::itos(tracker.seeders));
    entry->put("leechers", util::itos(tracker.leechers));
    entry->put("downloads", util::itos(tracker.downloads));
    entry->put("nextAnnounce", util::itos(tracker.nextAnnounceSeconds));
    entry->put("minAnnounce", util::itos(tracker.minAnnounceSeconds));
    entry->put("updating", tracker.updating ? VLB_TRUE : VLB_FALSE);
    entry->put("verified", tracker.verified ? VLB_TRUE : VLB_FALSE);
    if (!tracker.message.empty()) {
      entry->put("message", tracker.message);
    }
    auto endpoints = List::g();
    for (const auto& endpoint : tracker.endpoints) {
      auto endpointEntry = Dict::g();
      endpointEntry->put("localEndpoint", endpoint.localEndpoint);
      endpointEntry->put("protocol", endpoint.protocol);
      endpointEntry->put("status", endpoint.status);
      endpointEntry->put("failures", util::itos(endpoint.failures));
      endpointEntry->put("seeders", util::itos(endpoint.seeders));
      endpointEntry->put("leechers", util::itos(endpoint.leechers));
      endpointEntry->put("downloads", util::itos(endpoint.downloads));
      endpointEntry->put("nextAnnounce",
                         util::itos(endpoint.nextAnnounceSeconds));
      endpointEntry->put("minAnnounce",
                         util::itos(endpoint.minAnnounceSeconds));
      endpointEntry->put("updating", endpoint.updating ? VLB_TRUE : VLB_FALSE);
      endpointEntry->put("verified", endpoint.verified ? VLB_TRUE : VLB_FALSE);
      if (!endpoint.message.empty()) {
        endpointEntry->put("message", endpoint.message);
      }
      endpoints->append(std::move(endpointEntry));
    }
    entry->put("endpoints", std::move(endpoints));
    trackers->append(std::move(entry));
  }
  return trackers;
}

std::unique_ptr<ValueBase>
GetBtSessionStatusRpcMethod::process(const RpcRequest& req, DownloadEngine* e)
{
  const auto status = e->getBtSession()->status();
  auto result = Dict::g();
  result->put("listenPort", util::uitos(status.listenPort));
  result->put("announcePort", util::uitos(status.announcePort));
  result->put("externalIp", status.externalAddress);
  result->put("mappedTcpPort", util::uitos(status.mappedTcpPort));
  result->put("mappedUdpPort", util::uitos(status.mappedUdpPort));
  result->put("dhtNodes", util::uitos(status.dhtNodes));
  result->put("dhtReplacementNodes", util::uitos(status.dhtReplacements));
  result->put("dhtActiveRequests", util::uitos(status.dhtActiveRequests));
  result->put("droppedAlerts", util::uitos(status.droppedAlerts));
  result->put("peerSockets", util::uitos(status.peerSockets));
  result->put("establishedPeers", util::uitos(status.establishedPeers));
  result->put("handshakingPeers", util::uitos(status.handshakingPeers));
  result->put("halfOpenPeers", util::uitos(status.halfOpenPeers));
  result->put("tcpPeers", util::uitos(status.tcpPeers));
  result->put("utpPeers", util::uitos(status.utpPeers));
  result->put("queuedTrackerAnnounces",
              util::uitos(status.queuedTrackerAnnounces));
  result->put("connectionAttempts", util::uitos(status.connectionAttempts));
  result->put("connectionTimeouts", util::uitos(status.connectionTimeouts));
  result->put("payloadDownloaded", util::uitos(status.payloadDownloaded));
  result->put("payloadUploaded", util::uitos(status.payloadUploaded));
  result->put("trackerDownloaded", util::uitos(status.trackerDownloaded));
  result->put("trackerUploaded", util::uitos(status.trackerUploaded));
  result->put("ipOverheadDownloaded", util::uitos(status.ipOverheadDownloaded));
  result->put("ipOverheadUploaded", util::uitos(status.ipOverheadUploaded));
  result->put("dhtDownloaded", util::uitos(status.dhtDownloaded));
  result->put("dhtUploaded", util::uitos(status.dhtUploaded));
  result->put("diskBlocksInUse", util::uitos(status.diskBlocksInUse));
  result->put("queuedDiskJobs", util::uitos(status.queuedDiskJobs));
  result->put("averageDiskJobTime", util::uitos(status.averageDiskJobTime));
  result->put("diskRequestLatency", util::uitos(status.diskRequestLatency));
  result->put("diskReadWaitingPeers", util::uitos(status.diskReadWaitingPeers));
  result->put("diskWriteWaitingPeers",
              util::uitos(status.diskWriteWaitingPeers));
  if (!status.lastPerformanceWarning.empty()) {
    result->put("lastPerformanceWarning", status.lastPerformanceWarning);
  }
  auto performanceWarnings = Dict::g();
  for (const auto& warning : status.performanceWarnings) {
    performanceWarnings->put(warning.first, util::uitos(warning.second));
  }
  result->put("performanceWarnings", std::move(performanceWarnings));
  result->put("dhtStateHealthy", status.dhtStateHealthy ? VLB_TRUE : VLB_FALSE);
  if (!status.portMappingError.empty()) {
    result->put("portMappingError", status.portMappingError);
  }
  auto endpoints = List::g();
  for (const auto& endpoint : status.listenEndpoints) {
    endpoints->append(endpoint);
  }
  result->put("listenEndpoints", std::move(endpoints));
  return result;
}

} // namespace aria2::rpc
