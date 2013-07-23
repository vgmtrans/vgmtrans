#include "stdafx.h"
#include "mainfrm.h"
#include "osdepend.h"

void ShowAlertMessage(wchar_t *msg)
{ 
	MessageBox(pMainFrame->m_hWnd, msg, _T("Alert"), MB_OK);
}