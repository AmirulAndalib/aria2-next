/* <!-- copyright */
/*
 * aria2 - The high speed download utility
 *
 * Copyright (C) 2009 Tatsuhiro Tsujikawa
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
#include "RequestGroupActions.h"
#include "ContextAttribute.h"
#include <memory>
#include <string>
#include <utility>
#include "platform/SocketAddress.h"

#include "DownloadEngine.h"
#include "RequestGroup.h"
#include "RequestGroupMan.h"
#include "DownloadContext.h"
#include "FileEntry.h"
#include "Option.h"
#include "prefs.h"
#include "support/Text.h"
#include "support/Numbers.h"
#include "support/Encoding.h"
#include "support/FilePath.h"
#include "a2functional.h"
#include "DlAbortEx.h"
#include "SocketCore.h"
#include "Log.h"
#ifdef ENABLE_BITTORRENT
#  include "BtDownload.h"
#  include "BtSession.h"
#  include "BtMetadata.h"
#endif
#include <array>
#include <cassert>

namespace aria2 {

void changeOption(const std::shared_ptr<RequestGroup>& group,
                  const Option& option, DownloadEngine* e)
{
  const std::shared_ptr<DownloadContext>& dctx = group->getDownloadContext();
  const std::shared_ptr<Option>& grOption = group->getOption();
#ifdef ENABLE_BITTORRENT
  if (option.defined(PREF_SELECT_FILE) && group->getBtDownload()) {
    Option candidate(*grOption);
    candidate.merge(option);
    group->getBtDownload()->validateFileSelection(&candidate);
  }
#endif // ENABLE_BITTORRENT
  grOption->merge(option);
  if (option.defined(PREF_CHECKSUM)) {
    const std::string& checksum = grOption->get(PREF_CHECKSUM);
    auto p = util::divide(std::begin(checksum), std::end(checksum), '=');
    std::string hashType(p.first.first, p.first.second);
    util::lowercase(hashType);
    dctx->setDigest(hashType, util::fromHex(p.second.first, p.second.second));
  }
  if (option.defined(PREF_SELECT_FILE)) {
    auto sgl = util::parseIntSegments(grOption->get(PREF_SELECT_FILE));
    sgl.normalize();
    dctx->setFileFilter(std::move(sgl));
  }
  if (option.defined(PREF_ED2K_MAX_CONNECTIONS) &&
      dctx->hasAttribute(CTX_ATTR_ED2K)) {
    group->setNumConcurrentCommand(
        grOption->getAsInt(PREF_ED2K_MAX_CONNECTIONS));
  }
  if (option.defined(PREF_DIR) || option.defined(PREF_OUT)) {
    if (!group->getMetadataInfo()) {

      assert(dctx->getFileEntries().size() == 1);

      auto& fileEntry = dctx->getFirstFileEntry();

      if (!grOption->blank(PREF_OUT)) {
        fileEntry->setPath(
            util::applyDir(grOption->get(PREF_DIR), grOption->get(PREF_OUT)));
        fileEntry->setSuffixPath("");
      }
      else if (fileEntry->getSuffixPath().empty()) {
        fileEntry->setPath("");
      }
      else {
        fileEntry->setPath(util::applyDir(grOption->get(PREF_DIR),
                                          fileEntry->getSuffixPath()));
      }
    }
    else if (group->getMetadataInfo()
#ifdef ENABLE_BITTORRENT
             && !dctx->hasAttribute(CTX_ATTR_BT)
#endif // ENABLE_BITTORRENT
    ) {
      // In case of Metalink
      for (auto& fileEntry : dctx->getFileEntries()) {
        // PREF_OUT is not applicable to Metalink.  We have always
        // suffixPath set.
        fileEntry->setPath(util::applyDir(grOption->get(PREF_DIR),
                                          fileEntry->getSuffixPath()));
      }
    }
  }
#ifdef ENABLE_BITTORRENT
  if (option.defined(PREF_DIR) || option.defined(PREF_INDEX_OUT)) {
    if (group->getBtDownload()) {
      group->getBtDownload()->updateFilePaths(dctx, grOption.get());
    }
  }
#endif // ENABLE_BITTORRENT
  if (option.defined(PREF_MAX_DOWNLOAD_LIMIT)) {
    group->setMaxDownloadSpeedLimit(
        grOption->getAsInt(PREF_MAX_DOWNLOAD_LIMIT));
  }
  if (option.defined(PREF_MAX_UPLOAD_LIMIT)) {
    group->setMaxUploadSpeedLimit(grOption->getAsInt(PREF_MAX_UPLOAD_LIMIT));
  }
#ifdef ENABLE_BITTORRENT
  if (group->getBtDownload()) {
    if (option.defined(PREF_SELECT_FILE)) {
      group->getBtDownload()->updateSelection(dctx);
      group->getBtDownload()->submitFileSelection(grOption.get());
    }
    if (e->getBtSession()) {
      e->getBtSession()->applyDownloadOptions(group->getBtDownload(),
                                              grOption.get());
    }
  }
#endif
}

void changeGlobalOption(const Option& option, DownloadEngine* e)
{
#ifdef ENABLE_BITTORRENT
  if (e->getBtSession()) {
    Option candidate(*e->getOption());
    candidate.merge(option);
    e->getBtSession()->validateGlobalOptions(&candidate);
  }
  if (option.defined(PREF_BT_EXTERNAL_IP) &&
      !option.blank(PREF_BT_EXTERNAL_IP)) {
    std::array<unsigned char, 16> address{};
    if (net::getBinAddr(address.data(), option.get(PREF_BT_EXTERNAL_IP)) == 0) {
      throw DL_ABORT_EX("bt-external-ip must be a numeric IP address");
    }
  }
  if (option.defined(PREF_BT_PEER_BLOCKLIST)) {
    const auto& path = option.get(PREF_BT_PEER_BLOCKLIST);
    if (path.empty()) {
      std::string error;
      e->getBtSession()->replaceIpFilter({}, error);
    }
    else {
      e->getBtSession()->loadIpFilter(path);
    }
  }
#endif // ENABLE_BITTORRENT
  e->getOption()->merge(option);
#ifdef ENABLE_BITTORRENT
  if (e->getBtSession()) {
    e->getBtSession()->applyGlobalOptions(e->getOption());
    const bool updateTorrents =
        option.defined(PREF_BT_TRACKER) ||
        option.defined(PREF_BT_EXCLUDE_TRACKER) ||
        option.defined(PREF_BT_MAX_PEERS) ||
        option.defined(PREF_BT_MAX_UPLOADS_PER_TORRENT) ||
        option.defined(PREF_BT_FIRST_LAST_PIECE_FIRST) ||
        option.defined(PREF_BT_SUPER_SEEDING) ||
        option.defined(PREF_ENABLE_DHT) ||
        option.defined(PREF_ENABLE_PEER_EXCHANGE) ||
        option.defined(PREF_BT_ENABLE_LPD) ||
        option.defined(PREF_FORCE_SEQUENTIAL);
    if (updateTorrents) {
      auto apply = [&](auto& groups) {
        for (const auto& group : groups) {
          if (!group->getBtDownload()) {
            continue;
          }
          group->getOption()->merge(option);
          e->getBtSession()->applyDownloadOptions(group->getBtDownload(),
                                                  group->getOption().get());
        }
      };
      apply(e->getRequestGroupMan()->getRequestGroups());
      apply(e->getRequestGroupMan()->getReservedGroups());
    }
  }
#endif
  bool reconfigureLogging = false;
  auto logSettings = logging::getSettings();
  if (option.defined(PREF_MAX_OVERALL_DOWNLOAD_LIMIT)) {
    e->getRequestGroupMan()->setMaxOverallDownloadSpeedLimit(
        option.getAsInt(PREF_MAX_OVERALL_DOWNLOAD_LIMIT));
  }
  if (option.defined(PREF_MAX_OVERALL_UPLOAD_LIMIT)) {
    e->getRequestGroupMan()->setMaxOverallUploadSpeedLimit(
        option.getAsInt(PREF_MAX_OVERALL_UPLOAD_LIMIT));
  }
  if (option.defined(PREF_MAX_CONCURRENT_DOWNLOADS)) {
    e->getRequestGroupMan()->setMaxConcurrentDownloads(
        option.getAsInt(PREF_MAX_CONCURRENT_DOWNLOADS));
    e->getRequestGroupMan()->reduceActiveDownloadsToLimit(e);
    e->getRequestGroupMan()->requestQueueCheck();
  }
  if (option.defined(PREF_OPTIMIZE_CONCURRENT_DOWNLOADS)) {
    e->getRequestGroupMan()->setupOptimizeConcurrentDownloads();
    e->getRequestGroupMan()->requestQueueCheck();
  }
  if (option.defined(PREF_MAX_DOWNLOAD_RESULT)) {
    e->getRequestGroupMan()->setMaxDownloadResult(
        option.getAsInt(PREF_MAX_DOWNLOAD_RESULT));
  }
  if (option.defined(PREF_LOG_LEVEL)) {
    logSettings.fileLevel = logging::parseLevel(option.get(PREF_LOG_LEVEL));
    reconfigureLogging = true;
  }
  if (option.defined(PREF_CONSOLE_LOG_LEVEL)) {
    logSettings.consoleLevel =
        logging::parseLevel(option.get(PREF_CONSOLE_LOG_LEVEL));
    reconfigureLogging = true;
  }
  if (option.defined(PREF_LOG)) {
    logSettings.file = option.get(PREF_LOG);
    reconfigureLogging = true;
  }
  if (option.defined(PREF_LOG_MAX_SIZE)) {
    logSettings.maxFileSize = option.getAsLLInt(PREF_LOG_MAX_SIZE);
    reconfigureLogging = true;
  }
  if (option.defined(PREF_LOG_MAX_FILES)) {
    logSettings.maxFiles = option.getAsInt(PREF_LOG_MAX_FILES);
    reconfigureLogging = true;
  }
  if (reconfigureLogging) {
    logging::configure(logSettings);
  }
}

} // namespace aria2
