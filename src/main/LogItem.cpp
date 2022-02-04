#include "pch.h"

#include "LogItem.h"

LogItem::LogItem(const std::wstring &text, LogLevel level, const std::wstring &source) :
    text(text),
    source(source),
    level(level) {
}

std::wstring LogItem::GetText() const {
  return std::wstring(text);
}

const wchar_t *LogItem::GetCText() const {
  return text.c_str();
}

LogLevel LogItem::GetLogLevel() const {
  return level;
}

std::wstring LogItem::GetSource() const {
  return std::wstring(source);
}

const wchar_t *LogItem::GetCSource() const {
  return source.c_str();
}
