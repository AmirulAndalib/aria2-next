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
#include "OptionDefinitions.h"

#include "OptionHandlerImpl.h"
#include "prefs.h"
#include "usage_text.h"
#include "a2functional.h"
#include "help_tags.h"
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace aria2::option {

void addStreamOptions(OptionHandlers& handlers)
{
  {
    auto op = std::make_unique<DefaultOptionHandler>(
        PREF_FILENAME_HINT,
        " --filename-hint=NAME        Suggest a decoded Unicode basename.", "");
    op->addTag(TAG_HTTP);
    op->setInitialOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    auto op = std::make_unique<ParameterOptionHandler>(
        PREF_FILENAME_HINT_SOURCE,
        " --filename-hint-source=browser|title|suggested  Browser names precede "
        "response headers; other hints follow them.",
        "suggested", std::vector<std::string>{"browser", "title", "suggested"});
    op->addTag(TAG_HTTP);
    op->setInitialOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(
        new ChecksumOptionHandler(PREF_CHECKSUM, TEXT_CHECKSUM));
    op->addTag(TAG_HTTP);
    op->addTag(TAG_CHECKSUM);
    op->setInitialOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new NumberOptionHandler(
        PREF_CONNECT_TIMEOUT, TEXT_CONNECT_TIMEOUT, "60", 1, 600));
    op->addTag(TAG_HTTP);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new BooleanOptionHandler(
        PREF_DRY_RUN, TEXT_DRY_RUN, A2_V_FALSE, OptionHandler::OPT_ARG));
    op->addTag(TAG_HTTP);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new UnitNumberOptionHandler(
        PREF_LOWEST_SPEED_LIMIT, TEXT_LOWEST_SPEED_LIMIT, "0", 0));
    op->addTag(TAG_HTTP);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new NumberOptionHandler(
        PREF_MAX_FILE_NOT_FOUND, TEXT_MAX_FILE_NOT_FOUND, "0", 0));
    op->addTag(TAG_HTTP);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new NumberOptionHandler(
        PREF_MAX_TRIES, TEXT_MAX_TRIES, "5", 0, -1, 'm'));
    op->addTag(TAG_HTTP);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new BooleanOptionHandler(
        PREF_NO_NETRC, TEXT_NO_NETRC, A2_V_FALSE, OptionHandler::OPT_ARG, 'n'));
    op->addTag(TAG_HTTP);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new LocalFilePathOptionHandler(
        PREF_OUT, TEXT_OUT, NO_DEFAULT_VALUE,
        /* acceptStdin = */ false, 'o', /* mustExist = */ false));
    op->addTag(TAG_BASIC);
    op->addTag(TAG_HTTP);
    op->addTag(TAG_FILE);
    op->setInitialOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new UnitNumberOptionHandler(
        PREF_PIECE_LENGTH, TEXT_PIECE_LENGTH, "1M", 1_m, 1_g));
    op->addTag(TAG_ADVANCED);
    op->addTag(TAG_ED2K);
    op->addTag(TAG_METALINK);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(
        new BooleanOptionHandler(PREF_REMOTE_TIME, TEXT_REMOTE_TIME, A2_V_FALSE,
                                 OptionHandler::OPT_ARG, 'R'));
    op->addTag(TAG_HTTP);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(
        new NumberOptionHandler(PREF_RETRY_WAIT, TEXT_RETRY_WAIT, "0", 0, 600));
    op->addTag(TAG_HTTP);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new NumberOptionHandler(
        PREF_STREAM_MAX_CONNECTIONS, TEXT_STREAM_MAX_CONNECTIONS, "6", 1, 256));
    op->addTag(TAG_HTTP);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new ParameterOptionHandler(
        PREF_ED2K_PIECE_SELECTOR, TEXT_ED2K_PIECE_SELECTOR, A2_V_DEFAULT,
        {A2_V_DEFAULT, V_INORDER, A2_V_RANDOM, A2_V_GEOM}));
    op->addTag(TAG_ED2K);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(
        new NumberOptionHandler(PREF_TIMEOUT, TEXT_TIMEOUT, "60", 1, 600, 't'));
    op->addTag(TAG_HTTP);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    auto op = std::unique_ptr<ParameterOptionHandler>(
        new ParameterOptionHandler(PREF_MEDIA,
                                   " --media=auto|file|hls|dash     Select "
                                   "media handling (auto detects manifests).",
                                   "auto", {"auto", "file", "hls", "dash"}));
    op->addTag(TAG_HTTP);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    auto op =
        std::unique_ptr<ParameterOptionHandler>(new ParameterOptionHandler(
            PREF_MEDIA_FORMAT,
            " --media-format=mp4|mkv       Select the media output container.",
            "mp4", {"mp4", "mkv"}));
    op->addTag(TAG_HTTP);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    auto op = std::make_unique<DefaultOptionHandler>(
        PREF_MEDIA_REQUEST_CONTEXTS,
        " --media-request-contexts=JSON  HTTP request headers scoped by "
        "origin.",
        "");
    op->addTag(TAG_HTTP);
    op->setInitialOption(true);
    op->setChangeOptionForReserved(true);
    op->setEraseAfterParse(true);
    handlers.push_back(std::move(op));
  }
  for (auto pref : {PREF_MEDIA_VIDEO, PREF_MEDIA_AUDIO, PREF_MEDIA_SUBTITLES}) {
    auto op = std::unique_ptr<DefaultOptionHandler>(new DefaultOptionHandler(
        pref,
        " Select best, none, a language, or an opaque track ID from "
        "media.tracks.",
        pref == PREF_MEDIA_SUBTITLES ? "none" : "best"));
    op->addTag(TAG_HTTP);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    auto op = std::unique_ptr<BooleanOptionHandler>(new BooleanOptionHandler(
        PREF_MEDIA_PAUSE_AFTER_PROBE,
        " --media-pause-after-probe    Pause after publishing media tracks, "
        "before downloading segments.",
        "false"));
    op->addTag(TAG_HTTP);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    auto op = std::unique_ptr<NumberOptionHandler>(new NumberOptionHandler(
        PREF_MEDIA_RECORD_TIME,
        " --media-record-time=SECONDS  Stop live recording after this media "
        "duration (0: unlimited).",
        "0", 0, 31536000));
    op->addTag(TAG_HTTP);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
}
} // namespace aria2::option
