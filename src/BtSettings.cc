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
#include "BtSettings.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <vector>

#include <libtorrent/alert.hpp>
#include <libtorrent/fingerprint.hpp>
#include <libtorrent/version.hpp>

#include "DlAbortEx.h"
#include "Option.h"
#include "SocketCore.h"
#include "prefs.h"
#include "uri.h"
#include "util.h"

namespace aria2 {

namespace lt = libtorrent;

namespace {

std::string lower(std::string value)
{
  std::transform(
      value.begin(), value.end(), value.begin(),
      [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

std::vector<std::string> splitInterfaces(const Option* option)
{
  std::vector<std::string> interfaces;
  const auto& value = option->get(PREF_BT_INTERFACE);
  util::split(value.begin(), value.end(), std::back_inserter(interfaces), ',',
              true);
  for (auto& interface : interfaces) {
    interface = util::strip(interface);
    if (interface.size() >= 2 && interface.front() == '[' &&
        interface.back() == ']') {
      interface = interface.substr(1, interface.size() - 2);
    }
  }
  interfaces.erase(
      std::remove_if(interfaces.begin(), interfaces.end(),
                     [](const std::string& value) { return value.empty(); }),
      interfaces.end());
  return interfaces;
}

std::string withListenPort(const std::string& interface, int port)
{
  if (!interface.empty() && interface.front() != '[' &&
      interface.find(':') != std::string::npos) {
    return '[' + interface + "]:" + std::to_string(port);
  }
  return interface + ':' + std::to_string(port);
}

std::string makeListenInterfaces(const Option* option)
{
  const auto port = option->getAsInt(PREF_LISTEN_PORT);
  const auto configured = splitInterfaces(option);
  if (!configured.empty()) {
    std::string result;
    for (const auto& interface : configured) {
      if (option->getAsBool(PREF_DISABLE_IPV6) &&
          interface.find(':') != std::string::npos) {
        continue;
      }
      if (!result.empty()) {
        result += ',';
      }
      result += withListenPort(interface, port);
    }
    if (result.empty()) {
      throw DL_ABORT_EX("No usable BitTorrent listen interface remains");
    }
    return result;
  }

  auto result = "0.0.0.0:" + std::to_string(port);
  if (!option->getAsBool(PREF_DISABLE_IPV6)) {
    result += ",[::]:" + std::to_string(port);
  }
  return result;
}

void configurePeerConnections(lt::settings_pack& settings)
{
  settings.set_bool(lt::settings_pack::smooth_connects, true);
  settings.set_int(lt::settings_pack::connection_speed, 30);
  settings.set_int(lt::settings_pack::torrent_connect_boost, 30);
  settings.set_int(lt::settings_pack::handshake_timeout, 3);
  settings.set_int(lt::settings_pack::min_reconnect_time, 1);
  settings.set_int(lt::settings_pack::max_failcount, 3);
  settings.set_int(lt::settings_pack::request_queue_time, 1);
  settings.set_int(lt::settings_pack::max_out_request_queue, 4096);
  settings.set_int(lt::settings_pack::whole_pieces_threshold, 2);
}

} // namespace

BtConfig makeBtConfig(const Option* option)
{
  BtConfig config;
  lt::settings_pack settings;
  config.listenInterfaces = makeListenInterfaces(option);
  settings.set_str(lt::settings_pack::listen_interfaces,
                   config.listenInterfaces);
  const auto configuredInterfaces = splitInterfaces(option);
  if (!configuredInterfaces.empty()) {
    for (const auto& interface : configuredInterfaces) {
      if (option->getAsBool(PREF_DISABLE_IPV6) &&
          interface.find(':') != std::string::npos) {
        continue;
      }
      if (!config.outgoingInterfaces.empty()) {
        config.outgoingInterfaces += ',';
      }
      config.outgoingInterfaces += interface;
    }
    settings.set_str(lt::settings_pack::outgoing_interfaces,
                     config.outgoingInterfaces);
  }
  settings.set_int(lt::settings_pack::max_retry_port_bind, 0);
  settings.set_bool(lt::settings_pack::listen_system_port_fallback, false);

  bool allowDht = option->getAsBool(PREF_ENABLE_DHT);
  bool allowLsd = option->getAsBool(PREF_BT_ENABLE_LPD);
  bool allowPortMapping = option->getAsBool(PREF_BT_PORT_MAPPING);
  const auto& proxy = option->get(PREF_BT_PROXY);
  if (proxy.empty()) {
    settings.set_int(lt::settings_pack::proxy_type, lt::settings_pack::none);
  }
  else {
    uri::UriStruct parsed;
    if (!uri::parse(parsed, proxy) || parsed.host.empty() || parsed.port == 0 ||
        parsed.dir != "/" || !parsed.file.empty() || !parsed.query.empty()) {
      throw DL_ABORT_EX("Invalid bt-proxy URI");
    }
    const auto protocol = lower(parsed.protocol);
    int proxyType = lt::settings_pack::none;
    if (protocol == "http") {
      proxyType = parsed.username.empty() ? lt::settings_pack::http
                                          : lt::settings_pack::http_pw;
      allowDht = false;
    }
    else if (protocol == "socks4") {
      proxyType = lt::settings_pack::socks4;
      allowDht = false;
    }
    else if (protocol == "socks5") {
      proxyType = parsed.username.empty() ? lt::settings_pack::socks5
                                          : lt::settings_pack::socks5_pw;
    }
    else {
      throw DL_ABORT_EX("bt-proxy must use http, socks4, or socks5");
    }
    allowLsd = false;
    allowPortMapping = false;
    settings.set_int(lt::settings_pack::proxy_type, proxyType);
    settings.set_str(lt::settings_pack::proxy_hostname, parsed.host);
    settings.set_int(lt::settings_pack::proxy_port, parsed.port);
    settings.set_str(lt::settings_pack::proxy_username, parsed.username);
    settings.set_str(lt::settings_pack::proxy_password, parsed.password);
    settings.set_bool(lt::settings_pack::proxy_hostnames, true);
    settings.set_bool(lt::settings_pack::proxy_peer_connections, true);
    settings.set_bool(lt::settings_pack::proxy_tracker_connections, true);
  }

  settings.set_bool(lt::settings_pack::enable_dht, allowDht);
  config.dhtEnabled = allowDht;
  settings.set_bool(lt::settings_pack::enable_lsd, allowLsd);
  settings.set_bool(lt::settings_pack::enable_upnp, allowPortMapping);
  settings.set_bool(lt::settings_pack::enable_natpmp, allowPortMapping);
  const auto& transport = option->get(PREF_BT_TRANSPORT);
  const bool enableTcp = transport != V_UTP;
  const bool enableUtp = transport != V_TCP;
  settings.set_bool(lt::settings_pack::enable_incoming_tcp, enableTcp);
  settings.set_bool(lt::settings_pack::enable_outgoing_tcp, enableTcp);
  settings.set_bool(lt::settings_pack::enable_incoming_utp, enableUtp);
  settings.set_bool(lt::settings_pack::enable_outgoing_utp, enableUtp);
  settings.set_int(lt::settings_pack::mixed_mode_algorithm,
                   lt::settings_pack::prefer_tcp);
  configurePeerConnections(settings);
  settings.set_str(lt::settings_pack::user_agent,
                   "aria2-next/" PACKAGE_VERSION " libtorrent/" +
                       std::to_string(LIBTORRENT_VERSION_MAJOR) + "." +
                       std::to_string(LIBTORRENT_VERSION_MINOR) + "." +
                       std::to_string(LIBTORRENT_VERSION_TINY));
  settings.set_str(lt::settings_pack::peer_fingerprint,
                   lt::generate_fingerprint("A2", PACKAGE_VERSION_MAJOR,
                                            PACKAGE_VERSION_MINOR,
                                            PACKAGE_VERSION_PATCH));
  settings.set_str(lt::settings_pack::dht_bootstrap_nodes,
                   option->get(PREF_BT_DHT_BOOTSTRAP_NODES));
  settings.set_bool(lt::settings_pack::announce_to_all_tiers,
                    option->getAsBool(PREF_BT_ANNOUNCE_ALL_TIERS));
  settings.set_bool(lt::settings_pack::announce_to_all_trackers,
                    option->getAsBool(PREF_BT_ANNOUNCE_ALL_TRACKERS));
  settings.set_int(lt::settings_pack::active_tracker_limit, -1);
  settings.set_int(lt::settings_pack::active_dht_limit, -1);
  settings.set_int(lt::settings_pack::active_lsd_limit, -1);
  settings.set_int(lt::settings_pack::max_concurrent_http_announces,
                   option->getAsInt(PREF_BT_MAX_CONCURRENT_HTTP_ANNOUNCES));
  settings.set_int(lt::settings_pack::connections_limit,
                   option->getAsInt(PREF_BT_MAX_CONNECTIONS));
  settings.set_int(lt::settings_pack::unchoke_slots_limit,
                   option->getAsInt(PREF_BT_MAX_UPLOADS));
  settings.set_int(lt::settings_pack::alert_queue_size, 16384);
  settings.set_int(
      lt::settings_pack::alert_mask,
      static_cast<int>(static_cast<unsigned int>(
          lt::alert_category::error | lt::alert_category::status |
          lt::alert_category::storage | lt::alert_category::tracker |
          lt::alert_category::peer | lt::alert_category::connect |
          lt::alert_category::port_mapping |
          lt::alert_category::performance_warning |
          lt::alert_category::ip_block)));
  settings.set_int(lt::settings_pack::upload_rate_limit,
                   option->getAsInt(PREF_MAX_OVERALL_UPLOAD_LIMIT));
  settings.set_int(lt::settings_pack::download_rate_limit,
                   option->getAsInt(PREF_MAX_OVERALL_DOWNLOAD_LIMIT));
  settings.set_int(lt::settings_pack::file_pool_size,
                   option->getAsInt(PREF_BT_MAX_OPEN_FILES));
  settings.set_int(lt::settings_pack::aio_threads,
                   option->getAsInt(PREF_BT_IO_THREADS));
  settings.set_int(lt::settings_pack::hashing_threads,
                   option->getAsInt(PREF_BT_HASHING_THREADS));
  settings.set_bool(lt::settings_pack::anonymous_mode,
                    option->getAsBool(PREF_BT_ANONYMOUS_MODE));
  settings.set_int(lt::settings_pack::peer_dscp, option->getAsInt(PREF_DSCP)
                                                     << 2);
  if (option->getAsInt(PREF_SOCKET_RECV_BUFFER_SIZE) > 0) {
    settings.set_int(lt::settings_pack::recv_socket_buffer_size,
                     option->getAsInt(PREF_SOCKET_RECV_BUFFER_SIZE));
  }
  if (!option->blank(PREF_BT_EXTERNAL_IP)) {
    std::array<unsigned char, 16> address{};
    if (net::getBinAddr(address.data(), option->get(PREF_BT_EXTERNAL_IP)) ==
        0) {
      throw DL_ABORT_EX("bt-external-ip must be a numeric IP address");
    }
    settings.set_str(lt::settings_pack::announce_ip,
                     option->get(PREF_BT_EXTERNAL_IP));
  }
  settings.set_int(lt::settings_pack::announce_port,
                   option->getAsInt(PREF_BT_EXTERNAL_PORT));
  config.trackerCompletionTimeout =
      option->getAsInt(PREF_BT_TRACKER_COMPLETION_TIMEOUT);
  config.trackerReceiveTimeout =
      option->getAsInt(PREF_BT_TRACKER_RECEIVE_TIMEOUT);
  settings.set_int(lt::settings_pack::tracker_completion_timeout,
                   config.trackerCompletionTimeout);
  settings.set_int(lt::settings_pack::tracker_receive_timeout,
                   config.trackerReceiveTimeout);
  const auto& encryption = option->get(PREF_BT_ENCRYPTION);
  const auto outgoingEncryption =
      encryption == V_DISABLED ? lt::settings_pack::pe_disabled
                               : lt::settings_pack::pe_forced;
  const auto incomingEncryption =
      encryption == V_REQUIRED   ? lt::settings_pack::pe_forced
      : encryption == V_DISABLED ? lt::settings_pack::pe_disabled
                                 : lt::settings_pack::pe_enabled;
  settings.set_int(lt::settings_pack::out_enc_policy, outgoingEncryption);
  settings.set_int(lt::settings_pack::in_enc_policy, incomingEncryption);
  settings.set_int(lt::settings_pack::allowed_enc_level,
                   lt::settings_pack::pe_both);
  settings.set_bool(lt::settings_pack::prefer_rc4, false);
  settings.set_bool(lt::settings_pack::validate_https_trackers, true);
  settings.set_bool(lt::settings_pack::ssrf_mitigation, true);
  config.networkIdentity =
      config.listenInterfaces + '\n' + config.outgoingInterfaces + '\n' +
      option->get(PREF_BT_PROXY) + '\n' + option->get(PREF_BT_EXTERNAL_IP) +
      '\n' + option->get(PREF_BT_EXTERNAL_PORT);
  config.settings = std::move(settings);
  return config;
}

} // namespace aria2
