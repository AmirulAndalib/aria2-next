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

void addSharingOptions(OptionHandlers& handlers)
{
// BitTorrent/Metalink Options
#if defined(ENABLE_BITTORRENT) || defined(ENABLE_METALINK)
  {
    std::unique_ptr<OptionHandler> op(new IntegerRangeOptionHandler(
        PREF_SELECT_FILE, TEXT_SELECT_FILE, NO_DEFAULT_VALUE, 1, 1_m));
    op->addTag(TAG_BITTORRENT);
    op->addTag(TAG_METALINK);
    op->setInitialOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(
        new BooleanOptionHandler(PREF_SHOW_FILES, TEXT_SHOW_FILES, A2_V_FALSE,
                                 OptionHandler::OPT_ARG, 'S'));
    op->addTag(TAG_BASIC);
    op->addTag(TAG_BITTORRENT);
    op->addTag(TAG_METALINK);
    handlers.push_back(std::move(op));
  }
#endif // ENABLE_BITTORRENT || ENABLE_METALINK
// P2P sharing options
#if defined(ENABLE_BITTORRENT) || defined(ENABLE_ED2K)
  {
    std::unique_ptr<OptionHandler> op(
        new BooleanOptionHandler(PREF_DETACH_SHARE_ONLY, TEXT_DETACH_SHARE_ONLY,
                                 A2_V_FALSE, OptionHandler::OPT_ARG));
    op->addTag(TAG_BITTORRENT);
    op->addTag(TAG_ED2K);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new FloatNumberOptionHandler(
        PREF_SEED_TIME, TEXT_SEED_TIME, NO_DEFAULT_VALUE, 0));
    op->addTag(TAG_BITTORRENT);
    op->addTag(TAG_ED2K);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new FloatNumberOptionHandler(
        PREF_SEED_RATIO, TEXT_SEED_RATIO, "1.0", 0.0));
    op->addTag(TAG_BITTORRENT);
    op->addTag(TAG_ED2K);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
#endif // ENABLE_BITTORRENT || ENABLE_ED2K
}
} // namespace aria2::option
