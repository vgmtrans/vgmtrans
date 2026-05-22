/*
* VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <memory>
#include <string>
#include <utility>

#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>

#include "common.h"
enum LogLevel : int;

LogLevel convertSpdlogLevel(spdlog::level::level_enum level);

// Singleton LogManager class
class LogManager {
public:
  static LogManager& the();

  template<typename... Args>
  void log(spdlog::level::level_enum lvl, const char* file, int line, fmt::format_string<Args...> fmt, Args&&... args) const {
    if (lvl != spdlog::level::off) {
      logger_->log(spdlog::source_loc{file, line, ""}, lvl, fmt, std::forward<Args>(args)...);
    }
  }

  void log(spdlog::level::level_enum lvl, const char* file, int line, const std::string& fmt_string) const {
    if (lvl != spdlog::level::off) {
      logger_->log(spdlog::source_loc{file, line, ""}, lvl, "{}", fmt_string);
    }
  }

private:
  std::shared_ptr<spdlog::logger> logger_;

  LogManager();
  ~LogManager();

  LogManager(const LogManager&) = delete;
  LogManager& operator=(const LogManager&) = delete;
};

// Logging Macros
#define L_LOG(level, ...) LogManager::the().log(level, THIS_FILE_NAME, __LINE__, __VA_ARGS__)
#define L_ERROR(...) L_LOG(spdlog::level::err, __VA_ARGS__)
#define L_WARN(...) L_LOG(spdlog::level::warn, __VA_ARGS__)
#define L_INFO(...) L_LOG(spdlog::level::info, __VA_ARGS__)
#define L_DEBUG(...) L_LOG(spdlog::level::debug, __VA_ARGS__)
