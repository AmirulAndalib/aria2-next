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

#include "OptionHandlerImpl.h"
#include "prefs.h"
#include "usage_text.h"
#include "help_tags.h"
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>

namespace aria2::option {

void addRpcOptions(OptionHandlers& handlers)
{
  {
    std::unique_ptr<OptionHandler> op(new BooleanOptionHandler(
        PREF_RPC_ALLOW_ORIGIN_ALL, TEXT_RPC_ALLOW_ORIGIN_ALL, A2_V_FALSE,
        OptionHandler::OPT_ARG));
    op->addTag(TAG_RPC);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new LocalFilePathOptionHandler(
        PREF_RPC_CERTIFICATE, TEXT_RPC_CERTIFICATE, NO_DEFAULT_VALUE, false));
    op->addTag(TAG_RPC);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(
        new BooleanOptionHandler(PREF_RPC_LISTEN_ALL, TEXT_RPC_LISTEN_ALL,
                                 A2_V_FALSE, OptionHandler::OPT_ARG));
    op->addTag(TAG_RPC);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new NumberOptionHandler(
        PREF_RPC_LISTEN_PORT, TEXT_RPC_LISTEN_PORT, "6800", 1024, UINT16_MAX));
    op->addTag(TAG_RPC);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new UnitNumberOptionHandler(
        PREF_RPC_MAX_REQUEST_SIZE, TEXT_RPC_MAX_REQUEST_SIZE, "2M", 0));
    op->addTag(TAG_RPC);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new LocalFilePathOptionHandler(
        PREF_RPC_PRIVATE_KEY, TEXT_RPC_PRIVATE_KEY, NO_DEFAULT_VALUE, false));
    op->addTag(TAG_RPC);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new BooleanOptionHandler(
        PREF_RPC_SAVE_UPLOAD_METADATA, TEXT_RPC_SAVE_UPLOAD_METADATA, A2_V_TRUE,
        OptionHandler::OPT_ARG));
    op->addTag(TAG_RPC);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    handlers.push_back(std::move(op));
  }
  {
    auto op = std::make_unique<DefaultOptionHandler>(PREF_RPC_SECRET,
                                                     TEXT_RPC_SECRET);
    op->addTag(TAG_RPC);
    op->setEraseAfterParse(true);
    op->setAllowEmpty(false);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new BooleanOptionHandler(
        PREF_RPC_SECURE, TEXT_RPC_SECURE, A2_V_FALSE, OptionHandler::OPT_ARG));
    op->addTag(TAG_RPC);
    handlers.push_back(std::move(op));
  }
}
} // namespace aria2::option
