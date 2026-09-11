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
#include "FileEntry.h"
#include "DownloadContext.h"
#include "TransferStat.h"
#include "uri_split.h"
#include <chrono>
#include <cstddef>
#include <iterator>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
#include "RequestGroupMan.h"
#include "ServerStatMan.h"
#include "ServerStat.h"
#include "uri.h"
#include <algorithm>
#include <cassert>

namespace aria2 {

TransferStat RequestGroupMan::calculateStat()
{
  auto stat = netStat_.toTransferStat();
  stat.downloadSpeed = 0;
  stat.uploadSpeed = 0;
  for (const auto& group : requestGroups_) {
    const auto task = group->calculateStat();
    TransferStat active;
    active.downloadSpeed = task.downloadSpeed;
    active.uploadSpeed = task.uploadSpeed;
    stat.add(active);
  }
  return stat;
}

std::shared_ptr<ServerStat>
RequestGroupMan::findServerStat(const std::string& hostname,
                                const std::string& protocol) const
{
  return serverStatMan_->find(hostname, protocol);
}

std::shared_ptr<ServerStat>
RequestGroupMan::getOrCreateServerStat(const std::string& hostname,
                                       const std::string& protocol)
{
  std::shared_ptr<ServerStat> ss = findServerStat(hostname, protocol);
  if (!ss) {
    ss = std::make_shared<ServerStat>(hostname, protocol);
    addServerStat(ss);
  }
  return ss;
}

bool RequestGroupMan::addServerStat(
    const std::shared_ptr<ServerStat>& serverStat)
{
  return serverStatMan_->add(serverStat);
}

bool RequestGroupMan::loadServerStat(const std::string& filename)
{
  return serverStatMan_->load(filename);
}

bool RequestGroupMan::saveServerStat(const std::string& filename) const
{
  return serverStatMan_->save(filename);
}

void RequestGroupMan::removeStaleServerStat(const std::chrono::seconds& timeout)
{
  serverStatMan_->removeStaleServerStat(timeout);
}

bool RequestGroupMan::doesOverallDownloadSpeedExceed()
{
  return maxOverallDownloadSpeedLimit_ > 0 &&
         maxOverallDownloadSpeedLimit_ < calculateStat().downloadSpeed;
}

bool RequestGroupMan::doesOverallUploadSpeedExceed()
{
  return maxOverallUploadSpeedLimit_ > 0 &&
         maxOverallUploadSpeedLimit_ < calculateStat().uploadSpeed;
}

void RequestGroupMan::getUsedHosts(
    std::vector<std::pair<size_t, std::string>>& usedHosts)
{
  // vector of tuple which consists of use count, -download speed,
  // hostname. We want to sort by least used and faster download
  // speed. We use -download speed so that we can sort them using
  // operator<().
  std::vector<std::tuple<size_t, int, std::string>> tempHosts;
  for (const auto& rg : requestGroups_) {
    const auto& inFlightReqs =
        rg->getDownloadContext()->getFirstFileEntry()->getInFlightRequests();
    for (const auto& req : inFlightReqs) {
      uri_split_result us;
      if (uri_split(&us, req->getUri().c_str()) == 0) {
        std::string host =
            uri::getFieldString(us, USR_HOST, req->getUri().c_str());
        auto k = tempHosts.begin();
        auto eok = tempHosts.end();
        for (; k != eok; ++k) {
          if (std::get<2>(*k) == host) {
            ++std::get<0>(*k);
            break;
          }
        }
        if (k == eok) {
          std::string protocol =
              uri::getFieldString(us, USR_SCHEME, req->getUri().c_str());
          auto ss = findServerStat(host, protocol);
          int invDlSpeed = (ss && ss->isOK())
                               ? -(static_cast<int>(ss->getDownloadSpeed()))
                               : 0;
          tempHosts.emplace_back(1, invDlSpeed, host);
        }
      }
    }
  }
  std::sort(tempHosts.begin(), tempHosts.end());
  std::transform(tempHosts.begin(), tempHosts.end(),
                 std::back_inserter(usedHosts),
                 [](const std::tuple<size_t, int, std::string>& x) {
                   return std::make_pair(std::get<0>(x), std::get<2>(x));
                 });
}

} // namespace aria2
