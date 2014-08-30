#ifdef _WIN32
	#include "stdafx.h"
#endif

#include <string>
#include "datetime.h"

#include "LogItem.h"

LogItem::LogItem() :
	text(),
	time(DateTime::get_Now()),
	level(LOG_LEVEL_ERR),
	source()
{
}

LogItem::LogItem(const wchar_t* text, LogLevel level, const wchar_t* source) :
	text(text ? text : L""),
	time(DateTime::get_Now()),
	level(level),
	source(source ? source : L"")
{
}

LogItem::LogItem(std::wstring& text, LogLevel level, std::wstring& source) :
	text(text),
	time(DateTime::get_Now()),
	level(level),
	source(source)
{
}

LogItem::~LogItem()
{
}

std::wstring LogItem::GetText() const
{
	return std::wstring(text);
}

const wchar_t* LogItem::GetCText() const
{
	return text.c_str();
}

DateTime LogItem::GetTime() const
{
	return time;
}

LogLevel LogItem::GetLogLevel() const
{
	return level;
}

std::wstring LogItem::GetSource() const
{
	return std::wstring(source);
}

const wchar_t* LogItem::GetCSource() const
{
	return source.c_str();
}
