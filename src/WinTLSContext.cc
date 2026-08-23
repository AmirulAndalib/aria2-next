/* <!-- copyright */
/*
 * aria2 - The high speed download utility
 *
 * Copyright (C) 2013 Nils Maier
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

#include "WinTLSContext.h"

#include <cassert>
#include <sstream>

#include "BufferedFile.h"
#include "Log.h"
#include "fmt.h"
#include "message.h"
#include "util.h"

namespace aria2 {

WinTLSContext::WinTLSContext(TLSSessionSide side, TLSVersion ver)
    : side_(side), store_(0)
{
  memset(&credentials_, 0, sizeof(credentials_));
  memset(&tlsParameters_, 0, sizeof(tlsParameters_));
  credentials_.dwVersion = SCH_CREDENTIALS_VERSION;
  credentials_.cTlsParameters = 1;
  credentials_.pTlsParameters = &tlsParameters_;
  tlsParameters_.grbitDisabledProtocols =
      side_ == TLS_CLIENT ? SP_PROT_TLS1_0_CLIENT : SP_PROT_TLS1_0_SERVER;
  if (side_ == TLS_CLIENT) {
    switch (ver) {
    case TLS_PROTO_TLS11:
      break;
    case TLS_PROTO_TLS12:
      tlsParameters_.grbitDisabledProtocols |= SP_PROT_TLS1_1_CLIENT;
      break;
    case TLS_PROTO_TLS13:
      tlsParameters_.grbitDisabledProtocols |=
          SP_PROT_TLS1_1_CLIENT | SP_PROT_TLS1_2_CLIENT;
      break;
    default:
      assert(0);
      abort();
    }
  }
  else {
    switch (ver) {
    case TLS_PROTO_TLS11:
      break;
    case TLS_PROTO_TLS12:
      tlsParameters_.grbitDisabledProtocols |= SP_PROT_TLS1_1_SERVER;
      break;
    case TLS_PROTO_TLS13:
      tlsParameters_.grbitDisabledProtocols |=
          SP_PROT_TLS1_1_SERVER | SP_PROT_TLS1_2_SERVER;
      break;
    default:
      assert(0);
      abort();
    }
  }

  setVerifyPeer(side_ == TLS_CLIENT);
}

TLSContext* TLSContext::make(TLSSessionSide side, TLSVersion ver)
{
  return new WinTLSContext(side, ver);
}

WinTLSContext::~WinTLSContext()
{
  if (store_) {
    ::CertCloseStore(store_, 0);
    store_ = 0;
  }
}

void WinTLSContext::setVerifyPeer(bool verify)
{
  cred_.reset();

  // Never automatically push any client or server certs. We'll do cert setup
  // ourselves.
  credentials_.dwFlags = SCH_CRED_NO_DEFAULT_CREDS | SCH_USE_STRONG_CRYPTO;

  if (side_ != TLS_CLIENT || !verify) {
    // No verification for servers and if user explicitly requested it
    credentials_.dwFlags |=
        SCH_CRED_MANUAL_CRED_VALIDATION | SCH_CRED_IGNORE_NO_REVOCATION_CHECK |
        SCH_CRED_IGNORE_REVOCATION_OFFLINE | SCH_CRED_NO_SERVERNAME_CHECK;
    return;
  }

  // Verify the complete peer chain without making revocation service
  // availability a prerequisite for a valid TLS connection.
  credentials_.dwFlags |= SCH_CRED_AUTO_CRED_VALIDATION |
                          SCH_CRED_REVOCATION_CHECK_CHAIN |
                          SCH_CRED_IGNORE_NO_REVOCATION_CHECK |
                          SCH_CRED_IGNORE_REVOCATION_OFFLINE;
}

CredHandle* WinTLSContext::getCredHandle()
{
  if (cred_) {
    return cred_.get();
  }

  TimeStamp ts;
  cred_.reset(new CredHandle());

  const CERT_CONTEXT* ctx = nullptr;
  if (store_) {
    ctx = ::CertEnumCertificatesInStore(store_, nullptr);
    if (!ctx) {
      throw DL_ABORT_EX("Failed to load certificate");
    }
    credentials_.cCreds = 1;
    credentials_.paCred = &ctx;
  }
  else {
    credentials_.cCreds = 0;
    credentials_.paCred = nullptr;
  }
  SECURITY_STATUS status = ::AcquireCredentialsHandleW(
      nullptr, const_cast<SEC_WCHAR*>(UNISP_NAME_W),
      side_ == TLS_CLIENT ? SECPKG_CRED_OUTBOUND : SECPKG_CRED_INBOUND, nullptr,
      &credentials_, nullptr, nullptr, cred_.get(), &ts);
  if (ctx) {
    ::CertFreeCertificateContext(ctx);
  }
  if (status != SEC_E_OK) {
    cred_.reset();
    throw DL_ABORT_EX("Failed to initialize WinTLS context handle");
  }
  return cred_.get();
}

bool WinTLSContext::addCredentialFile(const std::string& certfile,
                                      const std::string& keyfile)
{
  std::stringstream ss;
  BufferedFile(certfile.c_str(), "rb").transfer(ss);
  auto data = ss.str();
  CRYPT_DATA_BLOB blob = {(DWORD)data.length(), (BYTE*)data.c_str()};
  if (!::PFXIsPFXBlob(&blob)) {
    A2_LOG_ERROR("Not a valid PKCS12 file");
    return false;
  }
  HCERTSTORE store =
      ::PFXImportCertStore(&blob, L"", CRYPT_EXPORTABLE | CRYPT_USER_KEYSET);
  if (!store_) {
    store = ::PFXImportCertStore(&blob, nullptr,
                                 CRYPT_EXPORTABLE | CRYPT_USER_KEYSET);
  }
  if (!store) {
    A2_LOG_ERROR("Failed to import PKCS12 store");
    return false;
  }
  auto ctx = ::CertEnumCertificatesInStore(store, nullptr);
  if (!ctx) {
    A2_LOG_ERROR("PKCS12 file does not contain certificates");
    ::CertCloseStore(store, 0);
    return false;
  }
  ::CertFreeCertificateContext(ctx);

  if (store_) {
    ::CertCloseStore(store_, 0);
  }
  store_ = store;
  cred_.reset();

  return true;
}

bool WinTLSContext::configurePeerVerification(
    TLSVerification verification, const std::string& caFile)
{
  if (verification == TLSVerification::CustomCA) {
    A2_LOG_ERROR(fmt("Custom TLS CA file is not supported by WinTLS: %s",
                     caFile.c_str()));
    return false;
  }
  setVerifyPeer(verification != TLSVerification::Disabled);
  return true;
}

} // namespace aria2
