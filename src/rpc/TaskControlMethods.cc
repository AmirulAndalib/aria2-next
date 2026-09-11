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
#include "ContextAttribute.h"
#include "GroupId.h"
#include "RpcRequest.h"
#include "ValueBase.h"
#include "aria2/aria2.h"
#include <chrono>
#include <cstddef>
#include <memory>
#include "common.h" // IWYU pragma: keep

#include "RpcMethods.h"
#include "RpcRequestHelpers.h"
#include "RpcFields.h"
#include "DownloadEngine.h"
#include "RequestGroup.h"
#include "support/Encoding.h"
#include "a2functional.h"
#include "fmt.h"
#include "DlAbortEx.h"
#include "RequestGroupActions.h"
#include "CurlDownload.h"
#include "CurlSession.h"
#include "DownloadContext.h"
#include "Ed2kSession.h"
#include "media/MediaDownload.h"
#ifdef ENABLE_BITTORRENT
#  include "BtDownload.h"
#  include "BtSession.h"
#  include "BtMetadata.h"
#endif

namespace aria2::rpc {

using namespace fields;
using namespace detail;

namespace {
std::unique_ptr<ValueBase> removeDownload(const RpcRequest& req,
                                          DownloadEngine* e, bool forceRemove)
{
  const String* gidParam = checkRequiredParam<String>(req, 0);

  a2_gid_t gid = str2Gid(gidParam);
  auto group = e->getRequestGroupMan()->findGroup(gid);
  if (group) {
    if (group->getState() == RequestGroup::STATE_ACTIVE) {
      if (forceRemove) {
        group->setForceHaltRequested(true, RequestGroup::USER_REQUEST);
      }
      else {
        group->setHaltRequested(true, RequestGroup::USER_REQUEST);
      }
      e->setRefreshInterval(std::chrono::milliseconds(0));
    }
    else {
      if (group->isDependencyResolved()) {
#ifdef ENABLE_BITTORRENT
        if (group->getBtDownload() && e->getBtSession()) {
          e->getBtSession()->discard(group->getBtDownload());
        }
#endif
        if (group->getCurlDownload() && e->getCurlSession()) {
          e->getCurlSession()->stop(group->getCurlDownload(), false);
        }
        if (group->getDownloadContext()->hasAttribute(CTX_ATTR_ED2K)) {
          e->getRequestGroupMan()->getEd2kSession()->discardDownload(
              group.get());
        }
        if (group->getMediaDownload())
          group->getMediaDownload()->stop(false);
        e->getRequestGroupMan()->removeReservedGroup(gid);
      }
      else {
        throw DL_ABORT_EX(
            fmt("GID#%s cannot be removed now", GroupId::toHex(gid).c_str()));
      }
    }
  }
  else {
    throw DL_ABORT_EX(fmt("Active Download not found for GID#%s",
                          GroupId::toHex(gid).c_str()));
  }
  return createGIDResponse(gid);
}
} // namespace

std::unique_ptr<ValueBase> RemoveRpcMethod::process(const RpcRequest& req,
                                                    DownloadEngine* e)
{
  return removeDownload(req, e, false);
}

std::unique_ptr<ValueBase> ForceRemoveRpcMethod::process(const RpcRequest& req,
                                                         DownloadEngine* e)
{
  return removeDownload(req, e, true);
}

namespace {
std::unique_ptr<ValueBase> pauseDownload(const RpcRequest& req,
                                         DownloadEngine* e, bool forcePause)
{
  const String* gidParam = checkRequiredParam<String>(req, 0);

  a2_gid_t gid = str2Gid(gidParam);
  auto group = e->getRequestGroupMan()->findGroup(gid);
  if (group) {
    bool reserved = group->getState() == RequestGroup::STATE_WAITING;
    if (pauseRequestGroup(group, reserved, forcePause)) {
      e->setRefreshInterval(std::chrono::milliseconds(0));
      return createGIDResponse(gid);
    }
  }
  throw DL_ABORT_EX(
      fmt("GID#%s cannot be paused now", GroupId::toHex(gid).c_str()));
}
} // namespace

std::unique_ptr<ValueBase> PauseRpcMethod::process(const RpcRequest& req,
                                                   DownloadEngine* e)
{
  return pauseDownload(req, e, false);
}

std::unique_ptr<ValueBase> ForcePauseRpcMethod::process(const RpcRequest& req,
                                                        DownloadEngine* e)
{
  return pauseDownload(req, e, true);
}

namespace {
template <typename InputIterator>
void pauseRequestGroups(InputIterator first, InputIterator last, bool reserved,
                        bool forcePause)
{
  for (; first != last; ++first) {
    pauseRequestGroup(*first, reserved, forcePause);
  }
}
} // namespace

namespace {
std::unique_ptr<ValueBase> pauseAllDownloads(const RpcRequest& req,
                                             DownloadEngine* e, bool forcePause)
{
  auto& groups = e->getRequestGroupMan()->getRequestGroups();
  pauseRequestGroups(groups.begin(), groups.end(), false, forcePause);
  auto& reservedGroups = e->getRequestGroupMan()->getReservedGroups();
  pauseRequestGroups(reservedGroups.begin(), reservedGroups.end(), true,
                     forcePause);
  return createOKResponse();
}
} // namespace

std::unique_ptr<ValueBase> PauseAllRpcMethod::process(const RpcRequest& req,
                                                      DownloadEngine* e)
{
  return pauseAllDownloads(req, e, false);
}

std::unique_ptr<ValueBase>
ForcePauseAllRpcMethod::process(const RpcRequest& req, DownloadEngine* e)
{
  return pauseAllDownloads(req, e, true);
}

std::unique_ptr<ValueBase> UnpauseRpcMethod::process(const RpcRequest& req,
                                                     DownloadEngine* e)
{
  const String* gidParam = checkRequiredParam<String>(req, 0);

  a2_gid_t gid = str2Gid(gidParam);
  auto group = e->getRequestGroupMan()->findGroup(gid);
  if (!group || group->getState() != RequestGroup::STATE_WAITING ||
      !group->isPauseRequested()) {
    throw DL_ABORT_EX(
        fmt("GID#%s cannot be unpaused now", GroupId::toHex(gid).c_str()));
  }
  else {
#ifdef ENABLE_BITTORRENT
    if (group->getBtDownload()) {
      group->getBtDownload()->beginFileSelectionApply();
    }
#endif
    group->setPauseRequested(false);
    e->getRequestGroupMan()->requestQueueCheck();
  }
  return createGIDResponse(gid);
}

std::unique_ptr<ValueBase> UnpauseAllRpcMethod::process(const RpcRequest& req,
                                                        DownloadEngine* e)
{
  auto& groups = e->getRequestGroupMan()->getReservedGroups();
#ifdef ENABLE_BITTORRENT
  for (const auto& group : groups) {
    if (group->getBtDownload() &&
        group->getBtDownload()->awaitingFileSelection()) {
      throw DL_ABORT_EX(fmt("GID#%s is awaiting a valid select-file option",
                            GroupId::toHex(group->getGID()).c_str()));
    }
  }
#endif // ENABLE_BITTORRENT
  for (auto& group : groups) {
#ifdef ENABLE_BITTORRENT
    if (group->getBtDownload()) {
      group->getBtDownload()->beginFileSelectionApply();
    }
#endif
    group->setPauseRequested(false);
  }
  e->getRequestGroupMan()->requestQueueCheck();
  return createOKResponse();
}

std::unique_ptr<ValueBase>
PurgeDownloadResultRpcMethod::process(const RpcRequest& req, DownloadEngine* e)
{
  e->getRequestGroupMan()->purgeDownloadResult();
  return createOKResponse();
}

std::unique_ptr<ValueBase>
RemoveDownloadResultRpcMethod::process(const RpcRequest& req, DownloadEngine* e)
{
  const String* gidParam = checkRequiredParam<String>(req, 0);

  a2_gid_t gid = str2Gid(gidParam);
  if (!e->getRequestGroupMan()->removeDownloadResult(gid)) {
    throw DL_ABORT_EX(fmt("Could not remove download result of GID#%s",
                          GroupId::toHex(gid).c_str()));
  }
  return createOKResponse();
}

std::unique_ptr<ValueBase>
ChangePositionRpcMethod::process(const RpcRequest& req, DownloadEngine* e)
{
  const String* gidParam = checkRequiredParam<String>(req, 0);
  const Integer* posParam = checkRequiredParam<Integer>(req, 1);
  const String* howParam = checkRequiredParam<String>(req, 2);

  a2_gid_t gid = str2Gid(gidParam);
  int pos = posParam->i();
  const std::string& howStr = howParam->s();
  OffsetMode how;
  if (howStr == "POS_SET") {
    how = OFFSET_MODE_SET;
  }
  else if (howStr == "POS_CUR") {
    how = OFFSET_MODE_CUR;
  }
  else if (howStr == "POS_END") {
    how = OFFSET_MODE_END;
  }
  else {
    throw DL_ABORT_EX("Illegal argument.");
  }
  size_t destPos =
      e->getRequestGroupMan()->changeReservedGroupPosition(gid, pos, how);
  return Integer::g(destPos);
}

std::unique_ptr<ValueBase> FinishMediaRpcMethod::process(const RpcRequest& req,
                                                         DownloadEngine* e)
{
  auto gid = str2Gid(checkRequiredParam<String>(req, 0));
  auto group = e->getRequestGroupMan()->findGroup(gid);
  if (!group || !group->getMediaDownload() ||
      !group->getMediaDownload()->finishRecording())
    throw DL_ABORT_EX("The task is not an active live recording");
  if (group->isPauseRequested()) {
    group->setPauseRequested(false);
    e->getRequestGroupMan()->requestQueueCheck();
    e->setRefreshInterval(std::chrono::milliseconds(0));
  }
  return createGIDResponse(gid);
}

} // namespace aria2::rpc

namespace aria2 {
bool pauseRequestGroup(const std::shared_ptr<RequestGroup>& group,
                       bool reserved, bool forcePause)
{
  if ((reserved && !group->isPauseRequested()) ||
      (!reserved && !group->isForceHaltRequested() &&
       ((forcePause && group->isHaltRequested() && group->isPauseRequested()) ||
        (!group->isHaltRequested() && !group->isPauseRequested())))) {
    if (!reserved) {
      // Call setHaltRequested before setPauseRequested because
      // setHaltRequested calls setPauseRequested(false) internally.
      if (forcePause) {
        group->setForceHaltRequested(true, RequestGroup::NONE);
      }
      else {
        group->setHaltRequested(true, RequestGroup::NONE);
      }
    }
    group->setPauseRequested(true);
    return true;
  }
  else {
    return false;
  }
}

} // namespace aria2

namespace aria2::rpc {

} // namespace aria2::rpc
