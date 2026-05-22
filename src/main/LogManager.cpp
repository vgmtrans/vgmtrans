#include "LogManager.h"

#include <mutex>

#include <spdlog/sinks/base_sink.h>

#include "LogItem.h"
#include "Root.h"
namespace {

// Custom sink to interface with UI logging.
template <typename Mutex>
class UISink : public spdlog::sinks::base_sink<Mutex> {
 protected:
  void sink_it_(const spdlog::details::log_msg& msg) override {
    auto level = convertSpdlogLevel(msg.level);
    auto logItem = new LogItem(fmt::to_string(msg.payload), level, msg.source.filename);
    pRoot->log(logItem);
    delete logItem;
  }

  void flush_() override {}
};

}  // namespace

LogLevel convertSpdlogLevel(spdlog::level::level_enum level) {
  switch (level) {
    case spdlog::level::err:
      return LOG_LEVEL_ERR;
    case spdlog::level::warn:
      return LOG_LEVEL_WARN;
    case spdlog::level::info:
      return LOG_LEVEL_INFO;
    case spdlog::level::debug:
      return LOG_LEVEL_DEBUG;
    default:
      return LOG_LEVEL_INFO;
  }
}

LogManager& LogManager::the() {
  static LogManager instance;
  return instance;
}

LogManager::LogManager() {
  auto ui_sink = std::make_shared<UISink<std::mutex>>();
  logger_ = std::make_shared<spdlog::logger>("ui_logger", ui_sink);
  logger_->set_level(spdlog::level::trace);
  logger_->flush_on(spdlog::level::trace);
}

LogManager::~LogManager() = default;
