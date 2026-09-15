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
#include "FileEntry.h"
#include "CurlDownload.h"
#include "DownloadContext.h"
#include "SegmentMan.h"
#include "GroupId.h"
#include "RpcRequest.h"
#include "ValueBase.h"
#include <cstddef>
#include <memory>
#include <utility>
#include "common.h" // IWYU pragma: keep

#include "RpcMethods.h"
#include "RpcRequestHelpers.h"
#include "DownloadEngine.h"
#include "RequestGroup.h"
#include "fmt.h"
#include "DlAbortEx.h"
#include "media/MediaDownload.h"

namespace aria2::rpc {

using namespace detail;

std::unique_ptr<ValueBase> ChangeUriRpcMethod::process(const RpcRequest& req,
                                                       DownloadEngine* e)
{
  const String* gidParam = checkRequiredParam<String>(req, 0);
  const Integer* indexParam = checkRequiredInteger(req, 1, IntegerGE(1));
  const List* delUrisParam = checkRequiredParam<List>(req, 2);
  const List* addUrisParam = checkRequiredParam<List>(req, 3);
  const Integer* posParam = checkParam<Integer>(req, 4);

  a2_gid_t gid = str2Gid(gidParam);
  bool posGiven = checkPosParam(posParam);
  size_t pos = posGiven ? posParam->i() : 0;
  size_t index = indexParam->i() - 1;
  auto group = e->getRequestGroupMan()->findGroup(gid);
  if (!group) {
    throw DL_ABORT_EX(
        fmt("Cannot remove URIs from GID#%s", GroupId::toHex(gid).c_str()));
  }
  if (group->getMediaDownload()) {
    throw DL_ABORT_EX("Media presentations have one source URI; submit a new "
                      "task to change it");
  }
  auto& files = group->getDownloadContext()->getFileEntries();
  if (files.size() <= index) {
    throw DL_ABORT_EX(fmt("fileIndex is out of range"));
  }
  auto& s = files[index];
  size_t delcount = 0;
  for (auto& elem : *delUrisParam) {
    const String* uri = downcast<String>(elem);
    if (uri && s->removeUri(uri->s())) {
      ++delcount;
    }
  }
  size_t addcount = 0;
  if (posGiven) {
    for (auto& elem : *addUrisParam) {
      const String* uri = downcast<String>(elem);
      if (uri && s->insertUri(uri->s(), pos)) {
        ++addcount;
        ++pos;
      }
    }
  }
  else {
    for (auto& elem : *addUrisParam) {
      const String* uri = downcast<String>(elem);
      if (uri && s->addUri(uri->s())) {
        ++addcount;
      }
    }
  }
  if (addcount || delcount) {
    if (auto download = group->getCurlDownload()) {
      download->synchronizeUris(
          group->getDownloadContext()->getFirstFileEntry()->getUris());
    }
  }
  if (addcount && group->getPieceStorage()) {
    group->getSegmentMan()->recognizeSegmentFor(s);
  }
  auto res = List::g();
  res->append(Integer::g(delcount));
  res->append(Integer::g(addcount));
  return std::move(res);
}

} // namespace aria2::rpc
