#include "LogItem.h"

LogItem::LogItem(const std::string &text, LogLevel level, const std::string &source) :
    text(text),
    source(source),
    level(level) {
}

std::string LogItem::GetText() const {
  return std::string(text);
}

const char* LogItem::GetCText() const {
  return text.c_str();
}

LogLevel LogItem::GetLogLevel() const {
  return level;
}

std::string LogItem::GetSource() const {
  return std::string(source);
}

const char* LogItem::GetCSource() const {
  return source.c_str();
}
