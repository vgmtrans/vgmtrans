// TestDialog.cpp : implementation of the CTestDialog class
//
/////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "resource.h"

#include "TestDialog.h"

LRESULT CCollDialog::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
	HICON hIcon = (HICON)::LoadImage(_pModule->GetResourceInstance(), MAKEINTRESOURCE(IDI_FOLDER_OPEN), IMAGE_ICON, 16,16, LR_SHARED);
	this->SetIcon(hIcon, ICON_SMALL);

	this->InitializeControls();
	this->InitializeValues();

	resizeClass::DlgResize_Init(false, true, WS_CLIPCHILDREN);

	return TRUE;
}

void CCollDialog::InitializeControls(void)
{
	//m_edit = this->GetDlgItem(IDC_EDIT_COMMAND);
	m_button = this->GetDlgItem(IDC_BTN_EXECUTE);
	m_list.SubclassWindow(this->GetDlgItem(IDC_LIST));

	DWORD nExtended = m_list.GetExtendedListViewStyle()|
				LVS_EX_FULLROWSELECT|LVS_EX_INFOTIP|LVS_EX_HEADERDRAGDROP|//LVS_EX_LABELTIP|
				LVS_EX_SUBITEMIMAGES|LVS_EX_UNDERLINEHOT|LVS_EX_FLATSB|LVS_EX_GRIDLINES;
	m_list.SetExtendedListViewStyle(nExtended);

	LVCOLUMN lvColumn = {0};

	lvColumn.mask = (LVCF_FMT|LVCF_TEXT);
	lvColumn.fmt = LVCFMT_LEFT;

	lvColumn.pszText = _T("Name");
	m_list.InsertColumn(0, &lvColumn);

	lvColumn.pszText = _T("Value");
	m_list.InsertColumn(1, &lvColumn);

	CRect rcDialog;
	this->GetClientRect(&rcDialog);

	m_list.SetColumnWidth(0, rcDialog.Width()/2);
	m_list.SetColumnWidth(1, rcDialog.Width()/2);
}

LRESULT CCollDialog::OnClose(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{
	if(::IsWindow(m_hWnd))
	{
		::DestroyWindow(m_hWnd);
	}

	// NOTE: For some reason, DefWindowProc doesn't call DestroyWindow
	//  when handling WM_CLOSE for modeless dialogs, so we'll do it ourself
	bHandled = TRUE;
	return 0;
}

LRESULT CCollDialog::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{
	m_list.UnsubclassWindow();

	// Let anyone else (including DefWindowProc) see the message
	bHandled = FALSE;
	return 0;
}

LRESULT CCollDialog::OnExecute(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{/*
#if _ATL_VER > 0x0700
	CString text;
	m_edit.GetWindowText(text);
	if(text.GetLength() > 0)
	{
		this->MessageBox(text, _T("Execute"), MB_OK);
	}
#else
	CComBSTR text;
	m_edit.GetWindowText(&text);
	if(text.Length() > 0)
	{
		::MessageBoxW(m_hWnd, text, L"Execute", MB_OK);
	}
#endif*/
	return 0;
}

void CCollDialog::InitializeValues(void)
{
	//m_edit.SetWindowText(_T("Test"));
	m_button.SetFocus();

	int itemIndex = 0;
	itemIndex = m_list.GetItemCount();
	itemIndex = m_list.InsertItem(itemIndex, _T("Hello"));
	m_list.SetItemText(itemIndex, 1, _T("There"));
}
