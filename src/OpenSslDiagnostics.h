/* <!-- copyright */
/*
 * aria2 - The high speed download utility
 *
 * Copyright (C) 2026 The aria2-next contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
/* copyright --> */
#ifndef D_OPENSSL_DIAGNOSTICS_H
#define D_OPENSSL_DIAGNOSTICS_H

#include <string>

#include <openssl/err.h>

#include "Log.h"

namespace aria2 {
namespace openssl {

inline std::string errorStack()
{
  std::string result;
  const char* function = nullptr;
  const char* data = nullptr;
  int flags = 0;
  unsigned long code = 0;
  while ((code = ERR_get_error_all(nullptr, nullptr, &function, &data,
                                   &flags)) != 0) {
    char message[256];
    ERR_error_string_n(code, message, sizeof(message));
    if (!result.empty()) {
      result += " | ";
    }
    result += logging::sanitizeText(message);
    if (function && *function) {
      result += " function=" + logging::sanitizeText(function);
    }
    if ((flags & ERR_TXT_STRING) != 0 && data && *data) {
      result += " detail=" + logging::sanitizeText(data);
    }
  }
  return result.empty() ? "OpenSSL did not provide an error" : result;
}

} // namespace openssl
} // namespace aria2

#endif // D_OPENSSL_DIAGNOSTICS_H
