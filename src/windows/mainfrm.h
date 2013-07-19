// MainFrm.h : interface of the CMainFrame class
//
/////////////////////////////////////////////////////////////////////////////

#if !defined(AFX_MAINFRM_H__F095BEBE_A897_4DA8_9344_DFBBDAA2C70B__INCLUDED_)
#define AFX_MAINFRM_H__F095BEBE_A897_4DA8_9344_DFBBDAA2C70B__INCLUDED_

#if _MSC_VER >= 1000
#pragma once
#endif // _MSC_VER >= 1000

//#include <atlctrlx.h>

#include <dbstate.h>
#include <DockingFrame.h>
#include <DockingFocus.h>
#include <VC7LikeCaption.h>
#include <TabbedDockingWindow.h>

#include "resource.h"

#include "VGMTransTabCtrl.h"
#include "CollDialog.h"
#include "DropFileHandler.h"

//#include "FileFrame.h"
//#include "PlainTextView.h"
//#include "RawFileTreeView.h"
//#include "VGMFileTreeView.h"
//#include "ItemTreeView.h"
//#include "HexView.h"

#include "MusicPlayer.h"

class CPlainTextView;
class CRawFileListView;
class CItemTreeView;
class CVGMFileTreeView;
class CVGMFileListView;
class CVGMCollListView;
class CFileFrame;

#define CWM_INITIALIZE	(WMDF_LAST+1)


typedef dockwins::CDockingFrameTraitsT< dockwins::CVC7LikeAutoHidePaneTraits, dockwins::CSimpleSplitterBar<3>,
		WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
		WS_EX_APPWINDOW | WS_EX_WINDOWEDGE> CMainFrameTraits;

class CMainFrame :
	//public dockwins::CDockingFrameImpl<CMainFrame, CWindow>,
	public dockwins::CAutoHideMDIDockingFrameImpl<CMainFrame, CMDIWindow, CMainFrameTraits, dockwins::CVC7LikeAutoHidePaneTraits>,
	public CUpdateUI<CMainFrame>,
	public CDropFilesHandler<CMainFrame>,
	public CMessageFilter,
	public CIdleHandler
