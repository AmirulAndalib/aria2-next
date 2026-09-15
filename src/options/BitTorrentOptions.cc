/* <!-- copyright */
/*
 * aria2 - The high speed download utility
 *
 * Copyright (C) 2006 Tatsuhiro Tsujikawa
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
#include "OptionHandler.h"
#include "options/OptionCatalog.h"
#include <cstdint>
#include "OptionDefinitions.h"

#include "ApplicationStatePath.h"
#include "OptionHandlerImpl.h"
#include "prefs.h"
#include "usage_text.h"
#include "a2functional.h"
#include "help_tags.h"
#include "File.h"
#include "Log.h"
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>

namespace aria2::option {

void addBitTorrentOptions(OptionHandlers& handlers)
{
// BitTorrent Specific Options
#ifdef ENABLE_BITTORRENT
  {
    std::unique_ptr<OptionHandler> op(
        new BooleanOptionHandler(PREF_BT_ENABLE_LPD, TEXT_BT_ENABLE_LPD,
                                 A2_V_TRUE, OptionHandler::OPT_ARG));
    op->addTag(TAG_BITTORRENT);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new DefaultOptionHandler(
        PREF_BT_EXCLUDE_TRACKER, TEXT_BT_EXCLUDE_TRACKER, NO_DESCRIPTION,
        "URI,... "
        "or *"));
    op->addTag(TAG_BITTORRENT);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(
        new DefaultOptionHandler(PREF_BT_EXTERNAL_IP, TEXT_BT_EXTERNAL_IP,
                                 NO_DEFAULT_VALUE, "a numeric IP address"));
    op->addTag(TAG_BITTORRENT);
    op->setChangeGlobalOption(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new NumberOptionHandler(
        PREF_BT_EXTERNAL_PORT, TEXT_BT_EXTERNAL_PORT, "0", 0, UINT16_MAX));
    op->addTag(TAG_BITTORRENT);
    op->setChangeGlobalOption(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new ParameterOptionHandler(
        PREF_BT_ENCRYPTION, TEXT_BT_ENCRYPTION, V_PREFERRED,
        {V_PREFERRED, V_REQUIRED, V_DISABLED}));
    op->addTag(TAG_BITTORRENT);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new ParameterOptionHandler(
        PREF_BT_TRANSPORT, TEXT_BT_TRANSPORT, V_BOTH, {V_TCP, V_UTP, V_BOTH}));
    op->addTag(TAG_BITTORRENT);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new DefaultOptionHandler(
        PREF_BT_DHT_BOOTSTRAP_NODES, TEXT_BT_DHT_BOOTSTRAP_NODES,
        "dht.libtorrent.org:25401,dht.transmissionbt.com:6881,"
        "router.bt.ouinet.work:6881",
        "HOST:PORT,..."));
    op->addTag(TAG_ADVANCED);
    op->addTag(TAG_BITTORRENT);
    op->setChangeGlobalOption(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new NumberOptionHandler(
        PREF_BT_MAX_CONNECTIONS, TEXT_BT_MAX_CONNECTIONS, "500", 2));
    op->addTag(TAG_ADVANCED);
    op->addTag(TAG_BITTORRENT);
    op->setChangeGlobalOption(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new NumberOptionHandler(
        PREF_BT_MAX_UPLOADS, TEXT_BT_MAX_UPLOADS, "20", 1));
    op->addTag(TAG_ADVANCED);
    op->addTag(TAG_BITTORRENT);
    op->setChangeGlobalOption(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(
        new DefaultOptionHandler(PREF_BT_PROXY, TEXT_BT_PROXY, NO_DEFAULT_VALUE,
                                 "http://, socks4://, or socks5:// URI"));
    op->addTag(TAG_ADVANCED);
    op->addTag(TAG_BITTORRENT);
    op->setChangeGlobalOption(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(
        new BooleanOptionHandler(PREF_BT_PORT_MAPPING, TEXT_BT_PORT_MAPPING,
                                 A2_V_TRUE, OptionHandler::OPT_ARG));
    op->addTag(TAG_ADVANCED);
    op->addTag(TAG_BITTORRENT);
    op->setChangeGlobalOption(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new NumberOptionHandler(
        PREF_BT_MAX_OPEN_FILES, TEXT_BT_MAX_OPEN_FILES, "100", 1));
    op->addTag(TAG_BITTORRENT);
    op->setChangeGlobalOption(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new NumberOptionHandler(
        PREF_BT_IO_THREADS, TEXT_BT_IO_THREADS, "10", 1, 1024));
    op->addTag(TAG_ADVANCED);
    op->addTag(TAG_BITTORRENT);
    op->setChangeGlobalOption(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new NumberOptionHandler(
        PREF_BT_HASHING_THREADS, TEXT_BT_HASHING_THREADS, "1", 1, 1024));
    op->addTag(TAG_ADVANCED);
    op->addTag(TAG_BITTORRENT);
    op->setChangeGlobalOption(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new NumberOptionHandler(
        PREF_BT_CONNECTION_SPEED, TEXT_BT_CONNECTION_SPEED, "30", 0, 10000));
    op->addTag(TAG_ADVANCED);
    op->addTag(TAG_BITTORRENT);
    op->setChangeGlobalOption(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new NumberOptionHandler(
        PREF_BT_MAX_OUT_REQUEST_QUEUE, TEXT_BT_MAX_OUT_REQUEST_QUEUE, "128", 1,
        UINT16_MAX));
    op->addTag(TAG_ADVANCED);
    op->addTag(TAG_BITTORRENT);
    op->setChangeGlobalOption(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new NumberOptionHandler(
        PREF_BT_MAX_IN_REQUEST_QUEUE, TEXT_BT_MAX_IN_REQUEST_QUEUE, "2000", 1,
        INT32_MAX));
    op->addTag(TAG_ADVANCED);
    op->addTag(TAG_BITTORRENT);
    op->setChangeGlobalOption(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new UnitNumberOptionHandler(
        PREF_BT_DISK_QUEUE_SIZE, TEXT_BT_DISK_QUEUE_SIZE, "100M", 16_k,
        INT32_MAX));
    op->addTag(TAG_ADVANCED);
    op->addTag(TAG_BITTORRENT);
    op->setChangeGlobalOption(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(
        new ParameterOptionHandler(PREF_BT_DISK_IO, TEXT_BT_DISK_IO, "default",
                                   {"default", "pread", "mmap", "posix"}));
    op->addTag(TAG_ADVANCED);
    op->addTag(TAG_BITTORRENT);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new ParameterOptionHandler(
        PREF_BT_DISK_READ_CACHE, TEXT_BT_DISK_READ_CACHE, "enabled",
        {"enabled", "disabled"}));
    op->addTag(TAG_ADVANCED);
    op->addTag(TAG_BITTORRENT);
    op->setChangeGlobalOption(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new ParameterOptionHandler(
        PREF_BT_DISK_WRITE_CACHE, TEXT_BT_DISK_WRITE_CACHE, "enabled",
        {"enabled", "disabled", "write-through"}));
    op->addTag(TAG_ADVANCED);
    op->addTag(TAG_BITTORRENT);
    op->setChangeGlobalOption(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new UnitNumberOptionHandler(
        PREF_BT_CHECKING_MEMORY, TEXT_BT_CHECKING_MEMORY, "32M", 16_k,
        INT32_MAX));
    op->addTag(TAG_ADVANCED);
    op->addTag(TAG_BITTORRENT);
    op->setChangeGlobalOption(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new BooleanOptionHandler(
        PREF_BT_PIECE_EXTENT_AFFINITY, TEXT_BT_PIECE_EXTENT_AFFINITY,
        A2_V_FALSE, OptionHandler::OPT_ARG));
    op->addTag(TAG_ADVANCED);
    op->addTag(TAG_BITTORRENT);
    op->setChangeGlobalOption(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new NumberOptionHandler(
        PREF_BT_PEER_TURNOVER, TEXT_BT_PEER_TURNOVER, "4", 0, 100));
    op->addTag(TAG_ADVANCED);
    op->addTag(TAG_BITTORRENT);
    op->setChangeGlobalOption(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(
        new NumberOptionHandler(PREF_BT_PEER_TURNOVER_CUTOFF,
                                TEXT_BT_PEER_TURNOVER_CUTOFF, "90", 0, 100));
    op->addTag(TAG_ADVANCED);
    op->addTag(TAG_BITTORRENT);
    op->setChangeGlobalOption(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new NumberOptionHandler(
        PREF_BT_PEER_TURNOVER_INTERVAL, TEXT_BT_PEER_TURNOVER_INTERVAL, "300",
        30, 3600));
    op->addTag(TAG_ADVANCED);
    op->addTag(TAG_BITTORRENT);
    op->setChangeGlobalOption(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new ParameterOptionHandler(
        PREF_BT_MIXED_MODE, TEXT_BT_MIXED_MODE, "prefer-tcp",
        {"prefer-tcp", "peer-proportional"}));
    op->addTag(TAG_ADVANCED);
    op->addTag(TAG_BITTORRENT);
    op->setChangeGlobalOption(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new ParameterOptionHandler(
        PREF_BT_UPLOAD_SLOT_ALGORITHM, TEXT_BT_UPLOAD_SLOT_ALGORITHM, "fixed",
        {"fixed", "rate-based"}));
    op->addTag(TAG_ADVANCED);
    op->addTag(TAG_BITTORRENT);
    op->setChangeGlobalOption(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new ParameterOptionHandler(
        PREF_BT_SEED_CHOKING_ALGORITHM, TEXT_BT_SEED_CHOKING_ALGORITHM,
        "fastest-upload", {"round-robin", "fastest-upload", "anti-leech"}));
    op->addTag(TAG_ADVANCED);
    op->addTag(TAG_BITTORRENT);
    op->setChangeGlobalOption(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new UnitNumberOptionHandler(
        PREF_BT_SEND_BUFFER_LOW_WATERMARK, TEXT_BT_SEND_BUFFER_LOW_WATERMARK,
        "10K", 1_k, INT32_MAX));
    op->addTag(TAG_ADVANCED);
    op->addTag(TAG_BITTORRENT);
    op->setChangeGlobalOption(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new UnitNumberOptionHandler(
        PREF_BT_SEND_BUFFER_WATERMARK, TEXT_BT_SEND_BUFFER_WATERMARK, "500K",
        16_k, INT32_MAX));
    op->addTag(TAG_ADVANCED);
    op->addTag(TAG_BITTORRENT);
    op->setChangeGlobalOption(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new NumberOptionHandler(
        PREF_BT_SEND_BUFFER_WATERMARK_FACTOR,
        TEXT_BT_SEND_BUFFER_WATERMARK_FACTOR, "50", 1, 1000));
    op->addTag(TAG_ADVANCED);
    op->addTag(TAG_BITTORRENT);
    op->setChangeGlobalOption(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(
        new BooleanOptionHandler(PREF_BT_SEEDING_OUTGOING_CONNECTIONS,
                                 TEXT_BT_SEEDING_OUTGOING_CONNECTIONS,
                                 A2_V_TRUE, OptionHandler::OPT_ARG));
    op->addTag(TAG_ADVANCED);
    op->addTag(TAG_BITTORRENT);
    op->setChangeGlobalOption(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new BooleanOptionHandler(
        PREF_BT_RATE_LIMIT_OVERHEAD, TEXT_BT_RATE_LIMIT_OVERHEAD, A2_V_FALSE,
        OptionHandler::OPT_ARG));
    op->addTag(TAG_ADVANCED);
    op->addTag(TAG_BITTORRENT);
    op->setChangeGlobalOption(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(
        new NumberOptionHandler(PREF_BT_STOP_TRACKER_TIMEOUT,
                                TEXT_BT_STOP_TRACKER_TIMEOUT, "2", 0, 600));
    op->addTag(TAG_ADVANCED);
    op->addTag(TAG_BITTORRENT);
    op->setChangeGlobalOption(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new ParameterOptionHandler(
        PREF_BT_BLOCKLIST_SCOPE, TEXT_BT_BLOCKLIST_SCOPE, "peers",
        {"peers", "peers-and-trackers", "all"}));
    op->addTag(TAG_ADVANCED);
    op->addTag(TAG_BITTORRENT);
    op->setChangeGlobalOption(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new NumberOptionHandler(
        PREF_BT_RESUME_SAVE_INTERVAL, TEXT_BT_RESUME_SAVE_INTERVAL, "60", 0,
        INT32_MAX));
    op->addTag(TAG_ADVANCED);
    op->addTag(TAG_BITTORRENT);
    op->setChangeGlobalOption(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new BooleanOptionHandler(
        PREF_BT_UPLOAD_SUGGESTIONS, TEXT_BT_UPLOAD_SUGGESTIONS, A2_V_FALSE,
        OptionHandler::OPT_ARG));
    op->addTag(TAG_ADVANCED);
    op->addTag(TAG_BITTORRENT);
    op->setChangeGlobalOption(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new DefaultOptionHandler(
        PREF_BT_FILE_PRIORITY, TEXT_BT_FILE_PRIORITY, NO_DEFAULT_VALUE,
        "INDEX=off|normal|high|top,..."));
    op->addTag(TAG_ADVANCED);
    op->addTag(TAG_BITTORRENT);
    op->setInitialOption(true);
    op->setChangeOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new NumberOptionHandler(
        PREF_BT_MAX_CONCURRENT_HTTP_ANNOUNCES,
        TEXT_BT_MAX_CONCURRENT_HTTP_ANNOUNCES, "50", 1));
    op->addTag(TAG_ADVANCED);
    op->addTag(TAG_BITTORRENT);
    op->setChangeGlobalOption(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new BooleanOptionHandler(
        PREF_BT_ANNOUNCE_ALL_TIERS, TEXT_BT_ANNOUNCE_ALL_TIERS, A2_V_TRUE,
        OptionHandler::OPT_ARG));
    op->addTag(TAG_ADVANCED);
    op->addTag(TAG_BITTORRENT);
    op->setChangeGlobalOption(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new BooleanOptionHandler(
        PREF_BT_ANNOUNCE_ALL_TRACKERS, TEXT_BT_ANNOUNCE_ALL_TRACKERS,
        A2_V_FALSE, OptionHandler::OPT_ARG));
    op->addTag(TAG_ADVANCED);
    op->addTag(TAG_BITTORRENT);
    op->setChangeGlobalOption(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new DefaultOptionHandler(
        PREF_BT_USER_AGENT, TEXT_BT_USER_AGENT, "qBittorrent/5.2.3"));
    op->addTag(TAG_ADVANCED);
    op->addTag(TAG_BITTORRENT);
    op->setChangeGlobalOption(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new DefaultOptionHandler(
        PREF_BT_PEER_ID_PREFIX, TEXT_BT_PEER_ID_PREFIX, "-qB5230-"));
    op->addTag(TAG_ADVANCED);
    op->addTag(TAG_BITTORRENT);
    op->setChangeGlobalOption(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(
        new BooleanOptionHandler(PREF_BT_ANONYMOUS_MODE, TEXT_BT_ANONYMOUS_MODE,
                                 A2_V_FALSE, OptionHandler::OPT_ARG));
    op->addTag(TAG_ADVANCED);
    op->addTag(TAG_BITTORRENT);
    op->setChangeGlobalOption(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new NumberOptionHandler(
        PREF_BT_MAX_PEERS, TEXT_BT_MAX_PEERS, "100", 0));
    op->addTag(TAG_BITTORRENT);
    op->setInitialOption(true);
    op->setChangeOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(
        new NumberOptionHandler(PREF_BT_MAX_UPLOADS_PER_TORRENT,
                                TEXT_BT_MAX_UPLOADS_PER_TORRENT, "4", 1));
    op->addTag(TAG_BITTORRENT);
    op->setInitialOption(true);
    op->setChangeOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new BooleanOptionHandler(
        PREF_BT_FIRST_LAST_PIECE_FIRST, TEXT_BT_FIRST_LAST_PIECE_FIRST,
        A2_V_FALSE, OptionHandler::OPT_ARG));
    op->addTag(TAG_BITTORRENT);
    op->setInitialOption(true);
    op->setChangeOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(
        new BooleanOptionHandler(PREF_BT_SUPER_SEEDING, TEXT_BT_SUPER_SEEDING,
                                 A2_V_FALSE, OptionHandler::OPT_ARG));
    op->addTag(TAG_BITTORRENT);
    op->setInitialOption(true);
    op->setChangeOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new LocalFilePathOptionHandler(
        PREF_BT_PEER_BLOCKLIST, TEXT_BT_PEER_BLOCKLIST, NO_DEFAULT_VALUE,
        /* acceptStdin = */ false, 0, /* mustExist = */ false));
    op->addTag(TAG_ADVANCED);
    op->addTag(TAG_BITTORRENT);
    op->setChangeGlobalOption(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new BooleanOptionHandler(
        PREF_BT_SEED_UNVERIFIED, TEXT_BT_SEED_UNVERIFIED, A2_V_FALSE,
        OptionHandler::OPT_ARG));
    op->addTag(TAG_BITTORRENT);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new DefaultOptionHandler(
        PREF_BT_TRACKER, TEXT_BT_TRACKER, NO_DESCRIPTION, "URI,..."));
    op->addTag(TAG_BITTORRENT);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new NumberOptionHandler(
        PREF_BT_TRACKER_COMPLETION_TIMEOUT, TEXT_BT_TRACKER_COMPLETION_TIMEOUT,
        "10", 1, 600));
    op->addTag(TAG_BITTORRENT);
    op->setChangeGlobalOption(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(
        new NumberOptionHandler(PREF_BT_TRACKER_RECEIVE_TIMEOUT,
                                TEXT_BT_TRACKER_RECEIVE_TIMEOUT, "10", 1, 600));
    op->addTag(TAG_BITTORRENT);
    op->setChangeGlobalOption(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(
        new DefaultOptionHandler(PREF_BT_INTERFACE, TEXT_BT_INTERFACE,
                                 NO_DEFAULT_VALUE, "INTERFACE,..."));
    op->addTag(TAG_ADVANCED);
    op->addTag(TAG_BITTORRENT);
    op->setChangeGlobalOption(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new BooleanOptionHandler(
        PREF_ENABLE_DHT, TEXT_ENABLE_DHT, A2_V_TRUE, OptionHandler::OPT_ARG));
    op->addTag(TAG_BASIC);
    op->addTag(TAG_BITTORRENT);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new BooleanOptionHandler(
        PREF_ENABLE_PEER_EXCHANGE, TEXT_ENABLE_PEER_EXCHANGE, A2_V_TRUE,
        OptionHandler::OPT_ARG));
    op->addTag(TAG_BITTORRENT);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(
        new ParameterOptionHandler(PREF_FOLLOW_TORRENT, TEXT_FOLLOW_TORRENT,
                                   A2_V_TRUE, {A2_V_TRUE, V_MEM, A2_V_FALSE}));
    op->addTag(TAG_BITTORRENT);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(
        new IndexOutOptionHandler(PREF_INDEX_OUT, TEXT_INDEX_OUT, 'O'));
    op->addTag(TAG_BITTORRENT);
    op->setInitialOption(true);
    op->setCumulative(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new NumberOptionHandler(
        PREF_LISTEN_PORT, TEXT_LISTEN_PORT, "6881", 1024, UINT16_MAX));
    op->addTag(TAG_BASIC);
    op->addTag(TAG_BITTORRENT);
    op->setChangeGlobalOption(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new UnitNumberOptionHandler(
        PREF_MAX_OVERALL_UPLOAD_LIMIT, TEXT_MAX_OVERALL_UPLOAD_LIMIT, "0", 0));
    op->addTag(TAG_BASIC);
    op->addTag(TAG_BITTORRENT);
    op->setChangeGlobalOption(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new UnitNumberOptionHandler(
        PREF_MAX_UPLOAD_LIMIT, TEXT_MAX_UPLOAD_LIMIT, "0", 0, -1, 'u'));
    op->addTag(TAG_BASIC);
    op->addTag(TAG_BITTORRENT);
    op->setInitialOption(true);
    op->setChangeOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new LocalFilePathOptionHandler(
        PREF_ON_BT_DOWNLOAD_COMPLETE, TEXT_ON_BT_DOWNLOAD_COMPLETE,
        NO_DEFAULT_VALUE,
        /* acceptStdin = */ false, 0, /* mustExist = */ false,
        PATH_TO_COMMAND));
    op->addTag(TAG_ADVANCED);
    op->addTag(TAG_HOOK);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new LocalFilePathOptionHandler(
        PREF_TORRENT_FILE, TEXT_TORRENT_FILE, NO_DEFAULT_VALUE, false, 'T'));
    op->addTag(TAG_BASIC);
    op->addTag(TAG_BITTORRENT);
    handlers.push_back(std::move(op));
  }
#endif // ENABLE_BITTORRENT
  // Metalink Specific Options
}
} // namespace aria2::option
