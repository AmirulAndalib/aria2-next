#ifndef D_APPLE_TRUST_VERIFIER_H
#define D_APPLE_TRUST_VERIFIER_H

#include <string>

typedef struct ssl_st SSL;

namespace aria2 {

bool verifyAppleSystemTrust(SSL* ssl, const std::string& hostname,
                            std::string& error);

} // namespace aria2

#endif // D_APPLE_TRUST_VERIFIER_H
