#include "pch.h"
#include "mainfrm.h"
#include "osdepend.h"

void Alert(const wchar_t *fmt, ...)
{ 
	if (pMainFrame != NULL && pMainFrame->m_hWnd != NULL)
		MessageBox(pMainFrame->m_hWnd, fmt, _T("Alert"), MB_OK);
	else
		fwprintf(stderr, L"%s\n", fmt);
}

void LogDebug(const wchar_t *fmt, ...)
{

}

//#define Alert(...)						\
//	{									\
//		wchar_t buffer[1024];			\
//		wsprintf(buffer, __VA_ARGS__);	\
//		ShowAlertMessage(buffer);		\
//	}	