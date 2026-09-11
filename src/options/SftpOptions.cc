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
#include "support/FilePath.h"
#include "help_tags.h"
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>

namespace aria2::option {

void addSftpOptions(OptionHandlers& handlers)
{
  // SFTP options
  {
    std::unique_ptr<OptionHandler> op(
        new DefaultOptionHandler(PREF_SFTP_PASSWD, TEXT_SFTP_PASSWD));
    op->addTag(TAG_BASIC);
    op->setEraseAfterParse(true);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(
        new DefaultOptionHandler(PREF_SFTP_USER, TEXT_SFTP_USER));
    op->addTag(TAG_BASIC);
    op->setEraseAfterParse(true);
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new DefaultOptionHandler(
        PREF_SSH_HOST_KEY_SHA256, TEXT_SSH_HOST_KEY_SHA256));
    op->setInitialOption(true);
    op->setChangeGlobalOption(true);
    op->setChangeOptionForReserved(true);
    handlers.push_back(std::move(op));
  }
  {
    std::unique_ptr<OptionHandler> op(new LocalFilePathOptionHandler(
        PREF_NETRC_PATH, TEXT_NETRC_PATH, util::getHomeDir() + "/.netrc",
        /* acceptStdin = */ false, 0, /* mustExist = */ false));
    handlers.push_back(std::move(op));
  }
}
} // namespace aria2::option
