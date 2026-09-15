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
#include <memory>
#include <string>
#include "SocketCore.h"
#include "platform/SocketOps.h"
#include "platform/SocketAddress.h"
#include "DlAbortEx.h"
#include "Log.h"
#include "message.h"
#include "prefs.h"
#include "fmt.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <sstream>

#ifdef ENABLE_SSL
#  include "TLSSession.h"
#  include "TLSContext.h"
#endif

namespace aria2 {

using namespace socket_ops;

#ifdef ENABLE_SSL
std::shared_ptr<TLSContext> SocketCore::svTlsContext_;
#endif

#ifdef ENABLE_SSL
void SocketCore::setServerTLSContext(
    const std::shared_ptr<TLSContext>& tlsContext)
{
  svTlsContext_ = tlsContext;
}

#endif // ENABLE_SSL

#ifdef ENABLE_SSL
bool SocketCore::tlsAccept() { return tlsHandshake(); }

#endif // ENABLE_SSL

#ifdef ENABLE_SSL
bool SocketCore::tlsHandshake()
{
  wantRead_ = false;
  wantWrite_ = false;

  if (secure_ == TlsState::Connected) {
    // Already connected!
    return true;
  }

  if (secure_ == TlsState::None) {
    // Do some initial setup
    A2_LOG_TRACE("Creating TLS session");
    tlsSession_.reset(TLSSession::make(svTlsContext_.get()));
    auto rv = tlsSession_->init(sockfd_);
    if (rv != TLS_ERR_OK) {
      std::string error = tlsSession_->getLastErrorString();
      tlsSession_.reset();
      throw DL_ABORT_EX(fmt(EX_SSL_INIT_FAILURE, error.c_str()));
    }
    // Done with the setup, now let handshaking begin immediately.
    secure_ = TlsState::Handshaking;
    A2_LOG_TRACE("TLS Handshaking");
  }

  if (secure_ == TlsState::Handshaking) {
    // Starting handshake after initial setup or still handshaking.
    TLSVersion ver = TLS_PROTO_NONE;
    const auto rv = tlsSession_->tlsAccept(ver);

    if (rv == TLS_ERR_OK) {
      // We're good, more or less.
      // 1. Construct peerinfo
      std::stringstream ss;
      auto peerEndpoint = getPeerInfo();
      ss << peerEndpoint.addr << ":" << peerEndpoint.port;

      std::string tlsVersion;
      switch (ver) {
      case TLS_PROTO_TLS11:
        tlsVersion = A2_V_TLS11;
        break;
      case TLS_PROTO_TLS12:
        tlsVersion = A2_V_TLS12;
        break;
      case TLS_PROTO_TLS13:
        tlsVersion = A2_V_TLS13;
        break;
      default:
        tlsVersion = "Unknown";
      }

      auto peerInfo = ss.str();

      A2_LOG_TRACE(fmt("Securely connected to %s with %s", peerInfo.c_str(),
                       tlsVersion.c_str()));

      // 2. We're connected now!
      secure_ = TlsState::Connected;
      return true;
    }

    if (rv == TLS_ERR_WOULDBLOCK) {
      // We're not done yet...
      if (tlsSession_->checkDirection() == TLS_WANT_READ) {
        // ... but read buffers are empty.
        wantRead_ = true;
      }
      else {
        // ... but write buffers are full.
        wantWrite_ = true;
      }
      // Returning false (instead of true==success or throwing) will cause this
      // function to be called again once buffering is dealt with
      return false;
    }

    if (rv == TLS_ERR_ERROR) {
      throw DL_ABORT_EX(fmt("SSL/TLS handshake failure: %s",
                            tlsSession_->getLastErrorString().c_str()));
    }

    // Some implementation passed back an invalid result.
    throw DL_ABORT_EX(fmt(EX_SSL_INIT_FAILURE,
                          "Invalid connect state (this is a bug in the TLS "
                          "backend!)"));
  }

  // We should never get here, i.e. all possible states should have been handled
  // and returned from a branch before! Getting here is a bug, of course!
  throw DL_ABORT_EX(fmt(EX_SSL_INIT_FAILURE, "Invalid state (this is a bug!)"));
}

#endif // ENABLE_SSL

size_t SocketCore::getRecvBufferedLength() const
{
#ifdef ENABLE_SSL
  if (!tlsSession_) {
    return 0;
  }

  return tlsSession_->getRecvBufferedLength();
#else  // !ENABLE_SSL
  return 0;
#endif // !ENABLE_SSL
}

} // namespace aria2
