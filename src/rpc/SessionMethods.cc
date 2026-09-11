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
#include "gai_strerror.h"
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
#include "a2functional.h"
#include "fmt.h"
#include "DlAbortEx.h"
#include "prefs.h"
#include "Option.h"
#include "FeatureConfig.h"
#include "TimedHaltCommand.h"
#include "SessionSerializer.h"
#include "Log.h"

namespace aria2::rpc {

using namespace fields;
using namespace detail;

std::unique_ptr<ValueBase> GetVersionRpcMethod::process(const RpcRequest& req,
                                                        DownloadEngine* e)
{
  auto result = Dict::g();
  result->put(KEY_PRODUCT, PRODUCT_NAME);
  result->put(KEY_VERSION, PACKAGE_VERSION);
  result->put(KEY_RPC_VERSION, RPC_VERSION);
  auto featureList = List::g();
  for (int feat = 0; feat < MAX_FEATURE; ++feat) {
    const char* name = strSupportedFeature(feat);
    if (name) {
      featureList->append(name);
    }
  }
  result->put(KEY_ENABLED_FEATURES, std::move(featureList));
  return std::move(result);
}

std::unique_ptr<ValueBase>
GetSessionInfoRpcMethod::process(const RpcRequest& req, DownloadEngine* e)
{
  auto result = Dict::g();
  result->put(KEY_SESSION_ID, util::toHex(e->getSessionId()));
  return std::move(result);
}

namespace {
std::unique_ptr<ValueBase> goingShutdown(const RpcRequest& req,
                                         DownloadEngine* e, bool forceHalt)
{
  // Schedule shutdown after 3seconds to give time to client to
  // receive RPC response.
  e->addRoutineCommand(
      make_unique<TimedHaltCommand>(e->newCUID(), e, 3_s, forceHalt));
  A2_LOG_DEBUG("Scheduled shutdown in 3 seconds.");
  return createOKResponse();
}
} // namespace

std::unique_ptr<ValueBase> ShutdownRpcMethod::process(const RpcRequest& req,
                                                      DownloadEngine* e)
{
  return goingShutdown(req, e, false);
}

std::unique_ptr<ValueBase>
ForceShutdownRpcMethod::process(const RpcRequest& req, DownloadEngine* e)
{
  return goingShutdown(req, e, true);
}

std::unique_ptr<ValueBase>
GetGlobalStatRpcMethod::process(const RpcRequest& req, DownloadEngine* e)
{
  auto& rgman = e->getRequestGroupMan();
  auto ts = rgman->calculateStat();
  auto res = Dict::g();
  res->put(KEY_DOWNLOAD_SPEED, util::itos(ts.downloadSpeed));
  res->put(KEY_UPLOAD_SPEED, util::itos(ts.uploadSpeed));
  res->put(KEY_NUM_WAITING, util::uitos(rgman->getReservedGroups().size()));
  res->put(KEY_NUM_STOPPED, util::uitos(rgman->getDownloadResults().size()));
  res->put(KEY_NUM_STOPPED_TOTAL, util::uitos(rgman->getNumStoppedTotal()));
  res->put(KEY_NUM_ACTIVE, util::uitos(rgman->getRequestGroups().size()));
  return std::move(res);
}

std::unique_ptr<ValueBase> SaveSessionRpcMethod::process(const RpcRequest& req,
                                                         DownloadEngine* e)
{
  const std::string& filename = e->getOption()->get(PREF_SAVE_SESSION);
  if (filename.empty()) {
    throw DL_ABORT_EX("Filename is not given.");
  }
  SessionSerializer sessionSerializer(e->getRequestGroupMan().get());
  if (sessionSerializer.save(filename)) {
    A2_LOG_DEBUG(
        fmt(_("Serialized session to '%s' successfully."), filename.c_str()));
    return createOKResponse();
  }
  throw DL_ABORT_EX(
      fmt("Failed to serialize session to '%s'.", filename.c_str()));
}

} // namespace aria2::rpc
