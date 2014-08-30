#include "stdafx.h"
#include "mainfrm.h"
#include "osdepend.h"

void ShowAlertMessage(wchar_t *msg)
{ 
	if (pMainFrame != NULL && pMainFrame->m_hWnd != NULL)
		MessageBox(pMainFrame->m_hWnd, msg, _T("Alert"), MB_OK);
	else
		fwprintf(stderr, L"%s\n", msg);
}
