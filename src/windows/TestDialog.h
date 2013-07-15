// CollDialog.h : interface of the CTestDialog class
//
/////////////////////////////////////////////////////////////////////////////

#ifndef _TestDialog_h_
#define _TestDialog_h_

#include "resource.h"

class CCollDialog :
	public CDialogImpl<CCollDialog>,
	public CDialogResize<CCollDialog>
{
protected:
	typedef CCollDialog thisClass;
	typedef CDialogImpl<CCollDialog> baseClass;
	typedef CDialogResize<CCollDialog> resizeClass;

protected:
	//CEdit m_edit;
	CButton m_button;
	CListViewNoFlicker m_list;

public:
	enum { IDD = IDD_DOCKEDDIALOG };

	BOOL PreTranslateMessage(MSG* pMsg)
	{
		return this->IsDialogMessage(pMsg);
	}

	BEGIN_MSG_MAP(thisClass)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		MESSAGE_HANDLER(WM_CLOSE, OnClose)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		if(uMsg == WM_FORWARDMSG)
			if(PreTranslateMessage((LPMSG)lParam)) return TRUE;

		COMMAND_ID_HANDLER(IDOK, OnExecute)
		COMMAND_ID_HANDLER(IDC_BTN_EXECUTE, OnExecute)
		CHAIN_MSG_MAP(resizeClass)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnClose(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);

	LRESULT OnExecute(WORD /*wNotifyCode*/, WORD wID, HWND hWndCtl, BOOL& /*bHandled*/);

	BEGIN_DLGRESIZE_MAP(thisClass)
		//DLGRESIZE_CONTROL(IDC_EDIT_COMMAND, (DLSZ_SIZE_X))
		DLGRESIZE_CONTROL(IDC_BTN_CREATE, (DLSZ_MOVE_X))
		//DLGRESIZE_CONTROL(IDC_DIVIDER, (DLSZ_SIZE_X))
		DLGRESIZE_CONTROL(IDC_FILELIST, (DLSZ_SIZE_X | DLSZ_SIZE_Y))
	END_DLGRESIZE_MAP()

protected:
	void InitializeControls(void);
	void InitializeValues(void);
};

#endif // _TestDialog_h_
