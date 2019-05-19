/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include <string>
#include <chrono>

enum LogLevel { LOG_LEVEL_ERR, LOG_LEVEL_WARN, LOG_LEVEL_INFO, LOG_LEVEL_DEBUG };

class LogItem {
   public:
    LogItem();
    LogItem(const wchar_t *text, LogLevel level, const wchar_t *source);
    LogItem(const std::wstring &text, LogLevel level, const std::wstring &source);
    virtual ~LogItem();

    std::wstring GetText() const;
    const wchar_t *GetCText() const;
    std::time_t GetTime() const;
    LogLevel GetLogLevel() const;
    std::wstring GetSource() const;
    const wchar_t *GetCSource() const;

   protected:
    std::wstring text;
    std::wstring source;
    std::chrono::system_clock::time_point time;
    LogLevel level;
};
