/*
* VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/fmt/fmt.h>
#include "Root.h"
#include "LogItem.h"

LogLevel convertSpdlogLevel(spdlog::level::level_enum level);

// Custom sink to interface with UI logging
template<typename Mutex>
class UISink : public spdlog::sinks::base_sink<Mutex> {
protected:
  void sink_it_(const spdlog::details::log_msg& msg) override {
    auto level = convertSpdlogLevel(msg.level);
    LogItem item(fmt::to_string(msg.payload), level, msg.source.filename);
    pRoot->log(std::move(item));
  }

  void flush_() override {}
};

// Singleton LogManager class
class LogManager {
public:
  static LogManager& the() {
    static LogManager instance;
    return instance;
  }

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

  LogManager() {
    auto ui_sink = std::make_shared<UISink<std::mutex>>();
    logger_ = std::make_shared<spdlog::logger>("ui_logger", ui_sink);
    logger_->set_level(spdlog::level::trace);
    logger_->flush_on(spdlog::level::trace);
  }

  ~LogManager() = default;

  LogManager(const LogManager&) = delete;
  LogManager& operator=(const LogManager&) = delete;
};

// Logging Macros
#define L_LOG(level, ...) LogManager::the().log(level, THIS_FILE_NAME, __LINE__, __VA_ARGS__)
#define L_ERROR(...) L_LOG(spdlog::level::err, __VA_ARGS__)
#define L_WARN(...) L_LOG(spdlog::level::warn, __VA_ARGS__)
#define L_INFO(...) L_LOG(spdlog::level::info, __VA_ARGS__)
#define L_DEBUG(...) L_LOG(spdlog::level::debug, __VA_ARGS__)