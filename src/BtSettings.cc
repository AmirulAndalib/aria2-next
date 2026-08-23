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

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/udp.hpp>

#include <libtorrent/alert.hpp>
#include <libtorrent/fingerprint.hpp>
#include <libtorrent/mmap_disk_io.hpp>
#include <libtorrent/posix_disk_io.hpp>
#include <libtorrent/pread_disk_io.hpp>
#include <libtorrent/session.hpp>
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

lt::download_priority_t filePriority(const std::string& value)
{
  if (value == "off") {
    return lt::dont_download;
  }
  if (value == "normal") {
    return lt::default_priority;
  }
  if (value == "high") {
    return lt::download_priority_t{6};
  }
  if (value == "top") {
    return lt::top_priority;
  }
  throw DL_ABORT_EX("Invalid BitTorrent file priority: " + value);
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

std::string routeAddress(const boost::asio::ip::address& destination)
{
  boost::asio::io_context context;
  boost::asio::ip::udp::socket socket(context);
  boost::system::error_code error;
  socket.open(destination.is_v4() ? boost::asio::ip::udp::v4()
                                  : boost::asio::ip::udp::v6(),
              error);
  if (error) {
    return {};
  }
  socket.connect({destination, 9}, error);
  if (error) {
    return {};
  }
  const auto local = socket.local_endpoint(error);
  if (error || local.address().is_unspecified()) {
    return {};
  }
  return local.address().to_string();
}

std::string makeListenInterfaces(
    const Option* option, const std::vector<std::string>& routeAddresses)
{
  const auto port = option->getAsInt(PREF_LISTEN_PORT);
  auto interfaces = splitInterfaces(option);
  if (interfaces.empty()) {
    interfaces = routeAddresses;
  }
  if (!interfaces.empty()) {
    std::string result;
    for (const auto& interface : interfaces) {
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

  return {};
}

} // namespace

std::vector<std::string> detectBtRouteAddresses(const Option* option)
{
  if (!splitInterfaces(option).empty() || !option->get(PREF_BT_PROXY).empty()) {
    return {};
  }

  std::vector<std::string> result;
  const auto ipv4 = routeAddress(boost::asio::ip::make_address("192.0.2.1"));
  if (!ipv4.empty()) {
    result.push_back(ipv4);
  }
  if (!option->getAsBool(PREF_DISABLE_IPV6)) {
    const auto ipv6 =
        routeAddress(boost::asio::ip::make_address("2001:db8::1"));
    if (!ipv6.empty() &&
        std::find(result.begin(), result.end(), ipv6) == result.end()) {
      result.push_back(ipv6);
    }
  }
  return result;
}

BtConfig makeBtConfig(const Option* option)
{
  return makeBtConfig(option, detectBtRouteAddresses(option));
}

BtConfig makeBtConfig(const Option* option,
                      const std::vector<std::string>& routeAddresses)
{
  BtConfig config;
  lt::settings_pack settings;
  const auto configuredInterfaces = splitInterfaces(option);
  config.automaticRoute =
      configuredInterfaces.empty() && option->get(PREF_BT_PROXY).empty();
  if (config.automaticRoute) {
    config.routeAddresses = routeAddresses;
  }
  else if (configuredInterfaces.empty()) {
    config.routeAddresses.push_back("0.0.0.0");
    if (!option->getAsBool(PREF_DISABLE_IPV6)) {
      config.routeAddresses.push_back("::");
    }
  }
  config.listenInterfaces =
      makeListenInterfaces(option, config.routeAddresses);
  settings.set_str(lt::settings_pack::listen_interfaces,
                   config.listenInterfaces);
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
                   option->get(PREF_BT_MIXED_MODE) == "peer-proportional"
                       ? lt::settings_pack::peer_proportional
                       : lt::settings_pack::prefer_tcp);
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
  settings.set_int(lt::settings_pack::connection_speed,
                   option->getAsInt(PREF_BT_CONNECTION_SPEED));
  settings.set_int(lt::settings_pack::max_out_request_queue,
                   option->getAsInt(PREF_BT_MAX_OUT_REQUEST_QUEUE));
  settings.set_int(lt::settings_pack::max_allowed_in_request_queue,
                   option->getAsInt(PREF_BT_MAX_IN_REQUEST_QUEUE));
  settings.set_int(lt::settings_pack::max_queued_disk_bytes,
                   option->getAsInt(PREF_BT_DISK_QUEUE_SIZE));
  settings.set_int(
      lt::settings_pack::checking_mem_usage,
      static_cast<int>(std::max<int64_t>(
          1, (option->getAsInt(PREF_BT_CHECKING_MEMORY) + 16_k - 1) / 16_k)));
  settings.set_bool(lt::settings_pack::piece_extent_affinity,
                    option->getAsBool(PREF_BT_PIECE_EXTENT_AFFINITY));
  settings.set_int(lt::settings_pack::peer_turnover,
                   option->getAsInt(PREF_BT_PEER_TURNOVER));
  settings.set_int(lt::settings_pack::peer_turnover_cutoff,
                   option->getAsInt(PREF_BT_PEER_TURNOVER_CUTOFF));
  settings.set_int(lt::settings_pack::peer_turnover_interval,
                   option->getAsInt(PREF_BT_PEER_TURNOVER_INTERVAL));
  settings.set_int(
      lt::settings_pack::choking_algorithm,
      option->get(PREF_BT_UPLOAD_SLOT_ALGORITHM) == "rate-based"
          ? lt::settings_pack::rate_based_choker
          : lt::settings_pack::fixed_slots_choker);
  const auto& seedChoking = option->get(PREF_BT_SEED_CHOKING_ALGORITHM);
  settings.set_int(
      lt::settings_pack::seed_choking_algorithm,
      seedChoking == "round-robin"
          ? lt::settings_pack::round_robin
          : seedChoking == "anti-leech" ? lt::settings_pack::anti_leech
                                         : lt::settings_pack::fastest_upload);
  settings.set_int(lt::settings_pack::send_buffer_low_watermark,
                   option->getAsInt(PREF_BT_SEND_BUFFER_LOW_WATERMARK));
  settings.set_int(lt::settings_pack::send_buffer_watermark,
                   option->getAsInt(PREF_BT_SEND_BUFFER_WATERMARK));
  settings.set_int(lt::settings_pack::send_buffer_watermark_factor,
                   option->getAsInt(PREF_BT_SEND_BUFFER_WATERMARK_FACTOR));
  settings.set_bool(
      lt::settings_pack::seeding_outgoing_connections,
      option->getAsBool(PREF_BT_SEEDING_OUTGOING_CONNECTIONS));
  settings.set_bool(lt::settings_pack::rate_limit_ip_overhead,
                    option->getAsBool(PREF_BT_RATE_LIMIT_OVERHEAD));
  settings.set_int(lt::settings_pack::stop_tracker_timeout,
                   option->getAsInt(PREF_BT_STOP_TRACKER_TIMEOUT));
  settings.set_int(
      lt::settings_pack::suggest_mode,
      option->getAsBool(PREF_BT_UPLOAD_SUGGESTIONS)
          ? lt::settings_pack::suggest_read_cache
          : lt::settings_pack::no_piece_suggestions);
  const auto& readCache = option->get(PREF_BT_DISK_READ_CACHE);
  settings.set_int(lt::settings_pack::disk_io_read_mode,
                   readCache == "disabled" ? lt::settings_pack::disable_os_cache
                                             : lt::settings_pack::enable_os_cache);
  const auto& writeCache = option->get(PREF_BT_DISK_WRITE_CACHE);
  settings.set_int(
      lt::settings_pack::disk_io_write_mode,
      writeCache == "disabled"
          ? lt::settings_pack::disable_os_cache
          : writeCache == "write-through" ? lt::settings_pack::write_through
                                            : lt::settings_pack::enable_os_cache);
  const auto& blocklistScope = option->get(PREF_BT_BLOCKLIST_SCOPE);
  settings.set_bool(lt::settings_pack::apply_ip_filter_to_trackers,
                    blocklistScope != "peers");
  settings.set_bool(lt::settings_pack::apply_filter_to_dht,
                    blocklistScope == "all");
  settings.set_int(lt::settings_pack::alert_queue_size, 16384);
  settings.set_int(
      lt::settings_pack::alert_mask,
      static_cast<int>(static_cast<unsigned int>(
          lt::alert_category::error | lt::alert_category::status |
          lt::alert_category::storage | lt::alert_category::port_mapping |
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
  const auto encryptionPolicy =
      encryption == V_REQUIRED   ? lt::settings_pack::pe_forced
      : encryption == V_DISABLED ? lt::settings_pack::pe_disabled
                                 : lt::settings_pack::pe_enabled;
  settings.set_int(lt::settings_pack::out_enc_policy, encryptionPolicy);
  settings.set_int(lt::settings_pack::in_enc_policy, encryptionPolicy);
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

void configureBtDiskIo(lt::session_params& params, const Option* option)
{
  const auto& diskIo = option->get(PREF_BT_DISK_IO);
  if (diskIo == "pread") {
    params.disk_io_constructor = lt::pread_disk_io_constructor;
  }
  else if (diskIo == "mmap") {
    params.disk_io_constructor = lt::mmap_disk_io_constructor;
  }
  else if (diskIo == "posix") {
    params.disk_io_constructor = lt::posix_disk_io_constructor;
  }
}

void applyBtFilePrioritySpec(
    std::vector<lt::download_priority_t>& priorities,
    const std::string& specification)
{
  if (specification.empty()) {
    return;
  }
  std::vector<std::string> entries;
  util::split(specification.begin(), specification.end(),
              std::back_inserter(entries), ',', true);
  for (auto entry : entries) {
    entry = util::strip(entry);
    const auto separator = entry.find('=');
    int32_t index = 0;
    if (separator == std::string::npos ||
        !util::parseIntNoThrow(index, entry.substr(0, separator)) || index < 1 ||
        static_cast<size_t>(index) > priorities.size()) {
      throw DL_ABORT_EX("Invalid BitTorrent file priority entry: " + entry);
    }
    const auto value = util::strip(entry.substr(separator + 1));
    priorities[static_cast<size_t>(index - 1)] = filePriority(value);
  }
}

} // namespace aria2
