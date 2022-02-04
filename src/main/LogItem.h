#pragma once

#include <string>

enum LogLevel { LOG_LEVEL_ERR, LOG_LEVEL_WARN, LOG_LEVEL_INFO, LOG_LEVEL_DEBUG };

class LogItem {
 public:
  LogItem(const std::wstring &text, LogLevel level, const std::wstring &source);

  std::wstring GetText() const;
  const wchar_t *GetCText() const;
  LogLevel GetLogLevel() const;
  std::wstring GetSource() const;
  const wchar_t *GetCSource() const;

 protected:
  std::wstring text;
  std::wstring source;
  LogLevel level;
};
