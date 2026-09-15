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
#include <limits>
#include "OptionDefinitions.h"

#include "ApplicationStatePath.h"
#include "OptionHandlerImpl.h"
#include "prefs.h"
#include "usage_text.h"
#include "support/Numbers.h"
#include "support/FilePath.h"
#include "a2functional.h"
#include "help_tags.h"
#include "File.h"
#include "Log.h"
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>

namespace aria2::option {

void addGeneralOptions(OptionHandlers& handlers)
{
  static const std::string logLevels[] = {V_TRACE, V_DEBUG, V_INFO, V_WARN,
                                          V_ERROR};
  // General Options
  {
    std::unique_ptr<OptionHandler> op(
        new BooleanOptionHandler(PREF_ALLOW_OVERWRITE, TEXT_ALLOW_OVERWRITE,
                                 A2_V_FALSE, OptionHandler::OPT_ARG));
    op->addTag(TAG_ADVANCED);
    op->addTag(TAG_FILE);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new BooleanOptionHandler(
        PREF_AUTO_FILE_RENAMING, TEXT_AUTO_FILE_RENAMING, A2_V_TRUE,
        OptionHandler::OPT_ARG));
    op->addTag(TAG_ADVANCED);
    op->addTag(TAG_FILE);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new NumberOptionHandler(
        PREF_STATE_SAVE_INTERVAL, TEXT_STATE_SAVE_INTERVAL, "60", 0, 600));
    op->addTag(TAG_ADVANCED);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(
        new BooleanOptionHandler(PREF_CHECK_INTEGRITY, TEXT_CHECK_INTEGRITY,
                                 A2_V_FALSE, OptionHandler::OPT_ARG, 'V'));
    op->addTag(TAG_BASIC);
    op->addTag(TAG_BITTORRENT);
    op->addTag(TAG_METALINK);
    op->addTag(TAG_FILE);
    op->addTag(TAG_CHECKSUM);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new DefaultOptionHandler(
        PREF_CONF_PATH, TEXT_CONF_PATH, util::getConfigFile(), PATH_TO_FILE));
    op->addTag(TAG_ADVANCED);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new BooleanOptionHandler(
        PREF_CONTINUE, TEXT_CONTINUE, A2_V_FALSE, OptionHandler::OPT_ARG, 'c'));
    op->addTag(TAG_BASIC);
    op->addTag(TAG_HTTP);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new BooleanOptionHandler(
        PREF_DAEMON, TEXT_DAEMON, A2_V_FALSE, OptionHandler::OPT_ARG, 'D'));
    op->addTag(TAG_ADVANCED);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(
        new UnitNumberOptionHandler(PREF_DISK_CACHE, TEXT_DISK_CACHE,
#ifdef DEFAULT_DISK_CACHE
                                    DEFAULT_DISK_CACHE,
#else
                                    "16M",
#endif
                                    0));
    op->addTag(TAG_ADVANCED);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new LocalFilePathOptionHandler(
        PREF_STATE_DIR, TEXT_STATE_DIR, state::defaultDirectory(), false, 0,
        false, PATH_TO_DIR));
    op->addTag(TAG_ADVANCED);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(
        new DefaultOptionHandler(PREF_ED2K_SERVER, TEXT_ED2K_SERVER));
    op->addTag(TAG_ADVANCED);
    op->addTag(TAG_ED2K);
    op->setInitialOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new LocalFilePathOptionHandler(
        PREF_ED2K_SERVER_LIST, TEXT_ED2K_SERVER_LIST, NO_DEFAULT_VALUE));
    op->addTag(TAG_ADVANCED);
    op->addTag(TAG_ED2K);
    op->setInitialOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new LocalFilePathOptionHandler(
        PREF_ED2K_NODE_LIST, TEXT_ED2K_NODE_LIST, NO_DEFAULT_VALUE));
    op->addTag(TAG_ADVANCED);
    op->addTag(TAG_ED2K);
    op->setInitialOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new NumberOptionHandler(
        PREF_ED2K_LISTEN_PORT, TEXT_ED2K_LISTEN_PORT, "4662", 0, UINT16_MAX));
    op->addTag(TAG_ADVANCED);
    op->addTag(TAG_ED2K);
    op->setInitialOption(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new NumberOptionHandler(
        PREF_ED2K_UDP_LISTEN_PORT, TEXT_ED2K_UDP_LISTEN_PORT, "4672", 0,
        UINT16_MAX));
    op->addTag(TAG_ADVANCED);
    op->addTag(TAG_ED2K);
    op->setInitialOption(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new NumberOptionHandler(
        PREF_ED2K_UPLOAD_SLOTS, TEXT_ED2K_UPLOAD_SLOTS, "3", 1));
    op->addTag(TAG_ADVANCED);
    op->addTag(TAG_ED2K);
    op->setInitialOption(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new NumberOptionHandler(
        PREF_ED2K_MAX_CONNECTIONS, TEXT_ED2K_MAX_CONNECTIONS, "20", 1, 1024));
    op->addTag(TAG_ADVANCED);
    op->addTag(TAG_ED2K);
    op->setInitialOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new BooleanOptionHandler(
        PREF_ED2K_PREVIEW_PRIORITY, TEXT_ED2K_PREVIEW_PRIORITY, A2_V_FALSE,
        OptionHandler::OPT_ARG));
    op->addTag(TAG_ADVANCED);
    op->addTag(TAG_ED2K);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new ParameterOptionHandler(
        PREF_CONSOLE_LOG_LEVEL, TEXT_CONSOLE_LOG_LEVEL, V_INFO,
        {std::begin(logLevels), std::end(logLevels)}));
    op->addTag(TAG_ADVANCED);
    op->setChangeGlobalOption(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(
        new BooleanOptionHandler(PREF_DEFERRED_INPUT, TEXT_DEFERRED_INPUT,
                                 A2_V_FALSE, OptionHandler::OPT_ARG));
    op->addTag(TAG_ADVANCED);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new LocalFilePathOptionHandler(
        PREF_DIR, TEXT_DIR, File::getCurrentDir(), /* acceptStdin = */ false,
        'd',
        /* mustExist = */ false, PATH_TO_DIR));
    op->addTag(TAG_BASIC);
    op->addTag(TAG_FILE);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(
        new BooleanOptionHandler(PREF_DISABLE_IPV6, TEXT_DISABLE_IPV6,
#if defined(__MINGW32__) && !defined(__MINGW64__)
                                 // Disable IPv6 by default for
                                 // MinGW build.  This is because
                                 // numerous IPv6 routines are
                                 // available from Vista. Checking
                                 // getaddrinfo failed in
                                 // configure.
                                 A2_V_TRUE,
#else  // !defined(__MINGW32__) || defined(__MINGW64__)
                                 A2_V_FALSE,
#endif // !defined(__MINGW32__) || defined(__MINGW64__)
                                 OptionHandler::OPT_ARG));
    op->addTag(TAG_ADVANCED);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(
        new NumberOptionHandler(PREF_DNS_TIMEOUT, NO_DESCRIPTION, "30", 1, 60));
    op->hide();
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new ParameterOptionHandler(
        PREF_DOWNLOAD_RESULT, TEXT_DOWNLOAD_RESULT, A2_V_DEFAULT,
        {A2_V_DEFAULT, A2_V_FULL, A2_V_HIDE}));
    op->addTag(TAG_ADVANCED);
    op->setChangeGlobalOption(true);
    handlers.push_back(std::move(op));
  }
  {
    // https://no-color.org/: a non-empty NO_COLOR environment variable
    // disables colored output unless --enable-color is given explicitly.
    const char* noColor = getenv("NO_COLOR");
    std::unique_ptr<OptionHandler> op(new BooleanOptionHandler(
        PREF_ENABLE_COLOR, TEXT_ENABLE_COLOR,
        noColor && *noColor ? A2_V_FALSE : A2_V_TRUE, OptionHandler::OPT_ARG));
    op->addTag(TAG_ADVANCED);
    handlers.push_back(std::move(op));
  }
