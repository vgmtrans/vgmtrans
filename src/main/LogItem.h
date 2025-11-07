#pragma once

#include <string>

enum LogLevel : int { LOG_LEVEL_ERR, LOG_LEVEL_WARN, LOG_LEVEL_INFO, LOG_LEVEL_DEBUG };

class LogItem {
 public:
  LogItem(const std::string &text, LogLevel level, const std::string &source) :
      m_text(text),
      m_source(source),
      m_level(level) {
  }

  const std::string& text() const { return m_text; };
  LogLevel logLevel() const { return m_level; }
  const std::string& source() const { return m_source; };

 private:
  std::string m_text;
  std::string m_source;
  LogLevel m_level;
};
