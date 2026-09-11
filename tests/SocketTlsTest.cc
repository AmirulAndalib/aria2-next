#include "TLSContext.h"
#include "a2netcompat.h"
#include <cstddef>
#include <openssl/asn1.h>
#include <openssl/crypto.h>
#include "common.h"

#if defined(ENABLE_SSL) && defined(HAVE_OPENSSL)
#  include "LibsslTLSContext.h"
#  include "SocketCore.h"
#  include "a2doctest.h"

#  include <openssl/err.h>
#  include <openssl/evp.h>
#  include <openssl/ssl.h>
#  include <openssl/x509.h>

#  include <array>
#  include <chrono>
#  include <memory>
#  include <string>
#  include <thread>

namespace aria2 {
namespace {

std::shared_ptr<OpenSSLTLSContext> createServerContext()
{
  auto context = std::make_shared<OpenSSLTLSContext>(TLS_PROTO_TLS12);
  REQUIRE(context->good());
  std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(
      EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", "prime256v1"), EVP_PKEY_free);
  std::unique_ptr<X509, decltype(&X509_free)> certificate(X509_new(),
                                                          X509_free);
  REQUIRE(key);
  REQUIRE(certificate);
  REQUIRE(X509_set_version(certificate.get(), 2) == 1);
  REQUIRE(ASN1_INTEGER_set(X509_get_serialNumber(certificate.get()), 1) == 1);
  REQUIRE(X509_gmtime_adj(X509_getm_notBefore(certificate.get()), -60));
  REQUIRE(X509_gmtime_adj(X509_getm_notAfter(certificate.get()), 3600));
  REQUIRE(X509_set_pubkey(certificate.get(), key.get()) == 1);
  auto subject = X509_get_subject_name(certificate.get());
  REQUIRE(X509_NAME_add_entry_by_txt(
              subject, "CN", MBSTRING_ASC,
              reinterpret_cast<const unsigned char*>("localhost"), -1, -1,
              0) == 1);
  REQUIRE(X509_set_issuer_name(certificate.get(), subject) == 1);
  REQUIRE(X509_sign(certificate.get(), key.get(), EVP_sha256()) > 0);
  REQUIRE(SSL_CTX_use_certificate(context->getSSLCtx(), certificate.get()) ==
          1);
  REQUIRE(SSL_CTX_use_PrivateKey(context->getSSLCtx(), key.get()) == 1);
  return context;
}

void completeHandshake(SocketCore& server, SSL* client)
{
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(5);
  bool clientReady = false;
  bool serverReady = false;
  while (!clientReady || !serverReady) {
    REQUIRE(std::chrono::steady_clock::now() < deadline);
    if (!clientReady) {
      ERR_clear_error();
      const auto result = SSL_connect(client);
      clientReady = result == 1;
      if (!clientReady) {
        const auto error = SSL_get_error(client, result);
        REQUIRE(
            (error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE));
      }
    }
    if (!serverReady) {
      serverReady = server.tlsAccept();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

} // namespace

TEST_CASE("SocketTls.writeVectorPreservesOrderAcrossPartialRecords")
{
  struct ResetServerContext {
    ~ResetServerContext() { SocketCore::setServerTLSContext(nullptr); }
  } reset;
  auto context = createServerContext();
  SocketCore::setServerTLSContext(context);
  SocketCore listener;
  listener.bind("127.0.0.1", 0, AF_INET);
  listener.beginListen();
  SocketCore transport;
  transport.establishConnection("127.0.0.1", listener.getAddrInfo().port);
  REQUIRE(listener.isReadable(5));
  auto server = listener.acceptConnection();
  std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> clientContext(
      SSL_CTX_new(TLS_client_method()), SSL_CTX_free);
  REQUIRE(clientContext);
  std::unique_ptr<SSL, decltype(&SSL_free)> client(SSL_new(clientContext.get()),
                                                   SSL_free);
  REQUIRE(client);
  REQUIRE(SSL_set_fd(client.get(), static_cast<int>(transport.getSockfd())) ==
          1);
  completeHandshake(*server, client.get());

  // The native server enables partial writes. The first buffer exceeds one
  // TLS record; the second must remain queued until the first is exhausted.
  std::string first(32768, 'a');
  std::string second = "trailer";
  std::array<a2iovec, 2> buffers{};
  buffers[0].A2IOVEC_BASE = first.data();
  buffers[0].A2IOVEC_LEN = first.size();
  buffers[1].A2IOVEC_BASE = second.data();
  buffers[1].A2IOVEC_LEN = second.size();
  const auto written = server->writeVector(buffers.data(), buffers.size());
  REQUIRE(written > 0);
  REQUIRE(static_cast<size_t>(written) < first.size());

  std::string received;
  std::array<char, 4096> block{};
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (received.size() < static_cast<size_t>(written)) {
    REQUIRE(std::chrono::steady_clock::now() < deadline);
    ERR_clear_error();
    const auto count = SSL_read(client.get(), block.data(), block.size());
    if (count > 0) {
      received.append(block.data(), count);
    }
    else {
      const auto error = SSL_get_error(client.get(), count);
      REQUIRE((error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE));
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
  REQUIRE(received.compare(0, received.size(), first, 0, written) == 0);
}

} // namespace aria2
#endif // ENABLE_SSL && HAVE_OPENSSL