#if defined(HAVE_MMAP) || defined(__MINGW32__)
  {
    std::unique_ptr<OptionHandler> op(
        new BooleanOptionHandler(PREF_ENABLE_MMAP, TEXT_ENABLE_MMAP, A2_V_FALSE,
                                 OptionHandler::OPT_ARG));
    op->addTag(TAG_ADVANCED);
    op->addTag(TAG_EXPERIMENTAL);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
#endif // HAVE_MMAP || __MINGW32__
  {
    std::unique_ptr<OptionHandler> op(new BooleanOptionHandler(
        PREF_ENABLE_RPC, TEXT_ENABLE_RPC, A2_V_FALSE, OptionHandler::OPT_ARG));
    op->addTag(TAG_RPC);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(
        new ParameterOptionHandler(PREF_EVENT_POLL, TEXT_EVENT_POLL,
#if defined(HAVE_EPOLL)
                                   V_EPOLL,
#elif defined(HAVE_KQUEUE)
                                   V_KQUEUE,
#elif defined(HAVE_POLL)
                                   V_POLL,
#else  // defined(HAVE_EPOLL)
                                   V_SELECT,
#endif // defined(HAVE_EPOLL)
                                   {
#ifdef HAVE_EPOLL
                                       V_EPOLL,
#endif // HAVE_EPOLL
#ifdef HAVE_KQUEUE
                                       V_KQUEUE,
#endif // HAVE_KQUEUE
#ifdef HAVE_POLL
                                       V_POLL,
#endif // HAVE_POLL
                                       V_SELECT}));
    op->addTag(TAG_ADVANCED);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new ParameterOptionHandler(
        PREF_FILE_ALLOCATION, TEXT_FILE_ALLOCATION, V_TRUNC,
        {V_NONE, V_PREALLOC, V_TRUNC,
#ifdef HAVE_SOME_FALLOCATE
         V_FALLOC
#endif // HAVE_SOME_FALLOCATE
        },
        'a'));
    op->addTag(TAG_BASIC);
    op->addTag(TAG_FILE);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new BooleanOptionHandler(
        PREF_FORCE_SAVE, TEXT_FORCE_SAVE, A2_V_FALSE, OptionHandler::OPT_ARG));
    op->addTag(TAG_ADVANCED);
    op->setInitialOption(true);
    op->setChangeOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(
        new BooleanOptionHandler(PREF_SAVE_NOT_FOUND, TEXT_SAVE_NOT_FOUND,
                                 A2_V_TRUE, OptionHandler::OPT_ARG));
    op->addTag(TAG_ADVANCED);
    op->setInitialOption(true);
    op->setChangeOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(
        new BooleanOptionHandler(PREF_FORCE_SEQUENTIAL, TEXT_FORCE_SEQUENTIAL,
                                 A2_V_FALSE, OptionHandler::OPT_ARG, 'Z'));
    op->addTag(TAG_BASIC);
    op->addTag(TAG_BITTORRENT);
    op->setInitialOption(true);
    op->setChangeOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(
        new DefaultOptionHandler(PREF_GID, TEXT_GID, NO_DEFAULT_VALUE));
    op->addTag(TAG_ADVANCED);
    op->setInitialOption(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(
        new BooleanOptionHandler(PREF_HASH_CHECK_ONLY, TEXT_HASH_CHECK_ONLY,
                                 A2_V_FALSE, OptionHandler::OPT_ARG));
    op->addTag(TAG_ADVANCED);
    op->addTag(TAG_BITTORRENT);
    op->addTag(TAG_METALINK);
    op->addTag(TAG_FILE);
    op->addTag(TAG_CHECKSUM);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(
        new BooleanOptionHandler(PREF_HUMAN_READABLE, TEXT_HUMAN_READABLE,
                                 A2_V_TRUE, OptionHandler::OPT_ARG));
    op->addTag(TAG_ADVANCED);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new LocalFilePathOptionHandler(
        PREF_INPUT_FILE, TEXT_INPUT_FILE, NO_DEFAULT_VALUE,
        /* acceptStdin = */ true, 'i', /* mustExist = */ false));
    op->addTag(TAG_BASIC);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new DefaultOptionHandler(
        PREF_INTERFACE, TEXT_INTERFACE, NO_DEFAULT_VALUE,
        "interface, IP address, hostname", OptionHandler::REQ_ARG));
    op->addTag(TAG_ADVANCED);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(
        new BooleanOptionHandler(PREF_KEEP_UNFINISHED_DOWNLOAD_RESULT,
                                 TEXT_KEEP_UNFINISHED_DOWNLOAD_RESULT,
                                 A2_V_TRUE, OptionHandler::OPT_ARG));
    op->addTag(TAG_ADVANCED);
    op->setChangeGlobalOption(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new LocalFilePathOptionHandler(
        PREF_LOG, TEXT_LOG, NO_DEFAULT_VALUE, /* acceptStdin = */ false, 'l',
        /* mustExist = */ false, PATH_TO_FILE_STDOUT));
    op->addTag(TAG_BASIC);
    op->setChangeGlobalOption(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new ParameterOptionHandler(
        PREF_LOG_LEVEL, TEXT_LOG_LEVEL, V_DEBUG,
        {std::begin(logLevels), std::end(logLevels)}));
    op->addTag(TAG_ADVANCED);
    op->setChangeGlobalOption(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new UnitNumberOptionHandler(
        PREF_LOG_MAX_SIZE, TEXT_LOG_MAX_SIZE, "10M", 1_k, 1_g));
    op->addTag(TAG_ADVANCED);
    op->setChangeGlobalOption(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new NumberOptionHandler(
        PREF_LOG_MAX_FILES, TEXT_LOG_MAX_FILES, "4", 1, logging::MAX_FILES));
    op->addTag(TAG_ADVANCED);
    op->setChangeGlobalOption(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new NumberOptionHandler(
        PREF_MAX_CONCURRENT_DOWNLOADS, TEXT_MAX_CONCURRENT_DOWNLOADS, "5", 1,
        -1, 'j'));
    op->addTag(TAG_BASIC);
    op->setChangeGlobalOption(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new UnitNumberOptionHandler(
        PREF_MAX_DOWNLOAD_LIMIT, TEXT_MAX_DOWNLOAD_LIMIT, "0", 0));
    op->addTag(TAG_BITTORRENT);
    op->addTag(TAG_ED2K);
    op->addTag(TAG_HTTP);
    op->setInitialOption(true);
    op->setChangeOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new NumberOptionHandler(
        PREF_MAX_DOWNLOAD_RESULT, TEXT_MAX_DOWNLOAD_RESULT, "1000", 0));
    op->addTag(TAG_ADVANCED);
    op->setChangeGlobalOption(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new UnitNumberOptionHandler(
        PREF_MAX_MMAP_LIMIT, TEXT_MAX_MMAP_LIMIT,
        util::itos(std::numeric_limits<int64_t>::max()), 0));
    op->addTag(TAG_ADVANCED);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(
        new UnitNumberOptionHandler(PREF_MAX_OVERALL_DOWNLOAD_LIMIT,
                                    TEXT_MAX_OVERALL_DOWNLOAD_LIMIT, "0", 0));
    op->addTag(TAG_BITTORRENT);
    op->addTag(TAG_ED2K);
    op->addTag(TAG_HTTP);
    op->setChangeGlobalOption(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new UnitNumberOptionHandler(
        PREF_ED2K_MIN_SPLIT_SIZE, TEXT_ED2K_MIN_SPLIT_SIZE, "20M", 1_m, 1_g));
    op->addTag(TAG_ED2K);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
