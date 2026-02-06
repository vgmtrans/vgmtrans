#include "LogManager.h"

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
