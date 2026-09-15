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

void addMetalinkOptions(OptionHandlers& handlers)
{
#ifdef ENABLE_METALINK
  {
    std::unique_ptr<OptionHandler> op(
        new ParameterOptionHandler(PREF_FOLLOW_METALINK, TEXT_FOLLOW_METALINK,
                                   A2_V_TRUE, {A2_V_TRUE, V_MEM, A2_V_FALSE}));
    op->addTag(TAG_METALINK);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new DefaultOptionHandler(
        PREF_METALINK_BASE_URI, TEXT_METALINK_BASE_URI, NO_DEFAULT_VALUE));
    op->addTag(TAG_METALINK);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(
        new BooleanOptionHandler(PREF_METALINK_ENABLE_UNIQUE_PROTOCOL,
                                 TEXT_METALINK_ENABLE_UNIQUE_PROTOCOL,
                                 A2_V_TRUE, OptionHandler::OPT_ARG));
    op->addTag(TAG_METALINK);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new LocalFilePathOptionHandler(
        PREF_METALINK_FILE, TEXT_METALINK_FILE, NO_DEFAULT_VALUE, true, 'M'));
    op->addTag(TAG_BASIC);
    op->addTag(TAG_METALINK);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new DefaultOptionHandler(
        PREF_METALINK_LANGUAGE, TEXT_METALINK_LANGUAGE));
    op->addTag(TAG_METALINK);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(
        new DefaultOptionHandler(PREF_METALINK_OS, TEXT_METALINK_OS));
    op->addTag(TAG_METALINK);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new ParameterOptionHandler(
        PREF_METALINK_PREFERRED_PROTOCOL, TEXT_METALINK_PREFERRED_PROTOCOL,
        V_NONE, {V_HTTP, V_HTTPS, V_NONE}));
    op->addTag(TAG_METALINK);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(
        new DefaultOptionHandler(PREF_METALINK_VERSION, TEXT_METALINK_VERSION));
    op->addTag(TAG_METALINK);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
#endif // ENABLE_METALINK
}
} // namespace aria2::option
