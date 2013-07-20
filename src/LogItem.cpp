#include "stdafx.h"

#include <string>
#include "datetime.h"

#include "LogItem.h"

LogItem::LogItem() :
	text(),
	time(DateTime::get_Now()),
	level(LOG_LEVEL_ERR)
{
}

LogItem::LogItem(const wchar_t* text, LogLevel level) :
	text(text ? text : L""),
	time(DateTime::get_Now()),
	level(level)
{
}

LogItem::LogItem(std::wstring& text, LogLevel level) :
	text(text),
	time(DateTime::get_Now()),
	level(level)
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
