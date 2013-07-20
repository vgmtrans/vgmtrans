#include "stdafx.h"

#include <string>

#include "LogItem.h"

LogItem::LogItem() :
	text(),
	level(LOG_LEVEL_ERR)
{
}

LogItem::LogItem(const wchar_t* text, LogLevel level) :
	text(text ? text : L""),
	level(level)
{
}

LogItem::LogItem(std::wstring& text, LogLevel level) :
	text(text),
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

LogLevel LogItem::GetLogLevel() const
{
	return level;
}