#ifdef ENABLE_SSL
  {
    std::unique_ptr<OptionHandler> op(new ParameterOptionHandler(
        PREF_MIN_TLS_VERSION, TEXT_MIN_TLS_VERSION, A2_V_TLS12,
        {A2_V_TLS11, A2_V_TLS12, A2_V_TLS13}));
    op->addTag(TAG_ADVANCED);
    handlers.push_back(std::move(op));
  }
#endif // ENABLE_SSL
  {
    std::unique_ptr<OptionHandler> op(new DefaultOptionHandler(
        PREF_MULTIPLE_INTERFACE, TEXT_MULTIPLE_INTERFACE, NO_DEFAULT_VALUE,
        "interface, IP address, hostname", OptionHandler::REQ_ARG));
    op->addTag(TAG_ADVANCED);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new BooleanOptionHandler(
        PREF_NO_CONF, TEXT_NO_CONF, A2_V_FALSE, OptionHandler::OPT_ARG));
    op->addTag(TAG_ADVANCED);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new UnitNumberOptionHandler(
        PREF_NO_FILE_ALLOCATION_LIMIT, TEXT_NO_FILE_ALLOCATION_LIMIT, "5M", 0));
    op->addTag(TAG_ADVANCED);
    op->addTag(TAG_FILE);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new LocalFilePathOptionHandler(
        PREF_ON_DOWNLOAD_COMPLETE, TEXT_ON_DOWNLOAD_COMPLETE, NO_DEFAULT_VALUE,
        /* acceptStdin = */ false, 0, /* mustExist = */ false,
        PATH_TO_COMMAND));
    op->addTag(TAG_ADVANCED);
    op->addTag(TAG_HOOK);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new LocalFilePathOptionHandler(
        PREF_ON_DOWNLOAD_ERROR, TEXT_ON_DOWNLOAD_ERROR, NO_DEFAULT_VALUE,
        /* acceptStdin = */ false, 0, /* mustExist = */ false,
        PATH_TO_COMMAND));
    op->addTag(TAG_ADVANCED);
    op->addTag(TAG_HOOK);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new LocalFilePathOptionHandler(
        PREF_ON_DOWNLOAD_PAUSE, TEXT_ON_DOWNLOAD_PAUSE, NO_DEFAULT_VALUE,
        /* acceptStdin = */ false, 0, /* mustExist = */ false,
        PATH_TO_COMMAND));
    op->addTag(TAG_ADVANCED);
    op->addTag(TAG_HOOK);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new LocalFilePathOptionHandler(
        PREF_ON_DOWNLOAD_START, TEXT_ON_DOWNLOAD_START, NO_DEFAULT_VALUE,
        /* acceptStdin = */ false, 0, /* mustExist = */ false,
        PATH_TO_COMMAND));
    op->addTag(TAG_ADVANCED);
    op->addTag(TAG_HOOK);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new LocalFilePathOptionHandler(
        PREF_ON_DOWNLOAD_STOP, TEXT_ON_DOWNLOAD_STOP, NO_DEFAULT_VALUE,
        /* acceptStdin = */ false, 0, /* mustExist = */ false,
        PATH_TO_COMMAND));
    op->addTag(TAG_ADVANCED);
    op->addTag(TAG_HOOK);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(
        new OptimizeConcurrentDownloadsOptionHandler(
            PREF_OPTIMIZE_CONCURRENT_DOWNLOADS,
            TEXT_OPTIMIZE_CONCURRENT_DOWNLOADS, A2_V_FALSE,
            OptionHandler::OPT_ARG));
    op->addTag(TAG_ADVANCED);
    op->setChangeGlobalOption(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(
        new BooleanOptionHandler(PREF_PARAMETERIZED_URI, TEXT_PARAMETERIZED_URI,
                                 A2_V_FALSE, OptionHandler::OPT_ARG, 'P'));
    op->addTag(TAG_ADVANCED);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new BooleanOptionHandler(
        PREF_PAUSE, TEXT_PAUSE, A2_V_FALSE, OptionHandler::OPT_ARG));
    op->addTag(TAG_ADVANCED);
    op->addTag(TAG_RPC);
    op->setInitialOption(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(
        new BooleanOptionHandler(PREF_PAUSE_METADATA, TEXT_PAUSE_METADATA,
                                 A2_V_FALSE, OptionHandler::OPT_ARG));
    op->addTag(TAG_ADVANCED);
    op->addTag(TAG_RPC);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new BooleanOptionHandler(
        PREF_QUIET, TEXT_QUIET, A2_V_FALSE, OptionHandler::OPT_ARG, 'q'));
    op->addTag(TAG_ADVANCED);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new BooleanOptionHandler(
        PREF_REALTIME_CHUNK_CHECKSUM, TEXT_REALTIME_CHUNK_CHECKSUM, A2_V_TRUE,
        OptionHandler::OPT_ARG));
    op->addTag(TAG_METALINK);
    op->addTag(TAG_CHECKSUM);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new LocalFilePathOptionHandler(
        PREF_SAVE_SESSION, TEXT_SAVE_SESSION, NO_DEFAULT_VALUE,
        /* acceptStdin = */ false, 0, /* mustExist = */ false));
    op->addTag(TAG_ADVANCED);
    op->setChangeGlobalOption(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new NumberOptionHandler(
        PREF_SAVE_SESSION_INTERVAL, TEXT_SAVE_SESSION_INTERVAL, "0", 0));
    op->addTag(TAG_ADVANCED);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(
        new NumberOptionHandler(PREF_DSCP, TEXT_DSCP, "0", 0));
    op->addTag(TAG_ADVANCED);
    handlers.push_back(std::move(op));
  }
