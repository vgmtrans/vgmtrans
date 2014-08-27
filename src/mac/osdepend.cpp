//
//  osdepend.cpp
//  VGMTrans
//
//  Created by Mike on 8/24/14.
//
//

#include <unistd.h>
#include <cwchar>
#include "osdepend.h"

void Alert(wchar_t const *msg)
{
	return;
}

void Alert(const wchar_t *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vwprintf(fmt, args);
    va_end(args);
}

void LogDebug(const wchar_t *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vwprintf(fmt, args);
    va_end(args);
}
