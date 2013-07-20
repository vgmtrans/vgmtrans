#pragma once

#include <string>

enum LogLevel { LOG_LEVEL_ERR, LOG_LEVEL_WARN, LOG_LEVEL_INFO };

class LogItem
{
public:
	LogItem();
	LogItem(const wchar_t* text, LogLevel level);
	LogItem(std::wstring& text, LogLevel level);
	virtual ~LogItem();

	std::wstring GetText() const;
	const wchar_t* GetCText() const;
	LogLevel GetLogLevel() const;

protected:
	std::wstring text;
	LogLevel level;
};
