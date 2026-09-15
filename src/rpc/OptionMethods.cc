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
#include "GroupId.h"
#include "RpcRequest.h"
#include "ValueBase.h"
#include <chrono>
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
#include "prefs.h"
#include "RequestGroupActions.h"
#include "Option.h"
#include "OptionParser.h"
#include "OptionHandler.h"
#include "LegacyInputAdapter.h"

namespace aria2::rpc {

using namespace detail;

std::unique_ptr<ValueBase> ChangeOptionRpcMethod::process(const RpcRequest& req,
                                                          DownloadEngine* e)
{
  const String* gidParam = checkRequiredParam<String>(req, 0);
  const Dict* optsParam = checkRequiredParam<Dict>(req, 1);

  a2_gid_t gid = str2Gid(gidParam);
  auto group = e->getRequestGroupMan()->findGroup(gid);
  if (group) {
    Option option;
    std::shared_ptr<Option> pendingOption;
    if (group->getState() == RequestGroup::STATE_ACTIVE) {
      pendingOption = std::make_shared<Option>();
      gatherChangeableOption(&option, pendingOption.get(), optsParam);
      if (!pendingOption->emptyLocal()) {
        group->setPendingOption(pendingOption);
        // pauseRequestGroup() may fail if group has been told to
        // stop/pause already.  In that case, we can still apply the
        // pending options on pause.
        if (pauseRequestGroup(group, false, false)) {
          group->setRestartRequested(true);
          e->setRefreshInterval(std::chrono::milliseconds(0));
        }
      }
    }
    else {
      gatherChangeableOptionForReserved(&option, optsParam);
    }
    changeOption(group, option, e);
  }
  else {
    throw DL_ABORT_EX(
        fmt("Cannot change option for GID#%s", GroupId::toHex(gid).c_str()));
  }
  return createOKResponse();
}

std::unique_ptr<ValueBase>
ChangeGlobalOptionRpcMethod::process(const RpcRequest& req, DownloadEngine* e)
{
  const Dict* optsParam = checkRequiredParam<Dict>(req, 0);

  Option option;
  gatherChangeableGlobalOption(&option, optsParam);
  changeGlobalOption(option, e);
  return createOKResponse();
}

namespace {
void pushRequestOption(Dict* dict, const std::shared_ptr<Option>& option,
                       const std::shared_ptr<OptionParser>& oparser)
{
  for (size_t i = 1, len = option::countOption(); i < len; ++i) {
    PrefPtr pref = option::i2p(i);
    const OptionHandler* h = oparser->find(pref);
    if (h && h->getInitialOption() && option->defined(pref)) {
      dict->put(pref->k, option->get(pref));
    }
  }
  for (const auto& item : projectLegacyOptions(option.get())) {
    dict->put(item.first, item.second);
  }
}
} // namespace

std::unique_ptr<ValueBase> GetOptionRpcMethod::process(const RpcRequest& req,
                                                       DownloadEngine* e)
{
  const String* gidParam = checkRequiredParam<String>(req, 0);

  a2_gid_t gid = str2Gid(gidParam);
  auto group = e->getRequestGroupMan()->findGroup(gid);
  auto result = Dict::g();
  if (!group) {
    auto dr = e->getRequestGroupMan()->findDownloadResult(gid);
    if (!dr) {
      throw DL_ABORT_EX(
          fmt("Cannot get option for GID#%s", GroupId::toHex(gid).c_str()));
    }
    pushRequestOption(result.get(), dr->option, getOptionParser());
  }
  else {
    pushRequestOption(result.get(), group->getOption(), getOptionParser());
  }
  return std::move(result);
}

std::unique_ptr<ValueBase>
GetGlobalOptionRpcMethod::process(const RpcRequest& req, DownloadEngine* e)
{
  auto result = Dict::g();
  for (size_t i = 0, len = e->getOption()->getTable().size(); i < len; ++i) {
    PrefPtr pref = option::i2p(i);
    if (pref == PREF_RPC_SECRET || !e->getOption()->defined(pref)) {
      continue;
    }
    const OptionHandler* h = getOptionParser()->find(pref);
    if (h) {
      result->put(pref->k, e->getOption()->get(pref));
    }
  }
  for (const auto& item : projectLegacyOptions(e->getOption())) {
    result->put(item.first, item.second);
  }
  return std::move(result);
}

} // namespace aria2::rpc
