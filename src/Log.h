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
#ifndef D_LOG_H
#define D_LOG_H

#include "common.h"

#include <cstddef>
#include <cstdint>
#include <string>

#include <spdlog/common.h>

namespace aria2 {

class Exception;

namespace logging {

constexpr size_t MAX_FILES = 100;
constexpr size_t MAX_RECORD_SIZE = 64 * 1024;

struct Settings {
  Settings();

  std::string file;
  size_t maxFileSize;
  size_t maxFiles;
  spdlog::level::level_enum fileLevel;
  spdlog::level::level_enum consoleLevel;
  bool consoleOutput;
  bool colorOutput;
  bool consoleToStderr;
};

Settings getSettings();
uint64_t revision();
spdlog::level::level_enum parseLevel(const std::string& level);
std::string sanitizeText(const std::string& value);
std::string sanitizeUri(const std::string& value);
std::string summarizeHttpMessage(const std::string& value);
void configure(const Settings& settings);
void flush();
void shutdown();

bool enabled(spdlog::level::level_enum level);
void write(spdlog::level::level_enum level, const char* sourceFile,
           int lineNum, const char* message);
void write(spdlog::level::level_enum level, const char* sourceFile,
           int lineNum, const std::string& message);
void write(spdlog::level::level_enum level, const char* sourceFile,
           int lineNum, const char* message, const Exception& exception);
void write(spdlog::level::level_enum level, const char* sourceFile,
           int lineNum, const std::string& message,
           const Exception& exception);
void tryWrite(spdlog::level::level_enum level, const char* sourceFile,
              int lineNum, const std::string& message) noexcept;

} // namespace logging

#define A2_LOG_ENABLED(level) aria2::logging::enabled(level)

#define A2_LOG(level, message)                                                \
  do {                                                                        \
    if (A2_LOG_ENABLED(level)) {                                              \
      aria2::logging::write(level, __FILE__, __LINE__, message);              \
    }                                                                         \
  } while (0)

#define A2_LOG_EX(level, message, exception)                                  \
  do {                                                                        \
    if (A2_LOG_ENABLED(level)) {                                              \
      aria2::logging::write(level, __FILE__, __LINE__, message, exception);   \
    }                                                                         \
  } while (0)

#define A2_LOG_TRACE_ENABLED A2_LOG_ENABLED(spdlog::level::trace)

#define A2_LOG_TRACE(message) A2_LOG(spdlog::level::trace, message)
#define A2_LOG_TRACE_EX(message, exception)                                   \
  A2_LOG_EX(spdlog::level::trace, message, exception)

#define A2_LOG_DEBUG(message) A2_LOG(spdlog::level::debug, message)
#define A2_LOG_DEBUG_EX(message, exception)                                   \
  A2_LOG_EX(spdlog::level::debug, message, exception)

#define A2_LOG_INFO(message) A2_LOG(spdlog::level::info, message)
#define A2_LOG_INFO_EX(message, exception)                                    \
  A2_LOG_EX(spdlog::level::info, message, exception)

#define A2_LOG_WARN(message) A2_LOG(spdlog::level::warn, message)
#define A2_LOG_WARN_EX(message, exception)                                    \
  A2_LOG_EX(spdlog::level::warn, message, exception)

#define A2_LOG_ERROR(message) A2_LOG(spdlog::level::err, message)
#define A2_LOG_ERROR_EX(message, exception)                                   \
  A2_LOG_EX(spdlog::level::err, message, exception)

} // namespace aria2

#endif // D_LOG_H
