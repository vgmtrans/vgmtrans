#pragma once

#include <string>

enum LogLevel { LOG_LEVEL_ERR, LOG_LEVEL_WARN, LOG_LEVEL_INFO, LOG_LEVEL_DEBUG };

class LogItem {
 public:
  LogItem(const std::string &text, LogLevel level, const std::string &source);

  std::string GetText() const;
  const char* GetCText() const;
  LogLevel GetLogLevel() const;
  std::string GetSource() const;
  const char* GetCSource() const;

 protected:
  std::string text;
  std::string source;
  LogLevel level;
};
