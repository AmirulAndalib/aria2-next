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
#include "BtSession.h"
#include "Log.h"
#include "fmt.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/close_reason.hpp>
#include <libtorrent/operations.hpp>
#include <libtorrent/portmap.hpp>
#include <libtorrent/socket_type.hpp>
#include <string>
#include <utility>
#include "bittorrent/BtSessionInternal.h"

namespace aria2 {
using namespace bt_session;

void BtSession::handleAlert(lt::log_alert* native)
{
  A2_LOG_TRACE(fmt("component=bittorrent event=native_session message=%s",
                   logging::sanitizeText(native->log_message()).c_str()));
}

void BtSession::handleAlert(lt::torrent_log_alert* native)
{
  const auto download = findDownload(native->handle);
  A2_LOG_TRACE(fmt("component=bittorrent event=native_torrent gid=%s "
                   "message=%s",
                   gidFor(download).c_str(),
                   logging::sanitizeText(native->log_message()).c_str()));
}

void BtSession::handleAlert(lt::peer_error_alert* peer)
{
  const auto download = findDownload(peer->handle);
  A2_LOG_DEBUG(fmt("component=bittorrent event=peer_error gid=%s "
                   "operation=%s category=%s code=%d message=%s",
                   gidFor(download).c_str(), lt::operation_name(peer->op),
                   peer->error.category().name(), peer->error.value(),
                   logging::sanitizeText(peer->error.message()).c_str()));
}

void BtSession::handleAlert(lt::peer_connect_alert* peer)
{
  const auto download = findDownload(peer->handle);
  A2_LOG_TRACE(fmt(
      "component=bittorrent event=peer_connected gid=%s "
      "direction=%s message=%s",
      gidFor(download).c_str(),
      peer->direction == lt::peer_connect_alert::direction_t::in ? "in" : "out",
      logging::sanitizeText(peer->message()).c_str()));
}

void BtSession::handleAlert(lt::peer_disconnected_alert* peer)
{
  if (peer->reason == lt::close_reason_t::torrent_removed) {
    return;
  }
  const auto download = findDownload(peer->handle);
  A2_LOG_TRACE(fmt("component=bittorrent event=peer_disconnected gid=%s "
                   "operation=%s category=%s code=%d message=%s",
                   gidFor(download).c_str(), lt::operation_name(peer->op),
                   peer->error.category().name(), peer->error.value(),
                   logging::sanitizeText(peer->error.message()).c_str()));
}

void BtSession::handleAlert(lt::tracker_announce_alert* tracker)
{
  const auto download = findDownload(tracker->handle);
  A2_LOG_DEBUG(fmt("component=bittorrent event=tracker_announce gid=%s "
                   "family=%s event_code=%d url=%s",
                   gidFor(download).c_str(),
                   tracker->local_endpoint.address().is_v6() ? "ipv6" : "ipv4",
                   static_cast<int>(tracker->event),
                   logging::sanitizeUri(tracker->tracker_url()).c_str()));
}

void BtSession::handleAlert(lt::tracker_reply_alert* tracker)
{
  const auto download = findDownload(tracker->handle);
  A2_LOG_DEBUG(fmt("component=bittorrent event=tracker_reply gid=%s "
                   "family=%s peers=%d url=%s",
                   gidFor(download).c_str(),
                   tracker->local_endpoint.address().is_v6() ? "ipv6" : "ipv4",
                   tracker->num_peers,
                   logging::sanitizeUri(tracker->tracker_url()).c_str()));
}

void BtSession::handleAlert(lt::dht_bootstrap_alert*)
{
  A2_LOG_INFO("component=bittorrent event=dht_bootstrap_complete");
}

void BtSession::handleAlert(lt::tracker_error_alert* tracker)
{
  const auto download = findDownload(tracker->handle);
  if (tracker->times_in_row == 1 || tracker->times_in_row % 10 == 0) {
    A2_LOG_DEBUG(
        fmt("component=bittorrent event=tracker_error gid=%s family=%s "
            "url=%s operation=%s error=%s consecutive=%d",
            gidFor(download).c_str(),
            tracker->local_endpoint.address().is_v6() ? "ipv6" : "ipv4",
            logging::sanitizeUri(tracker->tracker_url()).c_str(),
            lt::operation_name(tracker->op),
            logging::sanitizeText(tracker->error.message()).c_str(),
            tracker->times_in_row));
  }
}

void BtSession::handleAlert(lt::tracker_warning_alert* tracker)
{
  const auto download = findDownload(tracker->handle);
  A2_LOG_DEBUG(fmt("component=bittorrent event=tracker_warning gid=%s "
                   "url=%s message=%s",
                   gidFor(download).c_str(),
                   logging::sanitizeUri(tracker->tracker_url()).c_str(),
                   logging::sanitizeText(tracker->warning_message()).c_str()));
}

void BtSession::handleAlert(lt::listen_succeeded_alert* listening)
{
  if (listening->socket_type == lt::socket_type_t::tcp ||
      listening->socket_type == lt::socket_type_t::utp) {
    impl_->listenPort = static_cast<uint16_t>(listening->port);
  }
  auto address = listening->address.to_string();
  if (listening->address.is_v6()) {
    address = '[' + address + ']';
  }
  auto endpoint = address + ':' + std::to_string(listening->port);
  if (std::find(impl_->listenEndpoints.begin(), impl_->listenEndpoints.end(),
                endpoint) == impl_->listenEndpoints.end()) {
    impl_->listenEndpoints.push_back(std::move(endpoint));
  }
}

void BtSession::handleAlert(lt::listen_failed_alert* failed)
{
  auto address = failed->address.to_string();
  if (failed->address.is_v6()) {
    address = '[' + address + ']';
  }
  const auto endpoint = address + ':' + std::to_string(failed->port);
  impl_->listenEndpoints.erase(std::remove(impl_->listenEndpoints.begin(),
                                           impl_->listenEndpoints.end(),
                                           endpoint),
                               impl_->listenEndpoints.end());
  A2_LOG_ERROR(failed->message());
}

void BtSession::handleAlert(lt::external_ip_alert* external)
{
  const auto address = external->external_address.to_string();
  auto& previous = external->external_address.is_v4()
                       ? impl_->externalAddressV4
                       : impl_->externalAddressV6;
  previous = address;
  impl_->externalAddress = address;
}

void BtSession::handleAlert(lt::portmap_alert* mapped)
{
  if (mapped->map_protocol == lt::portmap_protocol::tcp) {
    impl_->mappedTcpPort = static_cast<uint16_t>(mapped->external_port);
  }
  else if (mapped->map_protocol == lt::portmap_protocol::udp) {
    impl_->mappedUdpPort = static_cast<uint16_t>(mapped->external_port);
  }
  impl_->portMappingError.clear();
}

void BtSession::handleAlert(lt::portmap_error_alert* mapped)
{
  const auto error = mapped->error.message();
  if (impl_->portMappingError != error) {
    A2_LOG_WARN(fmt("BitTorrent port mapping failed: %s", error.c_str()));
    impl_->portMappingError = error;
  }
}

void BtSession::handleAlert(lt::dht_stats_alert* stats)
{
  impl_->dhtNodes = 0;
  impl_->dhtReplacements = 0;
  for (const auto& bucket : stats->routing_table) {
    impl_->dhtNodes += static_cast<size_t>(bucket.num_nodes);
    impl_->dhtReplacements += static_cast<size_t>(bucket.num_replacements);
  }
  impl_->dhtActiveRequests = stats->active_requests.size();
}

void BtSession::handleAlert(lt::socks5_alert* proxyError)
{
  A2_LOG_ERROR(proxyError->message());
}

void BtSession::handleAlert(lt::performance_alert* warning)
{
  const auto code = static_cast<size_t>(warning->warning_code);
  if (code < impl_->performanceWarnings.size()) {
    if (impl_->performanceWarnings[code]++ == 0) {
      A2_LOG_WARN(warning->message());
    }
  }
  impl_->lastPerformanceWarning = warning->message();
}

void BtSession::handleAlert(lt::ip_ban_alert* banned)
{
  A2_LOG_DEBUG(banned->message());
}

} // namespace aria2
