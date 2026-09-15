/* <!-- copyright */
/*
 * aria2 - The high speed download utility
 *
 * Copyright (C) 2013 Tatsuhiro Tsujikawa
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
#include "LibsslTLSSession.h"
#include "OpenSslDiagnostics.h"

#include <openssl/err.h>
namespace aria2 {

TLSSession* TLSSession::make(TLSContext* ctx)
{
  return new OpenSSLTLSSession(static_cast<OpenSSLTLSContext*>(ctx));
}

OpenSSLTLSSession::OpenSSLTLSSession(OpenSSLTLSContext* tlsContext)
    : ssl_(nullptr), tlsContext_(tlsContext), rv_(1)
{
}

OpenSSLTLSSession::~OpenSSLTLSSession()
{
  if (ssl_) {
    SSL_free(ssl_);
  }
}

int OpenSSLTLSSession::init(sock_t sockfd)
{
  ERR_clear_error();
  ssl_ = SSL_new(tlsContext_->getSSLCtx());
  if (!ssl_) {
    return TLS_ERR_ERROR;
  }
  rv_ = SSL_set_fd(ssl_, sockfd);
  if (rv_ == 0) {
    return TLS_ERR_ERROR;
  }
  SSL_set_accept_state(ssl_);
  return TLS_ERR_OK;
}

int OpenSSLTLSSession::closeConnection()
{
  ERR_clear_error();
  SSL_shutdown(ssl_);
  // TODO handle return value
  return TLS_ERR_OK;
}

int OpenSSLTLSSession::checkDirection()
{
  int error = SSL_get_error(ssl_, rv_);
  if (error == SSL_ERROR_WANT_WRITE) {
    return TLS_WANT_WRITE;
  }
  else {
    // TODO We ignore error other than SSL_ERR_WANT_READ here for now
    return TLS_WANT_READ;
  }
}

namespace {
bool wouldblock(SSL* ssl, int rv)
{
  int error = SSL_get_error(ssl, rv);
  return error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE;
}
} // namespace

ssize_t OpenSSLTLSSession::writeData(const void* data, size_t len)
{
  ERR_clear_error();
  rv_ = SSL_write(ssl_, data, len);
  if (rv_ <= 0) {
    if (wouldblock(ssl_, rv_)) {
      return TLS_ERR_WOULDBLOCK;
    }
    else {
      return TLS_ERR_ERROR;
    }
  }
  else {
    ssize_t ret = rv_;
    rv_ = 1;
    return ret;
  }
}

ssize_t OpenSSLTLSSession::readData(void* data, size_t len)
{
  ERR_clear_error();
  rv_ = SSL_read(ssl_, data, len);
  if (rv_ <= 0) {
    if (wouldblock(ssl_, rv_)) {
      return TLS_ERR_WOULDBLOCK;
    }

    if (rv_ == 0) {
      auto err = SSL_get_error(ssl_, rv_);

      if (err == SSL_ERROR_ZERO_RETURN) {
        return 0;
      }
    }

    return TLS_ERR_ERROR;
  }

  ssize_t ret = rv_;
  rv_ = 1;
  return ret;
}

int OpenSSLTLSSession::tlsAccept(TLSVersion& version)
{
  ERR_clear_error();
  rv_ = SSL_accept(ssl_);
  if (rv_ <= 0) {
    int sslError = SSL_get_error(ssl_, rv_);
    switch (sslError) {
    case SSL_ERROR_NONE:
    case SSL_ERROR_WANT_X509_LOOKUP:
    case SSL_ERROR_ZERO_RETURN:
      // TODO Now assume we are doing non-blocking. Then above 2
      // errors are OK.
      break;
    case SSL_ERROR_WANT_READ:
    case SSL_ERROR_WANT_WRITE:
      return TLS_ERR_WOULDBLOCK;
    default:
      return TLS_ERR_ERROR;
    }
  }

  switch (SSL_version(ssl_)) {
#ifdef TLS1_1_VERSION
  case TLS1_1_VERSION:
    version = TLS_PROTO_TLS11;
    break;
#endif // TLS1_1_VERSION

#ifdef TLS1_2_VERSION
  case TLS1_2_VERSION:
    version = TLS_PROTO_TLS12;
    break;
#endif // TLS1_2_VERSION

#ifdef TLS1_3_VERSION
  case TLS1_3_VERSION:
    version = TLS_PROTO_TLS13;
    break;
#endif // TLS1_3_VERSION

  default:
    version = TLS_PROTO_NONE;
    break;
  }

  return TLS_ERR_OK;
}

size_t OpenSSLTLSSession::getRecvBufferedLength()
{
  return ssl_ ? static_cast<size_t>(SSL_pending(ssl_)) : 0;
}

std::string OpenSSLTLSSession::getLastErrorString()
{
  if (rv_ <= 0) {
    int sslError = SSL_get_error(ssl_, rv_);
    switch (sslError) {
    case SSL_ERROR_NONE:
    case SSL_ERROR_WANT_READ:
    case SSL_ERROR_WANT_WRITE:
    case SSL_ERROR_WANT_X509_LOOKUP:
    case SSL_ERROR_ZERO_RETURN:
      return "";
    case SSL_ERROR_SYSCALL: {
      if (ERR_peek_error() == 0) {
        if (rv_ == 0) {
          return "EOF was received";
        }
        else if (rv_ == -1) {
          return "SSL I/O error";
        }
        else {
          return "unknown syscall error";
        }
      }
      else {
        return openssl::errorStack();
      }
    }
    case SSL_ERROR_SSL:
      if (const auto verifyResult = SSL_get_verify_result(ssl_);
          verifyResult != X509_V_OK) {
        return X509_verify_cert_error_string(verifyResult);
      }
      if (ERR_peek_error() != 0) {
        return openssl::errorStack();
      }
      return "TLS protocol error";
    default:
      return "unknown error";
    }
  }
  else {
    return "";
  }
}

} // namespace aria2
