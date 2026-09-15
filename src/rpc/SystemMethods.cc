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
#include <cassert>
#include <memory>
#include <utility>
#include "common.h" // IWYU pragma: keep

#include "RpcMethods.h"
#include "RpcRequestHelpers.h"
#include "RpcFields.h"
#include "DownloadEngine.h"
#include "RequestGroup.h"
#include "fmt.h"
#include "message.h"
#include "DlAbortEx.h"
#include "RpcMethodFactory.h"
#include "RpcResponse.h"
#include "Log.h"

namespace aria2::rpc {

using namespace fields;
using namespace detail;

std::unique_ptr<ValueBase>
SystemMulticallRpcMethod::process(const RpcRequest& req, DownloadEngine* e)
{
  // Should never get here, since SystemMulticallRpcMethod overrides execute().
  assert(false);
  return nullptr;
}

RpcResponse SystemMulticallRpcMethod::execute(RpcRequest req, DownloadEngine* e)
{
  auto authorized = RpcResponse::AUTHORIZED;
  try {
    const List* methodSpecs = checkRequiredParam<List>(req, 0);
    auto list = List::g();
    for (auto& methodSpec : *methodSpecs) {
      Dict* methodDict = downcast<Dict>(methodSpec);
      if (!methodDict) {
        list->append(createErrorResponse(
            DL_ABORT_EX("system.multicall expected struct."), req));
        continue;
      }
      const String* methodName =
          downcast<String>(methodDict->get(KEY_METHOD_NAME));
      if (!methodName) {
        list->append(
            createErrorResponse(DL_ABORT_EX("Missing methodName."), req));
        continue;
      }
      if (methodName->s() == getMethodName()) {
        list->append(createErrorResponse(
            DL_ABORT_EX("Recursive system.multicall forbidden."), req));
        continue;
      }
      // TODO what if params missing?
      auto tempParamsList = methodDict->get(KEY_PARAMS);
      std::unique_ptr<List> paramsList;
      if (downcast<List>(tempParamsList)) {
        paramsList.reset(
            static_cast<List*>(methodDict->popValue(KEY_PARAMS).release()));
      }
      else {
        paramsList = List::g();
      }
      RpcRequest r = {methodName->s(), std::move(paramsList), nullptr,
                      req.jsonRpc};
      RpcResponse res = getMethod(methodName->s())->execute(std::move(r), e);
      if (rpc::not_authorized(res)) {
        authorized = RpcResponse::NOTAUTHORIZED;
      }
      if (res.code == 0) {
        auto l = List::g();
        l->append(std::move(res.param));
        list->append(std::move(l));
      }
      else {
        list->append(std::move(res.param));
      }
    }
    return RpcResponse(0, authorized, std::move(list), std::move(req.id));
  }
  catch (RecoverableException& ex) {
    A2_LOG_TRACE_EX(EX_EXCEPTION_CAUGHT, ex);
    return RpcResponse(1, authorized, createErrorResponse(ex, req),
                       std::move(req.id));
  }
}

std::unique_ptr<ValueBase>
SystemListMethodsRpcMethod::process(const RpcRequest& req, DownloadEngine* e)
{
  auto list = List::g();
  for (auto& s : allMethodNames()) {
    list->append(s);
  }

  return std::move(list);
}

RpcResponse SystemListMethodsRpcMethod::execute(RpcRequest req,
                                                DownloadEngine* e)
{
  auto r = process(req, e);
  return RpcResponse(0, RpcResponse::AUTHORIZED, std::move(r),
                     std::move(req.id));
}

std::unique_ptr<ValueBase>
SystemListNotificationsRpcMethod::process(const RpcRequest& req,
                                          DownloadEngine* e)
{
  auto list = List::g();
  for (auto& s : allNotificationsNames()) {
    list->append(s);
  }

  return std::move(list);
}

RpcResponse SystemListNotificationsRpcMethod::execute(RpcRequest req,
                                                      DownloadEngine* e)
{
  auto r = process(req, e);
  return RpcResponse(0, RpcResponse::AUTHORIZED, std::move(r),
                     std::move(req.id));
}

std::unique_ptr<ValueBase> NoSuchMethodRpcMethod::process(const RpcRequest& req,
                                                          DownloadEngine* e)
{
  throw DL_ABORT_EX(fmt("No such method: %s", req.methodName.c_str()));
}

} // namespace aria2::rpc
