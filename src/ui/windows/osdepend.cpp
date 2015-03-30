#include "pch.h"
#include "mainfrm.h"
#include "osdepend.h"

void Alert(const wchar_t *fmt, ...)
{ 
	va_list	args;
	TCHAR formattedMessage[8192];

	va_start(args, fmt);
	wvsprintf(formattedMessage, fmt, args);
	va_end(args);

	if (pMainFrame != NULL && pMainFrame->m_hWnd != NULL) {
		MessageBox(pMainFrame->m_hWnd, formattedMessage, _T("Alert"), MB_OK);
	}
	else {
		fwprintf(stderr, L"%s\n", formattedMessage);
	}
}

void LogDebug(const wchar_t *fmt, ...)
{
	va_list	args;
	TCHAR header[64];
	TCHAR formattedMessage[8192];
	TCHAR message[8192];
	SYSTEMTIME st;

	va_start(args, fmt);
	wvsprintf(formattedMessage, fmt, args);
	va_end(args);

	GetLocalTime(&st);
	wsprintf(header, TEXT("%04d/%02d/%02d %02d:%02d:%02d.%03d"),
		st.wYear, st.wMonth, st.wDay,
		st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

	wsprintf(message, TEXT("%s: %s\n"), header, formattedMessage);
	OutputDebugString(message);
}
