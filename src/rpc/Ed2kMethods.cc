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
#include "DownloadContext.h"
#include "ContextAttribute.h"
#include "GroupId.h"
#include "RpcRequest.h"
#include "ValueBase.h"
#include "ed2k_search.h"
#include <cstdint>
#include <memory>
#include <utility>
#include "common.h" // IWYU pragma: keep

#include "RpcMethods.h"
#include "RpcRequestHelpers.h"
#include "RpcFields.h"
#include "DownloadEngine.h"
#include "RequestGroup.h"
#include "support/Numbers.h"
#include "support/Encoding.h"
#include "fmt.h"
#include "DlAbortEx.h"
#include "Option.h"
#include "download_helper.h"
#include "Ed2kAttribute.h"
#include <limits>

namespace aria2::rpc {

using namespace fields;
using namespace detail;

namespace {
int64_t getDictInt(const Dict* dict, const char* key, int64_t defaultValue)
{
  if (!dict) {
    return defaultValue;
  }
  const auto value = downcast<Integer>(dict->get(key));
  return value ? value->i() : defaultValue;
}

uint32_t getDictUInt32(const Dict* dict, const char* key)
{
  const auto value = getDictInt(dict, key, 0);
  if (value <= 0) {
    return 0;
  }
  if (value > std::numeric_limits<uint32_t>::max()) {
    return std::numeric_limits<uint32_t>::max();
  }
  return static_cast<uint32_t>(value);
}

std::string getDictString(const Dict* dict, const char* key)
{
  if (!dict) {
    return std::string();
  }
  const auto value = downcast<String>(dict->get(key));
  return value ? value->s() : std::string();
}

std::unique_ptr<Dict>
createEd2kSearchResultEntry(const ed2k::SearchResultEntry& entry)
{
  auto dict = Dict::g();
  dict->put(KEY_HASH, util::toHex(entry.hash));
  dict->put(KEY_NAME, entry.name);
  dict->put(KEY_LENGTH, util::itos(entry.size));
  dict->put(KEY_SOURCE_COUNT, util::uitos(entry.sourceCount));
  dict->put(KEY_COMPLETE_SOURCE_COUNT, util::uitos(entry.completeSourceCount));
  dict->put(KEY_FILE_TYPE, entry.fileType);
  dict->put(KEY_EXTENSION, entry.extension);
  dict->put(KEY_MEDIA_ARTIST, entry.mediaArtist);
  dict->put(KEY_MEDIA_ALBUM, entry.mediaAlbum);
  dict->put(KEY_MEDIA_TITLE, entry.mediaTitle);
  dict->put(KEY_MEDIA_LENGTH, entry.mediaLength);
  dict->put(KEY_MEDIA_BITRATE, util::uitos(entry.mediaBitrate));
  dict->put(KEY_MEDIA_CODEC, entry.mediaCodec);
  dict->put(KEY_SOURCE_NETWORK, entry.sourceNetwork);
  dict->put(KEY_ED2K_LINK, entry.ed2kLink);
  return dict;
}
} // namespace

std::unique_ptr<ValueBase> Ed2kSearchRpcMethod::process(const RpcRequest& req,
                                                        DownloadEngine* e)
{
  const String* keywordParam = checkRequiredParam<String>(req, 0);
  const Dict* optsParam = checkParam<Dict>(req, 1);
  if (keywordParam->s().empty()) {
    throw DL_ABORT_EX("ED2K search keyword is empty.");
  }
  ed2k::SearchQuery query;
  query.keyword = keywordParam->s();
  query.fileType = getDictString(optsParam, KEY_FILE_TYPE);
  query.extension = getDictString(optsParam, KEY_EXTENSION);
  query.minSize = getDictInt(optsParam, "minSize", 0);
  query.maxSize = getDictInt(optsParam, "maxSize", 0);
  query.minSourceCount = getDictUInt32(optsParam, "minSourceCount");
  query.minCompleteSourceCount =
      getDictUInt32(optsParam, "minCompleteSourceCount");

  auto requestOption = std::make_shared<Option>(*e->getOption());
  gatherRequestOption(requestOption.get(), optsParam);
  auto group = createEd2kSearchRequestGroup(query, requestOption);
  return addRequestGroup(group, e, false, 0);
}

std::unique_ptr<ValueBase>
GetEd2kSearchResultsRpcMethod::process(const RpcRequest& req, DownloadEngine* e)
{
  const String* gidParam = checkRequiredParam<String>(req, 0);
  a2_gid_t gid = str2Gid(gidParam);
  auto group = e->getRequestGroupMan()->findGroup(gid);
  if (!group || !group->getDownloadContext()->hasAttribute(CTX_ATTR_ED2K)) {
    throw DL_ABORT_EX(fmt("No ED2K search data is available for GID#%s",
                          GroupId::toHex(gid).c_str()));
  }
  auto attrs = getEd2kAttrs(group->getDownloadContext());
  auto result = Dict::g();
  result->put(KEY_GID, GroupId::toHex(gid));
  result->put(KEY_MORE_RESULTS,
              attrs->searchMoreResults ? Bool::gTrue() : Bool::gFalse());
  auto entries = List::g();
  for (const auto& entry : attrs->searchResults) {
    entries->append(createEd2kSearchResultEntry(entry));
  }
  result->put("results", std::move(entries));
  return std::move(result);
}

} // namespace aria2::rpc