//	public CGameHandler
{
	friend class WinVGMRoot;

public:
	CMainFrame();

protected:
	typedef CMainFrame thisClass;
	typedef dockwins::CAutoHideMDIDockingFrameImpl<CMainFrame, CMDIWindow, CMainFrameTraits, dockwins::CVC7LikeAutoHidePaneTraits> baseClass;
	typedef CDotNetTabCtrl<CTabViewTabItem> TTabCtrl;

protected:
	// You can also use some of the other types of tabs as MDI tabs, like CDotNetButtonTabCtrl
	//CTabbedMDIClient<CDotNetButtonTabCtrl<CTabViewTabItem> > m_tabbedClient;
	CMultiPaneStatusBarCtrl m_status;
	CTabbedMDIClient<TTabCtrl> m_tabbedClient;
	sstate::CWindowStateMgr	m_stateMgr;

	CTabbedMDICommandBarCtrl m_CmdBar;

	std::vector<CTabbedAutoHideDockingWindow*> m_PaneWindows;
	typedef std::vector<CTabbedAutoHideDockingWindow*>::iterator _PaneWindowIter;
	std::vector<HICON> m_PaneWindowIcons;
	typedef std::vector<HICON>::iterator _PaneWindowIconsIter;

	
	//CHexView        m_FileHexView;
	//CPlainTextView m_OutputView;B
	//CPlainTextView m_FindResultsView;
	CCollDialog m_CollDialog;

	//tabbed mdi child views
	bool FileFrameVis;

	map<VGMFile*, CFileFrame*> frameMap;

	CTabbedAutoHideDockingWindow* pItemTreePaneWindow;

public:
	//map<HWND, CItemTreeView*> hwndToItemTreeView;
	

public:
	DECLARE_FRAME_WND_CLASS(NULL, IDR_MAINFRAME)

	virtual BOOL PreTranslateMessage(MSG* pMsg);
	virtual BOOL OnIdle();
	//virtual BOOL IsPaused();
	//virtual HRESULT OnUpdateFrame();

	BEGIN_UPDATE_UI_MAP(CMainFrame)
		UPDATE_ELEMENT(ID_VIEW_FILEHEXVIEW, UPDUI_MENUPOPUP)
		UPDATE_ELEMENT(ID_VIEW_TOOLBAR, UPDUI_MENUPOPUP)
		UPDATE_ELEMENT(ID_VIEW_STATUS_BAR, UPDUI_MENUPOPUP)
		UPDATE_ELEMENT(ID_VIEW_RAWFILELIST, UPDUI_MENUPOPUP)
		UPDATE_ELEMENT(ID_VIEW_VGMFILELIST, UPDUI_MENUPOPUP)
		UPDATE_ELEMENT(ID_VIEW_COLLECTIONS, UPDUI_MENUPOPUP)
		UPDATE_ELEMENT(ID_VIEW_COLLECTIONINFO, UPDUI_MENUPOPUP)
		UPDATE_ELEMENT(ID_PLAY, UPDUI_TOOLBAR)
		UPDATE_ELEMENT(ID_PAUSE, UPDUI_TOOLBAR)
		UPDATE_ELEMENT(ID_STOP, UPDUI_TOOLBAR)
	END_UPDATE_UI_MAP()


	BEGIN_MSG_MAP(CMainFrame)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_CLOSE, OnClose)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_SETTINGCHANGE, OnSettingChange)
		MESSAGE_HANDLER(WM_SYSCOLORCHANGE, OnSysColorChange)
		MESSAGE_HANDLER(WM_QUERYENDSESSION, OnQueryEndSession)
		MESSAGE_HANDLER(CWM_INITIALIZE, OnInitialize)

		COMMAND_ID_HANDLER(ID_FILE_OPEN, OnFileOpen)
		COMMAND_ID_HANDLER(ID_FILE_SAVE, OnFileSave)
		COMMAND_ID_HANDLER(ID_APP_EXIT, OnFileExit)
		//COMMAND_ID_HANDLER(ID_FILE_NEW, OnFileNew)
		//COMMAND_ID_HANDLER(ID_VIEW_FILEHEXVIEW, OnViewFileHexView)
		COMMAND_ID_HANDLER(ID_VIEW_TOOLBAR, OnViewToolBar)
		COMMAND_ID_HANDLER(ID_VIEW_STATUS_BAR, OnViewStatusBar)

		COMMAND_ID_HANDLER(ID_PLAY, OnPlay)
		COMMAND_ID_HANDLER(ID_PAUSE, OnPause)
		COMMAND_ID_HANDLER(ID_STOP, OnStop)

		COMMAND_ID_HANDLER(ID_APP_ABOUT, OnAppAbout)
		COMMAND_ID_HANDLER(ID_WINDOW_CASCADE, OnWindowCascade)
		COMMAND_ID_HANDLER(ID_WINDOW_TILE_HORZ, OnWindowTile)
		COMMAND_ID_HANDLER(ID_WINDOW_ARRANGE, OnWindowArrangeIcons)

		if(uMsg == WM_COMMAND && (LOWORD(wParam) >= ID_VIEW_PANEFIRST && LOWORD(wParam) <= ID_VIEW_PANELAST))
		{
			ATLASSERT(m_PaneWindows.size() >= (ID_VIEW_PANELAST-ID_VIEW_PANEFIRST));
			m_PaneWindows[LOWORD(wParam) - ID_VIEW_PANEFIRST]->Toggle();
		}

		CHAIN_MDI_CHILD_COMMANDS()
		CHAIN_MSG_MAP(CUpdateUI<CMainFrame>)
		
		CHAIN_MSG_MAP(CDropFilesHandler<CMainFrame>)
		CHAIN_MSG_MAP(baseClass)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

	LRESULT OnDropFiles(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnClose(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	void CloseUpShop();
	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnSettingChange(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnSysColorChange(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnQueryEndSession(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnInitialize(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

	LRESULT OnFileOpen(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnFileSave(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnFileExit(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	//LRESULT OnFileNew(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	//LRESULT OnViewFileHexView(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnViewToolBar(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnViewStatusBar(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnAppAbout(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	LRESULT OnPlay(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnPause(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnStop(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	LRESULT OnWindowCascade(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnWindowTile(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnWindowArrangeIcons(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	void SetStatusBarIcon(int iconID);
	void WriteStatusBar(const wchar_t* string, int iconID = IDI_DEFAULT);
	void WriteStatusBarOffsetAndLength(ULONG offset, ULONG length);
	void WriteItemToStatusBar(VGMItem* item);
	
	void ShowVGMFileFrame(VGMFile* vgmfile);
	void OnAddVGMFile(VGMFile* vgmfile);
	void OnRemoveVGMFile(VGMFile* vgmfile);
	void OnCloseVGMFileFrame(VGMFile* vgmfile);
	//void ShowFileHexView(bool bVis);

	//void SetItemTreeViewPane(VGMFile* vgmfile);
	void SelectItem(VGMItem* item);
	void SelectColl(VGMColl* coll);

// CDropFilesHandler requisites.
	void SetPaneWidths(int* arrWidths, int nPanes);
	BOOL IsReadyForDrop(void);
	BOOL HandleDroppedFile(LPCTSTR szBuff);
	void EndDropFiles(void);

// Helper methods
protected:
	void InitializeDefaultPanes(void);
	void UninitializeDefaultPanes(void);
	HWND CreateFileListViewPane(CRawFileListView& view, LPCTSTR sName, HICON hIcon, CRect& rcFloat, CRect& rcDock, HWND hDockTo);
	HWND CreateVGMFilesTreeViewPane(CVGMFileTreeView& view, LPCTSTR sName, HICON hIcon, CRect& rcFloat, CRect& rcDock, HWND hDockTo);
	HWND CreateVGMFileListViewPane(CVGMFileListView& view, LPCTSTR sName, HICON hIcon, CRect& rcFloat, CRect& rcDock, HWND hDockTo);
	HWND CreateVGMCollListViewPane(CVGMCollListView& view, LPCTSTR sName, HICON hIcon, CRect& rcFloat, CRect& rcDock, HWND hDockTo);
	HWND CreatePlainTextOutputPane(CPlainTextView& view, LPCTSTR sName, HICON hIcon, CRect& rcFloat, CRect& rcDock, HWND hDockTo = NULL);
	HWND CreateCollDialogPane(CCollDialog& dlg, LPCTSTR sName, CRect& rcFloat, CRect& rcDock, HWND hDockTo = NULL);
};

extern CMainFrame* pMainFrame;

/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_MAINFRM_H__F095BEBE_A897_4DA8_9344_DFBBDAA2C70B__INCLUDED_)
