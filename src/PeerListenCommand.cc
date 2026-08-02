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
#include "PeerListenCommand.h"

#include <utility>

#include "DownloadEngine.h"
#include "Peer.h"
#include "RequestGroupMan.h"
#include "RecoverableException.h"
#include "message.h"
#include "ReceiverMSEHandshakeCommand.h"
#include "Log.h"
#include "SocketCore.h"
#include "fmt.h"
#include "BtRegistry.h"
#include "BtPeerBlocklist.h"
#include "BtPeerListener.h"

namespace aria2 {

PeerListenCommand::PeerListenCommand(
    cuid_t cuid, DownloadEngine* e, std::shared_ptr<BtPeerListener> listener)
    : Command(cuid), e_(e), listener_(std::move(listener))
{
}

PeerListenCommand::~PeerListenCommand() = default;

bool PeerListenCommand::execute()
{
  if (e_->isHaltRequested() || e_->getRequestGroupMan()->downloadFinished()) {
    const auto previousPort = listener_->port();
    listener_->close();
    e_->getBtRegistry()->onListenPortChanged(previousPort);
    return true;
  }
  for (const auto& listener : listener_->entries()) {
    for (int i = 0; i < 3 && listener.socket->isReadable(0); ++i) {
      std::shared_ptr<SocketCore> peerSocket;
      try {
        peerSocket = listener.socket->acceptConnection();
        peerSocket->applyIpDscp();
        auto endpoint = peerSocket->getPeerInfo();

        if (e_->getBtRegistry()->getPeerBlocklist()->contains(endpoint.addr)) {
          A2_LOG_DEBUG(fmt("Rejected blocked BitTorrent peer %s:%u.",
                           endpoint.addr.c_str(), endpoint.port));
          peerSocket->closeConnection();
          continue;
        }

        auto peer = std::make_shared<Peer>(
            endpoint.addr, endpoint.port, Peer::ConnectionDirection::INCOMING);
        cuid_t cuid = e_->newCUID();
        e_->addCommand(make_unique<ReceiverMSEHandshakeCommand>(
            cuid, peer, e_, peerSocket));
        A2_LOG_TRACE(fmt("Accepted the connection from %s:%u.",
                         peer->getIPAddress().c_str(), peer->getRemotePort()));
        A2_LOG_TRACE(fmt("Added CUID#%" PRId64
                         " to receive BitTorrent/MSE handshake.",
                         cuid));
      }
      catch (RecoverableException& ex) {
        A2_LOG_TRACE_EX(fmt(MSG_ACCEPT_FAILURE, getCuid()), ex);
      }
    }
  }
  e_->addCommand(std::unique_ptr<Command>(this));
  return false;
}

} // namespace aria2
