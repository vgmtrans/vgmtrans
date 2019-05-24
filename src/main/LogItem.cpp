/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "LogItem.h"

LogItem::LogItem()
    : text(), time(std::chrono::system_clock::now()), level(LogLevel::ERROR), source() {}

LogItem::LogItem(const wchar_t *text, LogLevel level, const wchar_t *source)
    : text(text ? text : L""),
      time(std::chrono::system_clock::now()),
      level(level),
      source(source ? source : L"") {}

LogItem::LogItem(const std::wstring &text, LogLevel level, const std::wstring &source)
    : text(text), time(std::chrono::system_clock::now()), level(level), source(source) {}

LogItem::~LogItem() {}

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

std::time_t LogItem::GetTime() const {
    return std::chrono::system_clock::to_time_t(time);
}

const wchar_t *LogItem::GetCSource() const {
    return source.c_str();
}
