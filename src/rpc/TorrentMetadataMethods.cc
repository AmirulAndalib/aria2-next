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
#include "RecoverableException.h"
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
#include "support/Numbers.h"
#include "message.h"
#include "DlAbortEx.h"
#include "prefs.h"
#include "Option.h"
#include "download_helper.h"
#include "BtDownload.h"
#include "BtStateStore.h"
#include "base64.h"
#include "RpcResponse.h"
#include "Log.h"
#include <openssl/evp.h>
#include <iterator>

namespace aria2::rpc {

using namespace detail;

std::unique_ptr<ValueBase> AddTorrentRpcMethod::process(const RpcRequest& req,
                                                        DownloadEngine* e)
{
  const String* torrentParam = checkRequiredParam<String>(req, 0);
  const List* urisParam = checkParam<List>(req, 1);
  const Dict* optsParam = checkParam<Dict>(req, 2);
  const Integer* posParam = checkParam<Integer>(req, 3);

  std::unique_ptr<String> tempTorrentParam;
  if (req.jsonRpc) {
    tempTorrentParam = String::g(
        base64::decode(torrentParam->s().begin(), torrentParam->s().end()));
    torrentParam = tempTorrentParam.get();
  }
  std::vector<std::string> uris;
  toStringList(std::back_inserter(uris), urisParam);

  auto requestOption = std::make_shared<Option>(*e->getOption());
  gatherRequestOption(requestOption.get(), optsParam);

  bool posGiven = checkPosParam(posParam);
  size_t pos = posGiven ? posParam->i() : 0;

  auto download = BtDownload::fromBuffer(torrentParam->s(), uris);
  auto stateStore = e->getRequestGroupMan()->getBtStateStore();
  if (!stateStore) {
    throw DL_ABORT_EX("BitTorrent state store is unavailable");
  }
  const auto filename = stateStore->storeMetadata(download->torrentFileData());
  download->setManagedMetadataPath(filename);
  requestOption->put(PREF_TORRENT_FILE, filename);

  std::vector<std::shared_ptr<RequestGroup>> result;
  try {
    createRequestGroupForBitTorrent(result, requestOption, download, filename);
    if (!result.empty()) {
      return addRequestGroup(result.front(), e, posGiven, pos);
    }
  }
  catch (...) {
    e->getRequestGroupMan()->collectBtStateGarbage();
    throw;
  }
  e->getRequestGroupMan()->collectBtStateGarbage();
  throw DL_ABORT_EX("No Torrent to download.");
}

namespace {
std::string decodeTorrentBase64(const std::string& encoded)
{
  const auto maxEncodedSize = ((BtDownload::maxMetainfoSize() + 2) / 3) * 4;
  if (encoded.size() > maxEncodedSize) {
    throw BtMetainfoError(__FILE__, __LINE__, "Torrent metadata is too large.",
                          "torrentTooLarge", "aria2", 0);
  }

  using DecodeContext =
      std::unique_ptr<EVP_ENCODE_CTX, decltype(&EVP_ENCODE_CTX_free)>;
  DecodeContext context(EVP_ENCODE_CTX_new(), EVP_ENCODE_CTX_free);
  if (!context) {
    throw DL_ABORT_EX("Unable to allocate Base64 decoder.");
  }

  std::string decoded((encoded.size() / 4 + 1) * 3, '\0');
  int updateLength = 0;
  int finalLength = 0;
  EVP_DecodeInit(context.get());
  if (EVP_DecodeUpdate(
          context.get(), reinterpret_cast<unsigned char*>(decoded.data()),
          &updateLength, reinterpret_cast<const unsigned char*>(encoded.data()),
          static_cast<int>(encoded.size())) < 0 ||
      EVP_DecodeFinal(context.get(),
                      reinterpret_cast<unsigned char*>(decoded.data()) +
                          updateLength,
                      &finalLength) < 0) {
    throw BtMetainfoError(__FILE__, __LINE__, "Invalid Base64 torrent data.",
                          "invalidBase64", "rpc", 0);
  }
  decoded.resize(static_cast<size_t>(updateLength + finalLength));
  return decoded;
}

std::unique_ptr<ValueBase>
createTorrentInspectionError(const BtMetainfoError& error,
                             const RpcRequest& req)
{
  auto response = Dict::g();
  response->put(req.jsonRpc ? "code" : "faultCode", Integer::g(1));
  response->put(req.jsonRpc ? "message" : "faultString", error.what());
  auto data = Dict::g();
  data->put("kind", error.kind());
  data->put("category", error.category());
  data->put("code", Integer::g(error.nativeCode()));
  response->put("data", std::move(data));
  return response;
}
} // namespace

std::unique_ptr<ValueBase>
InspectTorrentRpcMethod::process(const RpcRequest& req, DownloadEngine*)
{
  const String* torrentParam = checkRequiredParam<String>(req, 0);
  const auto data =
      req.jsonRpc ? decodeTorrentBase64(torrentParam->s()) : torrentParam->s();
  const auto metainfo = BtDownload::fromBuffer(data, {})->metainfo();

  auto result = Dict::g();
  result->put("name", metainfo.name);
  result->put("mode",
              metainfo.mode == BtMetainfo::Mode::Multi ? "multi" : "single");
  result->put("infoHashV1", metainfo.infoHashV1);
  result->put("infoHashV2", metainfo.infoHashV2);
  result->put("totalLength", util::itos(metainfo.totalLength));
  auto files = List::g();
  for (const auto& file : metainfo.files) {
    auto entry = Dict::g();
    entry->put("index", util::uitos(file.index));
    entry->put("path", file.path);
    entry->put("length", util::itos(file.length));
    files->append(std::move(entry));
  }
  result->put("files", std::move(files));
  return result;
}

RpcResponse InspectTorrentRpcMethod::execute(RpcRequest req, DownloadEngine* e)
{
  auto authorized = RpcResponse::NOTAUTHORIZED;
  try {
    authorize(req, e);
    authorized = RpcResponse::AUTHORIZED;
    return RpcResponse(0, authorized, process(req, e), std::move(req.id));
  }
  catch (BtMetainfoError& error) {
    A2_LOG_TRACE_EX(EX_EXCEPTION_CAUGHT, error);
    return RpcResponse(1, authorized, createTorrentInspectionError(error, req),
                       std::move(req.id));
  }
  catch (RecoverableException& error) {
    A2_LOG_TRACE_EX(EX_EXCEPTION_CAUGHT, error);
    return RpcResponse(1, authorized, createErrorResponse(error, req),
                       std::move(req.id));
  }
}

} // namespace aria2::rpc
