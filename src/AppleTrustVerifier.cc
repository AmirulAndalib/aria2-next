#include "AppleTrustVerifier.h"

#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#include <memory>
#include <type_traits>
#include <vector>

namespace aria2 {
namespace {

template <typename T> struct CFReleaser {
  void operator()(T value) const
  {
    if (value) {
      CFRelease(value);
    }
  }
};

template <typename T>
using CFPointer = std::unique_ptr<typename std::remove_pointer<T>::type,
                                  CFReleaser<T>>;

std::string toString(CFStringRef value)
{
  if (!value) {
    return {};
  }
  auto size = CFStringGetMaximumSizeForEncoding(CFStringGetLength(value),
                                                 kCFStringEncodingUTF8) +
              1;
  std::vector<char> buffer(static_cast<size_t>(size));
  if (!CFStringGetCString(value, buffer.data(), size, kCFStringEncodingUTF8)) {
    return {};
  }
  return buffer.data();
}

} // namespace

bool verifyAppleSystemTrust(SSL* ssl, const std::string& hostname,
                            std::string& error)
{
  auto chain = SSL_get_peer_cert_chain(ssl);
  if (!chain || sk_X509_num(chain) == 0) {
    error = "server certificate chain is empty";
    return false;
  }

  CFPointer<CFMutableArrayRef> certificates(
      CFArrayCreateMutable(kCFAllocatorDefault, sk_X509_num(chain),
                           &kCFTypeArrayCallBacks));
  if (!certificates) {
    error = "failed to allocate certificate chain";
    return false;
  }

  for (int i = 0; i < sk_X509_num(chain); ++i) {
    auto certificate = sk_X509_value(chain, i);
    auto length = i2d_X509(certificate, nullptr);
    if (length <= 0) {
      error = "failed to encode server certificate";
      return false;
    }
    std::vector<unsigned char> der(static_cast<size_t>(length));
    auto output = der.data();
    if (i2d_X509(certificate, &output) != length) {
      error = "failed to encode server certificate";
      return false;
    }
    CFPointer<CFDataRef> data(CFDataCreate(kCFAllocatorDefault, der.data(),
                                           static_cast<CFIndex>(der.size())));
    CFPointer<SecCertificateRef> secCertificate(
        data ? SecCertificateCreateWithData(kCFAllocatorDefault, data.get())
             : nullptr);
    if (!secCertificate) {
      error = "failed to import server certificate";
      return false;
    }
    CFArrayAppendValue(certificates.get(), secCertificate.get());
  }

  CFPointer<CFStringRef> host(CFStringCreateWithCString(
      kCFAllocatorDefault, hostname.c_str(), kCFStringEncodingUTF8));
  CFPointer<SecPolicyRef> policy(
      host ? SecPolicyCreateSSL(true, host.get()) : nullptr);
  if (!policy) {
    error = "failed to create TLS trust policy";
    return false;
  }

  SecTrustRef rawTrust = nullptr;
  auto status = SecTrustCreateWithCertificates(certificates.get(), policy.get(),
                                                &rawTrust);
  CFPointer<SecTrustRef> trust(rawTrust);
  if (status != errSecSuccess || !trust) {
    error = "failed to create system trust evaluation";
    return false;
  }

  CFErrorRef rawError = nullptr;
  if (SecTrustEvaluateWithError(trust.get(), &rawError)) {
    return true;
  }
  CFPointer<CFErrorRef> trustError(rawError);
  CFPointer<CFStringRef> description(
      trustError ? CFErrorCopyDescription(trustError.get()) : nullptr);
  error = toString(description.get());
  if (error.empty()) {
    error = "system trust evaluation failed";
  }
  return false;
}

} // namespace aria2
