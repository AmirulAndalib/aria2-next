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
#include "RpcRequestHelpers.h"
#include "GroupId.h"
#include "ValueBase.h"
#include <memory>
#include "common.h" // IWYU pragma: keep

#include "DownloadEngine.h"
#include "RequestGroup.h"
#include "RequestGroupMan.h"
#include "DlAbortEx.h"
#include "fmt.h"
#include <cassert>

namespace aria2::rpc::detail {

std::unique_ptr<ValueBase> createGIDResponse(a2_gid_t gid)
{
  return String::g(GroupId::toHex(gid));
}

std::unique_ptr<ValueBase> createOKResponse() { return String::g("OK"); }

std::unique_ptr<ValueBase>
addRequestGroup(const std::shared_ptr<RequestGroup>& group, DownloadEngine* e,
                bool posGiven, int pos)
{
  if (posGiven) {
    e->getRequestGroupMan()->insertReservedGroup(pos, group);
  }
  else {
    e->getRequestGroupMan()->addReservedGroup(group);
  }
  return createGIDResponse(group->getGID());
}

bool checkPosParam(const Integer* posParam)
{
  if (posParam) {
    if (posParam->i() >= 0) {
      return true;
    }
    else {
      throw DL_ABORT_EX("Position must be greater than or equal to 0.");
    }
  }
  return false;
}

a2_gid_t str2Gid(const String* str)
{
  assert(str);
  if (str->s().size() > sizeof(a2_gid_t) * 2) {
    throw DL_ABORT_EX(fmt("Invalid GID %s", str->s().c_str()));
  }
  a2_gid_t n;
  switch (GroupId::expandUnique(n, str->s().c_str())) {
  case GroupId::ERR_NOT_UNIQUE:
    throw DL_ABORT_EX(fmt("GID %s is not unique", str->s().c_str()));
  case GroupId::ERR_NOT_FOUND:
    throw DL_ABORT_EX(fmt("GID %s is not found", str->s().c_str()));
  case GroupId::ERR_INVALID:
    throw DL_ABORT_EX(fmt("Invalid GID %s", str->s().c_str()));
  }
  return n;
}

} // namespace aria2::rpc::detail
