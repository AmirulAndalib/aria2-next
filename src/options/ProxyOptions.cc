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
#include "help_tags.h"
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>

namespace aria2::option {

void addProxyOptions(OptionHandlers& handlers)
{
  // Proxy options
  {
    std::unique_ptr<OptionHandler> op(new HttpProxyOptionHandler(
        PREF_HTTP_PROXY, TEXT_HTTP_PROXY, NO_DEFAULT_VALUE));
    op->addTag(TAG_HTTP);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new DefaultOptionHandler(
        PREF_HTTP_PROXY_PASSWD, TEXT_HTTP_PROXY_PASSWD, NO_DEFAULT_VALUE));
    op->addTag(TAG_HTTP);
    op->setEraseAfterParse(true);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new DefaultOptionHandler(
        PREF_HTTP_PROXY_USER, TEXT_HTTP_PROXY_USER, NO_DEFAULT_VALUE));
    op->addTag(TAG_HTTP);
    op->setEraseAfterParse(true);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new HttpProxyOptionHandler(
        PREF_HTTPS_PROXY, TEXT_HTTPS_PROXY, NO_DEFAULT_VALUE));
    op->addTag(TAG_HTTP);
    op->addTag(TAG_HTTPS);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new DefaultOptionHandler(
        PREF_HTTPS_PROXY_PASSWD, TEXT_HTTPS_PROXY_PASSWD, NO_DEFAULT_VALUE));
    op->addTag(TAG_HTTP);
    op->addTag(TAG_HTTPS);
    op->setEraseAfterParse(true);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new DefaultOptionHandler(
        PREF_HTTPS_PROXY_USER, TEXT_HTTPS_PROXY_USER, NO_DEFAULT_VALUE));
    op->addTag(TAG_HTTP);
    op->addTag(TAG_HTTPS);
    op->setEraseAfterParse(true);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new HttpProxyOptionHandler(
        PREF_ALL_PROXY, TEXT_ALL_PROXY, NO_DEFAULT_VALUE));
    op->addTag(TAG_HTTP);
    op->addTag(TAG_HTTPS);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new DefaultOptionHandler(
        PREF_ALL_PROXY_PASSWD, TEXT_ALL_PROXY_PASSWD, NO_DEFAULT_VALUE));
    op->addTag(TAG_HTTP);
    op->addTag(TAG_HTTPS);
    op->setEraseAfterParse(true);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new DefaultOptionHandler(
        PREF_ALL_PROXY_USER, TEXT_ALL_PROXY_USER, NO_DEFAULT_VALUE));
    op->addTag(TAG_HTTP);
    op->addTag(TAG_HTTPS);
    op->setEraseAfterParse(true);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(
        new DefaultOptionHandler(PREF_NO_PROXY, TEXT_NO_PROXY, NO_DEFAULT_VALUE,
                                 "HOSTNAME,DOMAIN,NETWORK/CIDR"));
    op->addTag(TAG_HTTP);
    op->addTag(TAG_HTTPS);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
}
} // namespace aria2::option
