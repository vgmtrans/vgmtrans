// DockingDemoView.h : interface of the CDockingDemoView class
//
/////////////////////////////////////////////////////////////////////////////

#if !defined(AFX_DOCKINGDEMOVIEW_H__C37D7210_589F_4349_A8CC_812F747EC6D0__INCLUDED_)
#define AFX_DOCKINGDEMOVIEW_H__C37D7210_589F_4349_A8CC_812F747EC6D0__INCLUDED_

#if _MSC_VER >= 1000
#pragma once
#endif // _MSC_VER >= 1000

class CDockingDemoView : public CWindowImpl<CDockingDemoView, CEdit>
{
public:
	DECLARE_WND_SUPERCLASS(NULL, CEdit::GetWndClassName())

	BOOL PreTranslateMessage(MSG* pMsg);

	BEGIN_MSG_MAP(CDockingDemoView)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)
};


/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_DOCKINGDEMOVIEW_H__C37D7210_589F_4349_A8CC_812F747EC6D0__INCLUDED_)
