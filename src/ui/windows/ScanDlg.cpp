#include "pch.h"
#include "resource.h"

#include "ScanDlg.h"

LRESULT CScanDlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
	CenterWindow(GetParent());
	return TRUE;
}

LRESULT CScanDlg::OnCloseCmd(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	//EndDialog(wID);
	this->DestroyWindow();
	return 0;
}