#ifdef HAVE_SYS_RESOURCE_H
  {
    std::unique_ptr<OptionHandler> op(new NumberOptionHandler(
        PREF_RLIMIT_NOFILE, TEXT_RLIMIT_NOFILE,
        // Somewhat sane default that most *nix use.
        // Some other *nix, like OSX, have insane defaults like
        // 256, hence better *not* get the default value from
        // getrlimit().
        "1024",
        // 1 should not be a problem in practise, since the code
        // will only adjust if the specified value > the current
        // soft limit.
        // And sane systems have a default soft limit > 1.
        1));
    op->addTag(TAG_ADVANCED);
    handlers.push_back(std::move(op));
  }
#endif // HAVE_SYS_RESOURCE_H
  {
    std::unique_ptr<OptionHandler> op(new BooleanOptionHandler(
        PREF_SHOW_CONSOLE_READOUT, TEXT_SHOW_CONSOLE_READOUT, A2_V_TRUE));
    op->addTag(TAG_ADVANCED);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new UnitNumberOptionHandler(
        PREF_SOCKET_RECV_BUFFER_SIZE, TEXT_SOCKET_RECV_BUFFER_SIZE, "0", 0,
        16_m));
    op->addTag(TAG_ADVANCED);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new BooleanOptionHandler(
        PREF_STDERR, TEXT_STDERR, A2_V_FALSE, OptionHandler::OPT_ARG));

    op->addTag(TAG_ADVANCED);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(
        new NumberOptionHandler(PREF_STOP, TEXT_STOP, "0", 0, INT32_MAX));
    op->addTag(TAG_ADVANCED);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new NumberOptionHandler(
        PREF_STOP_WITH_PROCESS, TEXT_STOP_WITH_PROCESS, NO_DEFAULT_VALUE, 0));
    op->addTag(TAG_ADVANCED);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new NumberOptionHandler(
        PREF_SUMMARY_INTERVAL, TEXT_SUMMARY_INTERVAL, "60", 0, INT32_MAX));
    op->addTag(TAG_ADVANCED);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new BooleanOptionHandler(
        PREF_TRUNCATE_CONSOLE_READOUT, TEXT_TRUNCATE_CONSOLE_READOUT, A2_V_TRUE,
        OptionHandler::OPT_ARG));
    op->addTag(TAG_ADVANCED);
    handlers.push_back(std::move(op));
  }
}
} // namespace aria2::option
