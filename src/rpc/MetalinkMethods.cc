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
#include "RpcRequest.h"
#include "ValueBase.h"
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "common.h" // IWYU pragma: keep

#include "RpcMethods.h"
#include "RpcRequestHelpers.h"
#include "DownloadEngine.h"
#include "RequestGroup.h"
#include "support/Encoding.h"
#include "support/FilePath.h"
#include "fmt.h"
#include "prefs.h"
#include "Option.h"
#include "download_helper.h"
#include "base64.h"
#include "Log.h"
#include "MessageDigest.h"
#include "message_digest_helper.h"

namespace aria2::rpc {

using namespace detail;

namespace {
std::string getHexSha1(const std::string& s)
{
  unsigned char hash[20];
  message_digest::digest(hash, sizeof(hash), MessageDigest::sha1().get(),
                         s.data(), s.size());
  return util::toHex(hash, sizeof(hash));
}
} // namespace

std::unique_ptr<ValueBase> AddMetalinkRpcMethod::process(const RpcRequest& req,
                                                         DownloadEngine* e)
{
  const String* metalinkParam = checkRequiredParam<String>(req, 0);
  const Dict* optsParam = checkParam<Dict>(req, 1);
  const Integer* posParam = checkParam<Integer>(req, 2);

  std::unique_ptr<String> tempMetalinkParam;
  if (req.jsonRpc) {
    tempMetalinkParam = String::g(
        base64::decode(metalinkParam->s().begin(), metalinkParam->s().end()));
    metalinkParam = tempMetalinkParam.get();
  }
  auto requestOption = std::make_shared<Option>(*e->getOption());
  gatherRequestOption(requestOption.get(), optsParam);

  bool posGiven = checkPosParam(posParam);
  size_t pos = posGiven ? posParam->i() : 0;

  std::vector<std::shared_ptr<RequestGroup>> result;
  std::string filename;
  if (requestOption->getAsBool(PREF_RPC_SAVE_UPLOAD_METADATA)) {
    // TODO RFC5854 Metalink has the extension .meta4 and Metalink
    // Version 3 uses .metalink extension. We use .meta4 for both
    // RFC5854 Metalink and Version 3. aria2 can detect which of which
    // by reading content rather than extension.
    filename = util::applyDir(requestOption->get(PREF_DIR),
                              getHexSha1(metalinkParam->s()) + ".meta4");
    // Save uploaded data in order to save this download in
    // --save-session file.
    if (util::saveAs(filename, metalinkParam->s(), true)) {
      A2_LOG_DEBUG(
          fmt("Uploaded metalink data was saved as %s", filename.c_str()));
      requestOption->put(PREF_METALINK_FILE, filename);
      createRequestGroupForMetalink(result, requestOption);
    }
    else {
      A2_LOG_DEBUG(fmt("Uploaded metalink data was not saved."
                       " Failed to write file %s",
                       filename.c_str()));
      createRequestGroupForMetalink(result, requestOption, metalinkParam->s());
    }
  }
  else {
    createRequestGroupForMetalink(result, requestOption, metalinkParam->s());
  }
  auto gids = List::g();
  if (!result.empty()) {
    if (posGiven) {
      e->getRequestGroupMan()->insertReservedGroup(pos, result);
    }
    else {
      e->getRequestGroupMan()->addReservedGroup(result);
    }
    for (auto& i : result) {
      gids->append(GroupId::toHex(i->getGID()));
    }
  }
  return std::move(gids);
}

} // namespace aria2::rpc
