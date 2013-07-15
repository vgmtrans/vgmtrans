// HtmlFrame.h : interface of the CHtmlFrame class
//
/////////////////////////////////////////////////////////////////////////////

#if !defined(AFX_FileFrame_H__053AD675_0AE2_11D6_8BF1_00500477589F__INCLUDED_)
#define AFX_FileFrame_H__053AD675_0AE2_11D6_8BF1_00500477589F__INCLUDED_

#if _MSC_VER >= 1000
#pragma once
#endif // _MSC_VER >= 1000

#ifndef __WTL_TABBED_FRAME_H__
	#error FileFrame.h requires TabbedFrame.h to be included first
#endif

#include "resource.h"
#include "HexViewFrame.h"
#include "ItemTreeView.h"

const int ID_VIEW_SOURCE = WM_USER;

typedef CWinTraits<WS_OVERLAPPEDWINDOW | WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | WS_MAXIMIZE, WS_EX_MDICHILD>	CFileFrameWinTraits;

class CFileFrame :
	public CTabbedFrameImpl<CFileFrame, CDotNetButtonTabCtrl<CTabViewTabItem>, CTabbedMDIChildWindowImpl<CFileFrame, CMDIWindow, CFileFrameWinTraits> >
{
protected:
	typedef CTabbedFrameImpl<CFileFrame, CDotNetButtonTabCtrl<CTabViewTabItem>, CTabbedMDIChildWindowImpl<CFileFrame, CMDIWindow, CFileFrameWinTraits> > baseClass;
	typedef CFileFrame thisClass;

public:
	DECLARE_FRAME_WND_CLASS(NULL, IDR_FILEHEXFRAME)

	CTabbedMDICommandBarCtrl* m_pCmdBar;
	CHexViewFrame m_HexViewFrame;
	//CTrackViewFrame m_TrackViewFrame;
	int m_nHexViewTabIndex;
	//int m_nTrackViewTabIndex;
	VGMFile* m_vgmfile;

	CFileFrame(VGMFile* theVGMFile);

	virtual void OnFinalMessage(HWND /*hWnd*/)
	{
		//delete this;
	}

	void SetCommandBarCtrlForContextMenu(CTabbedMDICommandBarCtrl* pCmdBar)
	{
		m_pCmdBar = pCmdBar;
	}

	BEGIN_MSG_MAP(CFileFrame)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_SETTINGCHANGE, OnSettingChange)
		MESSAGE_HANDLER(WM_FORWARDMSG, OnForwardMsg)
		MESSAGE_HANDLER(WM_MDIACTIVATE, OnMDIActivate)
		MESSAGE_HANDLER(UWM_MDICHILDSHOWTABCONTEXTMENU, OnShowTabContextMenu)
		COMMAND_ID_HANDLER(ID_FILE_SAVE, OnFileSave)
		COMMAND_ID_HANDLER(ID_FILE_CLOSE, OnFileClose)
		//COMMAND_ID_HANDLER(ID_VIEW_SOURCE, OnViewSource)

		CHAIN_MSG_MAP(baseClass)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)
	
	LRESULT OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled);
	LRESULT OnSettingChange(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/);
	LRESULT OnForwardMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/);
	LRESULT OnShowTabContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled);
	LRESULT OnFileSave(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnFileClose(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnMDIActivate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

	void SelectItem(VGMItem* item);

	//LRESULT OnViewSource(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
	//{
	//	this->DisplayTab(m_HtmlSourceView, FALSE);
	//	return 0;
	//}
};

/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_HtmlFrame_H__053AD675_0AE2_11D6_8BF1_00500477589F__INCLUDED_)
