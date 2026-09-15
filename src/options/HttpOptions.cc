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
#include "timegm.h"
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

void addHttpOptions(OptionHandlers& handlers)
{
  // HTTP Specific Options
  {
    std::unique_ptr<OptionHandler> op(new LocalFilePathOptionHandler(
        PREF_CA_CERTIFICATE, TEXT_CA_CERTIFICATE, "",
        /* acceptStdin = */ false, 0, /* mustExist = */ false));
    op->addTag(TAG_HTTP);
    op->addTag(TAG_HTTPS);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new LocalFilePathOptionHandler(
        PREF_CERTIFICATE, TEXT_CERTIFICATE, NO_DEFAULT_VALUE, false));
    op->addTag(TAG_HTTP);
    op->addTag(TAG_HTTPS);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(
        new BooleanOptionHandler(PREF_CHECK_CERTIFICATE, TEXT_CHECK_CERTIFICATE,
                                 A2_V_TRUE, OptionHandler::OPT_ARG));
    op->addTag(TAG_HTTP);
    op->addTag(TAG_HTTPS);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new BooleanOptionHandler(
        PREF_ENABLE_HTTP_KEEP_ALIVE, TEXT_ENABLE_HTTP_KEEP_ALIVE, A2_V_TRUE,
        OptionHandler::OPT_ARG));
    op->addTag(TAG_HTTP);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new CumulativeOptionHandler(
        PREF_HEADER, TEXT_HEADER, NO_DEFAULT_VALUE, "\n"));
    op->addTag(TAG_HTTP);
    op->setInitialOption(true);
    op->setCumulative(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(
        new BooleanOptionHandler(PREF_HTTP_ACCEPT_GZIP, TEXT_HTTP_ACCEPT_GZIP,
                                 A2_V_FALSE, OptionHandler::OPT_ARG));
    op->addTag(TAG_HTTP);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(
        new BooleanOptionHandler(PREF_HTTP_NO_CACHE, TEXT_HTTP_NO_CACHE,
                                 A2_V_FALSE, OptionHandler::OPT_ARG));
    op->addTag(TAG_HTTP);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(
        new DefaultOptionHandler(PREF_HTTP_PASSWD, TEXT_HTTP_PASSWD));
    op->addTag(TAG_BASIC);
    op->addTag(TAG_HTTP);
    op->setEraseAfterParse(true);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(
        new DefaultOptionHandler(PREF_HTTP_USER, TEXT_HTTP_USER));
    op->addTag(TAG_BASIC);
    op->addTag(TAG_HTTP);
    op->setEraseAfterParse(true);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new LocalFilePathOptionHandler(
        PREF_LOAD_COOKIES, TEXT_LOAD_COOKIES, NO_DEFAULT_VALUE,
        /* acceptStdin = */ false, 0, /* mustExist = */ false));
    op->addTag(TAG_BASIC);
    op->addTag(TAG_HTTP);
    op->addTag(TAG_COOKIE);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new DefaultOptionHandler(
        PREF_METALINK_LOCATION, TEXT_METALINK_LOCATION));
    op->addTag(TAG_METALINK);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new LocalFilePathOptionHandler(
        PREF_PRIVATE_KEY, TEXT_PRIVATE_KEY, NO_DEFAULT_VALUE,
        /* acceptStdin = */ false, 0, /* mustExist = */ false));
    op->addTag(TAG_HTTP);
    op->addTag(TAG_HTTPS);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(
        new DefaultOptionHandler(PREF_REFERER, TEXT_REFERER));
    op->addTag(TAG_HTTP);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new LocalFilePathOptionHandler(
        PREF_SAVE_COOKIES, TEXT_SAVE_COOKIES, NO_DEFAULT_VALUE,
        /* acceptStdin = */ false, 0, /* mustExist = */ false));
    op->addTag(TAG_HTTP);
    op->addTag(TAG_COOKIE);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new DefaultOptionHandler(
        PREF_USER_AGENT, TEXT_USER_AGENT, "aria2-next/" PACKAGE_VERSION, "",
        OptionHandler::REQ_ARG, 'U'));
    op->addTag(TAG_HTTP);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
}
} // namespace aria2::option
