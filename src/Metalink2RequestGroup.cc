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
#include "Metalink2RequestGroup.h"

#include <algorithm>

#include "RequestGroup.h"
#include "Option.h"
#include "Log.h"
#include "prefs.h"
#include "util.h"
#include "message.h"
#include "DownloadContext.h"
#include "metalink_helper.h"
#include "BinaryStream.h"
#include "MetalinkEntry.h"
#include "MetalinkResource.h"
#include "MetalinkMetaurl.h"
#include "FileEntry.h"
#include "download_helper.h"
#include "fmt.h"
#include "DownloadFailureException.h"
#include "Signature.h"
#include "Checksum.h"
#include "ChunkChecksum.h"
#include "CurlDownload.h"

namespace aria2 {

Metalink2RequestGroup::Metalink2RequestGroup() = default;

namespace {
class AccumulateNonP2PUri {
private:
  std::vector<std::string>& urisPtr;

public:
  AccumulateNonP2PUri(std::vector<std::string>& urisPtr) : urisPtr(urisPtr) {}

  void operator()(const std::unique_ptr<MetalinkResource>& resource)
  {
    switch (resource->type) {
    case MetalinkResource::TYPE_HTTP:
    case MetalinkResource::TYPE_HTTPS:
    case MetalinkResource::TYPE_SFTP:
      urisPtr.push_back(resource->url);
      break;
    default:
      break;
    }
  }
};
} // namespace

void Metalink2RequestGroup::generate(
    std::vector<std::shared_ptr<RequestGroup>>& groups,
    const std::string& metalinkFile, const std::shared_ptr<Option>& option,
    const std::string& baseUri)
{
  std::vector<std::shared_ptr<RequestGroup>> tempgroups;
  createRequestGroup(
      tempgroups, metalink::parseAndQuery(metalinkFile, option.get(), baseUri),
      option);
  std::shared_ptr<MetadataInfo> mi;
  if (metalinkFile == DEV_STDIN) {
    mi = std::make_shared<MetadataInfo>();
  }
  else {
    // TODO Downloads from local metalink file does not save neither
    // its gid nor MetadataInfo's gid.
    mi = std::make_shared<MetadataInfo>(GroupId::create(), metalinkFile);
  }
  setMetadataInfo(std::begin(tempgroups), std::end(tempgroups), mi);
  groups.insert(std::end(groups), std::begin(tempgroups), std::end(tempgroups));
}

void Metalink2RequestGroup::generate(
    std::vector<std::shared_ptr<RequestGroup>>& groups,
    const std::shared_ptr<BinaryStream>& binaryStream,
    const std::shared_ptr<Option>& option, const std::string& baseUri)
{
  std::vector<std::shared_ptr<RequestGroup>> tempgroups;
  createRequestGroup(
      tempgroups,
      metalink::parseAndQuery(binaryStream.get(), option.get(), baseUri),
      option);
  auto mi = std::make_shared<MetadataInfo>();
  setMetadataInfo(std::begin(tempgroups), std::end(tempgroups), mi);
  groups.insert(std::end(groups), std::begin(tempgroups), std::end(tempgroups));
}

void Metalink2RequestGroup::createRequestGroup(
    std::vector<std::shared_ptr<RequestGroup>>& groups,
    std::vector<std::unique_ptr<MetalinkEntry>> entries,
    const std::shared_ptr<Option>& optionTemplate)
{
  if (entries.empty()) {
    A2_LOG_INFO(EX_NO_RESULT_WITH_YOUR_PREFS);
    return;
  }
  std::vector<std::string> locations;
  if (optionTemplate->defined(PREF_METALINK_LOCATION)) {
    auto& loc = optionTemplate->get(PREF_METALINK_LOCATION);
    util::split(std::begin(loc), std::end(loc), std::back_inserter(locations),
                ',', true);
    for (auto& s : locations) {
      util::lowercase(s);
    }
  }
  std::string preferredProtocol;
  if (optionTemplate->get(PREF_METALINK_PREFERRED_PROTOCOL) != V_NONE) {
    preferredProtocol = optionTemplate->get(PREF_METALINK_PREFERRED_PROTOCOL);
  }
  for (auto& entry : entries) {
    entry->dropUnsupportedResource();
    if (entry->resources.empty() && entry->metaurls.empty()) {
      continue;
    }
    entry->setLocationPriority(locations,
                               -MetalinkResource::getLowestPriority());
    if (!preferredProtocol.empty()) {
      entry->setProtocolPriority(preferredProtocol,
                                 -MetalinkResource::getLowestPriority());
    }
  }
  auto sgl = util::parseIntSegments(optionTemplate->get(PREF_SELECT_FILE));
  sgl.normalize();
  if (sgl.hasNext()) {
    size_t inspoint = 0;
    for (size_t i = 0, len = entries.size(); i < len && sgl.hasNext(); ++i) {
      size_t j = sgl.peek() - 1;
      if (i == j) {
        if (inspoint != i) {
          entries[inspoint] = std::move(entries[i]);
        }
        ++inspoint;
        sgl.next();
      }
    }
    entries.resize(inspoint);
  }
  for (auto& ownedEntry : entries) {
    auto* entry = ownedEntry.get();
    auto option = util::copy(optionTemplate);
    auto rg = std::make_shared<RequestGroup>(GroupId::create(), option);
    A2_LOG_DEBUG(fmt(MSG_METALINK_QUEUEING, entry->getPath().c_str()));
    entry->reorderResourcesByPriority();
    for (auto& resource : entry->resources) {
      A2_LOG_TRACE(fmt("priority=%d url=%s", resource->priority,
                       logging::sanitizeUri(resource->url).c_str()));
    }
    std::vector<std::string> uris;
    std::for_each(std::begin(entry->resources), std::end(entry->resources),
                  AccumulateNonP2PUri(uris));
    if (uris.empty()) {
      continue;
    }
    const auto pieceLength = entry->chunkChecksum
                                 ? entry->chunkChecksum->getPieceLength()
                                 : option->getAsInt(PREF_PIECE_LENGTH);
    auto dctx = std::make_shared<DownloadContext>(
        pieceLength, entry->getLength(),
        util::applyDir(option->get(PREF_DIR), entry->file->getPath()));
    dctx->getFirstFileEntry()->setUris(uris);
    dctx->getFirstFileEntry()->setSuffixPath(entry->file->getPath());
    if (!entry->metaurls.empty()) {
      dctx->getFirstFileEntry()->setOriginalName(entry->metaurls[0]->name);
    }
    if (option->getAsBool(PREF_METALINK_ENABLE_UNIQUE_PROTOCOL)) {
      dctx->getFirstFileEntry()->setUniqueProtocol(true);
    }
    if (entry->checksum) {
      dctx->setDigest(entry->checksum->getHashType(),
                      entry->checksum->getDigest());
    }
    if (entry->chunkChecksum) {
      dctx->setPieceHashes(entry->chunkChecksum->getHashType(),
                           std::begin(entry->chunkChecksum->getPieceHashes()),
                           std::end(entry->chunkChecksum->getPieceHashes()));
    }
    dctx->setSignature(entry->popSignature());
    rg->setDownloadContext(dctx);
    rg->setCurlDownload(std::make_shared<CurlDownload>(std::move(uris)));

    if (option->getAsBool(PREF_ENABLE_RPC)) {
      rg->setPauseRequested(option->getAsBool(PREF_PAUSE));
    }

    removeOneshotOption(option);
    // remove "metalink" from Accept Type list to avoid loop in
    // transparent metalink
    dctx->setAcceptMetalink(false);
    groups.push_back(rg);
  }
}

} // namespace aria2
