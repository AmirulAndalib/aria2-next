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
#ifndef D_SQLITE_DIAGNOSTICS_H
#define D_SQLITE_DIAGNOSTICS_H

#include <mutex>
#include <string>

#include <sqlite3.h>

#include "Log.h"
#include "fmt.h"

namespace aria2 {
namespace sqlite {

inline std::string diagnostic(::sqlite3* db, int result, const char* operation)
{
  const auto extended = db ? sqlite3_extended_errcode(db) : result;
  const auto offset = db ? sqlite3_error_offset(db) : -1;
  return fmt("operation=%s result=%d extended=%d offset=%d message=%s",
             operation, result, extended, offset,
             db ? sqlite3_errmsg(db) : sqlite3_errstr(result));
}

inline void nativeLog(void*, int result, const char* message) noexcept
{
  try {
    logging::tryWrite(
        spdlog::level::trace, __FILE__, __LINE__,
        fmt("component=storage event=sqlite_native result=%d message=%s",
            result,
            logging::sanitizeText(message ? message : "unknown").c_str()));
  }
  catch (...) {
  }
}

inline void configureNativeLogging()
{
  static std::once_flag once;
  std::call_once(once, []() {
    const auto result = sqlite3_config(SQLITE_CONFIG_LOG, nativeLog, nullptr);
    if (result != SQLITE_OK) {
      A2_LOG_WARN(fmt("component=storage event=sqlite_log_config_failed "
                      "result=%d message=%s",
                      result, sqlite3_errstr(result)));
    }
  });
}

inline void configureConnection(::sqlite3* db)
{
  if (!db) {
    return;
  }
  const auto result = sqlite3_extended_result_codes(db, 1);
  if (result != SQLITE_OK) {
    A2_LOG_WARN(fmt("component=storage event=sqlite_extended_codes_failed %s",
                    diagnostic(db, result, "extended_result_codes").c_str()));
  }
}

} // namespace sqlite
} // namespace aria2

#endif // D_SQLITE_DIAGNOSTICS_H
