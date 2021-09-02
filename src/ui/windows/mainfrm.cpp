// MainFrm.cpp : implmentation of the CMainFrame class
//
/////////////////////////////////////////////////////////////////////////////


#include "pch.h"
#include "resource.h"

#include "Root.h"

#include "aboutdlg.h"
#include "MainFrm.h"

#include "WinVGMRoot.h"

#include "PlainTextView.h"
#include "RawFileListView.h"
#include "VGMFileTreeView.h"
#include "VGMFileListView.h"
#include "VGMCollListView.h"
#include "LogListView.h"
#include "HexView.h"
#include "FileFrame.h"

using namespace std;

HANDLE killProgramSem;

CMainFrame::CMainFrame()
{
}

//destructor probably not needed stuff:
//map<VGMFile*, CItemTreeView*>::iterator p = itemViewMap.begin();
//	for (map<VGMFile*, CItemTreeView*>::iterator p = itemViewMap.begin(); p != itemViewMap.end(); p++)
//		delete *p;


BOOL CMainFrame::PreTranslateMessage(MSG* pMsg)
{
	if(baseClass::PreTranslateMessage(pMsg))
		return TRUE;

	if (m_hWnd) {
		HWND hWnd = MDIGetActive();
		if(hWnd != NULL)
			return (BOOL)::SendMessage(hWnd, WM_FORWARDMSG, 0, (LPARAM)pMsg);
	}

	return FALSE;
}

BOOL CMainFrame::OnIdle()
{
	UIUpdateToolBar();
	UIUpdateStatusBar();
	int id=ID_VIEW_PANEFIRST;
	for(_PaneWindowIter iter=m_PaneWindows.begin(); iter!=m_PaneWindows.end(); id++, iter++)
	{
		if((*iter)->IsWindow())
		{
			UISetCheck(id,(*iter)->IsWindowVisible());
		}
	}
	return FALSE;
}


//BOOL CMainFrame::IsPaused()
//{
//	return bPaused;
//}

//HRESULT CMainFrame::OnUpdateFrame()
//{
//		//C: Only update the state 10 times / second.
//	DWORD dwCurTickCount = ::GetTickCount();
//
//	if (dwCurTickCount - m_dwLastTickCount < 1000/65)
//	{
//		//Sleep(1000/70-(dwCurTickCount-m_dwLastTickCount));
//		Sleep(1);
//		return S_OK;
//	}
//
//	m_dwLastTickCount = dwCurTickCount;
//
//	for (list<RenderWnd*>::iterator iter = renderwnds.begin(); iter != renderwnds.end(); iter++)
//	{
//		(*iter)->render();
//	}
//	return S_OK;
//}

//template <class T> void CMainFrame::AddD3DWindow(D3DWindow<T> wnd)




LRESULT CMainFrame::OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
	// create command bar window
	HWND hWndCmdBar = m_CmdBar.Create(m_hWnd, rcDefault, NULL, ATL_SIMPLE_CMDBAR_PANE_STYLE);
	// attach menu
	m_CmdBar.AttachMenu(GetMenu());
	// load command bar images
	m_CmdBar.LoadImages(IDR_MAINFRAME);
	// remove old menu
	SetMenu(NULL);

	HWND hWndToolBar = CreateSimpleToolBarCtrl(m_hWnd, IDR_MAINFRAME, FALSE, ATL_SIMPLE_TOOLBAR_PANE_STYLE);

	CreateSimpleReBar(ATL_SIMPLE_REBAR_NOBORDER_STYLE);
	AddSimpleReBarBand(hWndCmdBar);
	AddSimpleReBarBand(hWndToolBar, NULL, TRUE);

	CreateSimpleStatusBar();
	m_status.SubclassWindow(m_hWndStatusBar);

	int arrPanes[] = { ID_DEFAULT_PANE, IDI_OFFSET, IDI_LENGTH };
	m_status.SetPanes(arrPanes, sizeof(arrPanes) / sizeof(int), false);

	// set status bar pane widths using local workaround
	int arrWidths[] = { 0, 150, 150 };
	SetPaneWidths(arrWidths, sizeof(arrWidths) / sizeof(int));

	// set the status bar pane icons
	m_status.SetPaneIcon(ID_DEFAULT_PANE, AtlLoadIconImage(IDI_DEFAULT, LR_DEFAULTCOLOR));
	//m_status.SetPaneIcon(IDI_OFFSET, AtlLoadIconImage(IDI_OFFSET, LR_DEFAULTCOLOR));
	//m_status.SetPaneIcon(IDI_LENGTH, AtlLoadIconImage(IDI_LENGTH, LR_DEFAULTCOLOR));

	killProgramSem = CreateSemaphore(NULL, 1, 1, NULL);

	CreateMDIClient();
	ModifyStyleEx(0, WS_EX_ACCEPTFILES);

	// If you want to only show MDI tabs when there's more than one window, uncomment the following
	//CTabbedMDIClient<TTabCtrl>::TTabOwner& tabOwner = m_tabbedClient.GetTabOwner();
	//tabOwner.SetMinTabCountForVisibleTabs(2);

	// If you want to adjust the scroll "speed", here's how you could do it:
	//TTabCtrl& tabControl = tabOwner.GetTabCtrl();
	//tabControl.SetScrollDelta(10);
	//tabControl.SetScrollRepeat(TTabCtrl::ectcScrollRepeat_Slow);

	// If you want to use the MDI child's document icon, uncomment the following:
	//m_tabbedClient.UseMDIChildIcon(TRUE);

	// If you want to only show MDI tabs when the MDI children
	// are maximized, uncomment the following:
	//m_tabbedClient.HideMDITabsWhenMDIChildNotMaximized(TRUE);

	m_tabbedClient.SetTabOwnerParent(m_hWnd);
	BOOL bSubclass = m_tabbedClient.SubclassWindow(m_hWndMDIClient);
	bSubclass; // avoid warning on level 4
	//m_CmdBar.UseMaxChildDocIconAndFrameCaptionButtons(true);
	m_CmdBar.UseMaxChildDocIconAndFrameCaptionButtons(false);
	m_CmdBar.SetMDIClient(m_hWndMDIClient);

	UIAddToolBar(hWndToolBar);
	UISetCheck(ID_VIEW_TOOLBAR, 1);
	UISetCheck(ID_VIEW_STATUS_BAR, 1);

	UIEnable(ID_FILE_SAVE, 0);
	UIEnable(ID_STOP, 0);
	UIEnable(ID_PAUSE, 0);
	UIEnable(ID_PLAY, 0);

	// register object for message filtering and idle updates
	CMessageLoop* pLoop = _Module.GetMessageLoop();
	ATLASSERT(pLoop != NULL);
	pLoop->AddMessageFilter(this);
	pLoop->AddIdleHandler(this);

	if (!musicplayer.Init(m_hWnd))
		PostQuitMessage(1);


	FileFrameVis = false;

	InitializeDockingFrame();
	InitializeDefaultPanes();




	winroot.Init();
	//winroot.SetWndPtrs(this);

	PostMessage(CWM_INITIALIZE);

	return 0;
}

// this workaround solves a bug in CMultiPaneStatusBarCtrl
// (in SetPanes() method) that limits the size of all panes
// after the default pane to a combined total of 100 pixels  
void CMainFrame::SetPaneWidths(int* arrWidths, int nPanes)
{
	// find the size of the borders
	int arrBorders[3];
	m_status.GetBorders(arrBorders);

	// calculate right edge of default pane
	arrWidths[0] += arrBorders[2];
	for (int i = 1; i < nPanes; i++)
		arrWidths[0] += arrWidths[i];

	// calculate right edge of remaining panes
	for (int j = 1; j < nPanes; j++)
		arrWidths[j] += arrBorders[2] + arrWidths[j - 1];

	// set the pane widths
	m_status.SetParts(m_status.m_nPanes, arrWidths);
}

LRESULT CMainFrame::OnClose(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{
	winroot.Exit();	//we need to call this here instead of in OnDestroy because it removes tree items from views before they get destroyed

	bHandled = FALSE;
	return 0;
}

void CMainFrame::CloseUpShop()
{
	//bool bSafeToClose = m_tabbedClient.SaveAllModified(true, true);
	//bool bSafeToClose = true;
	//if(!bSafeToClose)
	//{
	//	// Someone "cancelled".  Don't let DefWindowProc, or
	//	// anyone else have WM_CLOSE (the one sent to main frame)
	//	bHandled = TRUE;
	//	return 0;
	//}
	
	//WaitForSingleObject(m_hEventDead, INFINITE);
	//WaitForSingleObject(m_hThread, INFINITE);

	this->UninitializeDefaultPanes();

	m_tabbedClient.UnsubclassWindow();
}


LRESULT CMainFrame::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{
	CMessageLoop* pLoop = _Module.GetMessageLoop();
	ATLASSERT(pLoop != NULL);
	pLoop->RemoveMessageFilter(this);
	pLoop->RemoveIdleHandler(this);

	// NOTE: the pane windows will delete themselves,
	//  so we just need to remove them from the list
	m_PaneWindows.erase(m_PaneWindows.begin(),m_PaneWindows.end());

	for(_PaneWindowIconsIter iter=m_PaneWindowIcons.begin(); iter!=m_PaneWindowIcons.end(); iter++)
	{
		::DestroyIcon(*iter);
		*iter = NULL;
	}
	m_PaneWindowIcons.erase(m_PaneWindowIcons.begin(), m_PaneWindowIcons.end());

#ifdef DF_FOCUS_FEATURES
		m_focusHandler.RemoveHook(m_hWnd);
#endif

	//map<VGMFile*, CItemTreeView*> itemViewMap;
	//map<HWND, CItemTreeView*> hwndToItemTreeView;
	//for (map<VGMFile*, CItemTreeView*>::iterator iter = itemViewMap.begin(); iter != itemViewMap.end(); iter++)
	//	delete iter->second;
		//iter->second->DestroyWindow();
	//for (map<HWND, CItemTreeView*>::iterator iter = hwndToItemTreeView.begin(); iter != hwndToItemTreeView.end(); iter++)
	//	*iter->second->DestroyWindow();
	//hwndToItemTreeView.clear();
	PostQuitMessage(0);

	bHandled = TRUE;
	return 0;
}

LRESULT CMainFrame::OnSettingChange(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
{
	// Call the base class handler first
	BOOL bBaseHandled = FALSE;
	baseClass::OnSettingChange(uMsg,wParam,lParam,bBaseHandled);

	// Then broadcast to every descendant
	// (NOTE: If there are other top level windows,
	// they need to handle WM_SETTINGCHANGE themselves)
	this->SendMessageToDescendants(uMsg, wParam, lParam, TRUE);

	return 0;
}

LRESULT CMainFrame::OnSysColorChange(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
	// If baseClass defines OnSysColorChange, uncomment following code,
	// and removed "bHandled = FALSE;"
	bHandled = FALSE;

	//// Call the base class handler first
	//BOOL bBaseHandled = FALSE;
	//baseClass::OnSysColorChange(uMsg,wParam,lParam,bBaseHandled);

	// Then broadcast to every descendant
	// (NOTE: If there are other top level windows,
	// they need to handle WM_SETTINGCHANGE themselves)
	this->SendMessageToDescendants(uMsg, wParam, lParam, TRUE);

	return 0;
}

LRESULT CMainFrame::OnQueryEndSession(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
	m_tabbedClient.CloseAll();
	return 0;
}

LRESULT CMainFrame::OnInitialize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
	sstate::CDockWndMgr mgrDockWnds;

	//fileView.Create(m_hWnd, rcDefault, _T("File Hex View"), WS_CHILD | /*WS_VISIBLE |*/ WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_HSCROLL | WS_VSCROLL, WS_EX_CLIENTEDGE);

	for(_PaneWindowIter iter=m_PaneWindows.begin(); iter!=m_PaneWindows.end(); iter++)
	{
		mgrDockWnds.Add(sstate::CDockingWindowStateAdapter<CTabbedAutoHideDockingWindow>(*(*iter)));
	}

	// NOTE: If you want to match "nCmdShow" from the Run call, pass that to here
	//  and use instead of SW_SHOWDEFAULT.  Or you can force something like SW_SHOWMAXIMIZED.
	m_stateMgr.Initialize(_T("SOFTWARE\\VGMTrans\\VGMTrans"),m_hWnd, SW_SHOWDEFAULT);
	m_stateMgr.Add(sstate::CRebarStateAdapter(m_hWndToolBar));
	m_stateMgr.Add(sstate::CToggleWindowAdapter(m_hWndStatusBar));
	m_stateMgr.Add(mgrDockWnds);
	m_stateMgr.Restore();
	UpdateLayout();
	return 0;
}

LRESULT CMainFrame::OnFileOpen(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	std::wstring filename = pRoot->UI_GetOpenFilePath();
	if (!filename.empty()) {
		pRoot->OpenRawFile(filename);
	}
	return 0;
}

LRESULT CMainFrame::OnFileSave(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	VGMFile* pvgmfile = GetActiveFile();
	if (pvgmfile != NULL)
		pvgmfile->OnSaveAsRaw();
	return 0;
}

LRESULT CMainFrame::OnFileExit(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	PostMessage(WM_CLOSE);
	return 0;
}

//LRESULT CMainFrame::OnFileNew(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//{
void CMainFrame::ShowVGMFileFrame(VGMFile* vgmfile)
{
	//map<VGMFile*, CFileFrame*>::iterator p = frameMap.find(vgmfile);
	//if (p != frameMap.end())
	if (frameMap[vgmfile])
	{
		//TODO bring it into bring focus
	}
	else
	{
		CFileFrame* pChild = new CFileFrame(vgmfile);
		frameMap[vgmfile] = pChild;
		pChild->CreateEx(m_hWndClient);
		pChild->SetCommandBarCtrlForContextMenu(&m_CmdBar);

		pChild->SetTitle(vgmfile->GetName()->c_str());
	}

	UIEnable(ID_FILE_SAVE, 1);
}
/*
void CMainFrame::CloseVGMFileFrame(VGMFile* vgmfile)
{
	//map<VGMFile*, CFileFrame*>::iterator p = frameMap.find(vgmfile);
	//if (p != frameMap.end())
	if (frameMap[vgmfile])
	{
		//TODO bring it into bring focus
	}
	else
	{
		CFileFrame* pChild = new CFileFrame(vgmfile);
		globTest = pChild;
		frameMap[vgmfile] = pChild;
		pChild->CreateEx(m_hWndClient);
		pChild->SetCommandBarCtrlForContextMenu(&m_CmdBar);

		USES_CONVERSION;
		pChild->SetTitle(A2W(vgmfile->GetName()->c_str()));
	}
}*/

void CMainFrame::OnCloseVGMFileFrame(VGMFile* vgmfile)
{
	frameMap.erase(frameMap.find(vgmfile));
	if (frameMap.size() > 0)
		UIEnable(ID_FILE_SAVE, 1);
	else
		UIEnable(ID_FILE_SAVE, 0);
}


/*void CMainFrame::ShowFileHexView(bool bVis)
{
	if (bVis)
	{
		if (!fileView.IsWindowVisible())
		{
			m_pFileFrame.CreateEx(m_hWndClient);
			m_pFileFrame.SetCommandBarCtrlForContextMenu(&m_CmdBar);
		}
		else
			m_pFileFrame.SetFocus();
	}
	else
		m_pFileFrame.OnFileClose(0, 0, 0, *((BOOL*)0));
}*/


//LRESULT CMainFrame::OnViewFileHexView(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//{
	//ShowFileHexView(!fileView.IsWindowVisible());
	/*if (!fileView.IsWindowVisible())
	{
		m_pFileFrame.CreateEx(m_hWndClient);
		m_pFileFrame.SetCommandBarCtrlForContextMenu(&m_CmdBar);
	}
	else
		m_pFileFrame.OnFileClose(0, 0, 0, *((BOOL*)0));
		//m_FileFrame->SendMessage(ID_FILE_CLOSE);
		//SendMessage(m_tabbedClient.m_hWnd, ID_FILE_CLOSE, 0, 0);*/
//	return 0;
//}

LRESULT CMainFrame::OnViewToolBar(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	static BOOL bVisible = TRUE;	// initially visible
	bVisible = !bVisible;
	CReBarCtrl rebar = m_hWndToolBar;
	int nBandIndex = rebar.IdToIndex(ATL_IDW_BAND_FIRST + 1);	// toolbar is 2nd added band
	rebar.ShowBand(nBandIndex, bVisible);
	UISetCheck(ID_VIEW_TOOLBAR, bVisible);
	UpdateLayout();
	return 0;
}

LRESULT CMainFrame::OnViewStatusBar(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	BOOL bVisible = !::IsWindowVisible(m_hWndStatusBar);
	::ShowWindow(m_hWndStatusBar, bVisible ? SW_SHOWNOACTIVATE : SW_HIDE);
	UISetCheck(ID_VIEW_STATUS_BAR, bVisible);
	UpdateLayout();
	return 0;
}

LRESULT CMainFrame::OnPlay(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	winroot.Play();
	return 0;
}

LRESULT CMainFrame::OnPause(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	winroot.Pause();
	return 0;
}



LRESULT CMainFrame::OnStop(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	winroot.Stop();
	return 0;
}




LRESULT CMainFrame::OnAppAbout(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	CAboutDlg dlg;
	dlg.DoModal();
	return 0;
}

LRESULT CMainFrame::OnWindowCascade(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	MDICascade();
	return 0;
}

LRESULT CMainFrame::OnWindowTile(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	MDITile();
	return 0;
}

LRESULT CMainFrame::OnWindowArrangeIcons(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	MDIIconArrange();
	return 0;
}

void CMainFrame::SetStatusBarIcon(int iconID)
{
	m_status.SetPaneIcon(ID_DEFAULT_PANE, AtlLoadIconImage(iconID, LR_DEFAULTCOLOR));
}

void CMainFrame::WriteStatusBar(const wchar_t* string, int iconID)
{
	m_status.SetPaneText(ID_DEFAULT_PANE, string);
	m_status.SetPaneIcon(ID_DEFAULT_PANE, AtlLoadIconImage(iconID, LR_DEFAULTCOLOR));
}

void CMainFrame::WriteStatusBarOffsetAndLength(ULONG offset, ULONG length)
{
	USES_CONVERSION;
	CString stroffset, strlength;
	stroffset.Format(_T("Offset: 0x%X"), offset);
	strlength.Format(_T("Length: 0x%X"), length);
	m_status.SetPaneText(IDI_OFFSET, stroffset);
	m_status.SetPaneText(IDI_LENGTH, strlength);
}

void CMainFrame::WriteItemToStatusBar(VGMItem* item)
{
	if (item)
	{
		WriteStatusBar(item->GetDescription().c_str(), item->GetIcon() + IDI_UNKNOWN);
		WriteStatusBarOffsetAndLength(item->dwOffset, item->unLength);
	}
}

void CMainFrame::InitializeDefaultPanes(void)
{
	CRect rcClient;
	this->GetClientRect(&rcClient);

	CRect rcFloat(0,0,400,200);
	CRect rcDock(0,0,220,/*rcClient.Width()-200*/200);
	CRect rcFileTreeDock(0,0,220,10);
	CRect rcItemListDock(0,0,150,300);
	CRect rcvgmfileDock(0,0,220,/*rcClient.Width()-200*/300);
	CRect rcCollListDock(150,150,220,/*rcClient.Width()-200*/100);
	CRect rcLogListDock(0,0,400,80);
	//CRect rcDock();

	CImageList ilIcons;
	ilIcons.Create(16, 16, ILC_MASK | ILC_COLOR24, 0, 0);
	CBitmap bmpIcons;
	bmpIcons.LoadBitmap(IDB_TAB_ICONS);
	ilIcons.Add((HBITMAP)bmpIcons, RGB(0,255,0));

	HWND hWndFirst = 
	//CreateVGMFilesTreeViewPane(VGMFilesView, _T("Detected VGM Files"), ilIcons.ExtractIcon(10), rcFloat, rcvgmfileDock, NULL);
	CreateVGMFileListViewPane(theVGMFileListView, _T("Detected Music Files"), ilIcons.ExtractIcon(10), rcFloat, rcvgmfileDock, NULL);
	CreateFileListViewPane(rawFileListView,      _T("Scanned Files"),      ilIcons.ExtractIcon(6),  rcFloat, rcFileTreeDock, NULL);
	CreateVGMCollListViewPane(theVGMCollListView, _T("Collections"), ilIcons.ExtractIcon(10), rcFloat, rcCollListDock, NULL);

	//this->CreatePlainTextOutputPane(m_OutputView,        _T("Output"),         ilIcons.ExtractIcon(3),  rcFloat, rcDock, hWndFirst);
	//this->CreatePlainTextOutputPane(m_FindResultsView,   _T("Find Results 1"), ilIcons.ExtractIcon(11), rcFloat, rcDock, hWndFirst);
	CreateCollDialogPane(theCollDialog, _T("Coll Info"),  rcFloat, rcDock, NULL);

	CreateLogListViewPane(theLogListView, _T("Logs"), ilIcons.ExtractIcon(10), rcFloat, rcLogListDock, NULL);

}

void CMainFrame::UninitializeDefaultPanes(void)
{
	rawFileListView.SendMessage(WM_CLOSE);
	theVGMFileListView.SendMessage(WM_CLOSE);
	//itemTreeView.SendMessage(WM_CLOSE);

	if(rawFileListView.IsWindow())
	{
		rawFileListView.DestroyWindow();
	}
	if(theVGMFileListView.IsWindow())
	{
		theVGMFileListView.DestroyWindow();
	}
	if (theVGMCollListView.IsWindow())
	{
		theVGMCollListView.DestroyWindow();
	}
	//if(itemTreeView.IsWindow())
	//{
	//	itemTreeView.DestroyWindow();
	//}
	/*if(m_OutputView.IsWindow())
	{
		m_OutputView.DestroyWindow();
	}
	if(m_FindResultsView.IsWindow())
	{
		m_FindResultsView.DestroyWindow();
	}*/
	if(theCollDialog.IsWindow())
	{
		theCollDialog.DestroyWindow();
	}
	if(theLogListView.IsWindow())
	{
		theLogListView.DestroyWindow();
	}
}

HWND CMainFrame::CreatePlainTextOutputPane(CPlainTextView& view, LPCTSTR sName, HICON hIcon, CRect& rcFloat, CRect& rcDock, HWND hDockTo)
{
	HWND hWndPane = NULL;

	// Task List
	CTabbedAutoHideDockingWindow* pPaneWindow = CTabbedAutoHideDockingWindow::CreateInstance();
	if(pPaneWindow)
	{
		DWORD dwStyle=WS_OVERLAPPEDWINDOW | WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
		hWndPane = pPaneWindow->Create(m_hWnd,rcFloat,sName,dwStyle);
		DockWindow(
			*pPaneWindow,
			dockwins::CDockingSide(dockwins::CDockingSide::sBottom),
			0 /*nBar*/,
			float(0.0)/*fPctPos*/,
			rcDock.Width() /* nWidth*/,
			rcDock.Height() /* nHeight*/);

		view.Create(hWndPane, rcDefault, NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_HSCROLL | WS_VSCROLL | ES_AUTOHSCROLL | ES_AUTOVSCROLL | ES_MULTILINE | ES_NOHIDESEL, WS_EX_CLIENTEDGE);
		view.SetIcon(hIcon, ICON_SMALL);

		m_PaneWindowIcons.insert(m_PaneWindowIcons.end(), hIcon);
		m_PaneWindows.insert(m_PaneWindows.end(), pPaneWindow);

		pPaneWindow->SetReflectNotifications(false);
		pPaneWindow->SetClient(view);

		if(hDockTo)
		{
			pPaneWindow->DockTo(hDockTo, (int)m_PaneWindows.size());
		}	

	} 

	return hWndPane;
}

HWND CMainFrame::CreateFileListViewPane(CRawFileListView& view, LPCTSTR sName, HICON hIcon, CRect& rcFloat, CRect& rcDock, HWND hDockTo)
{
	HWND hWndPane = NULL;

	// Task List
	CTabbedAutoHideDockingWindow* pPaneWindow = CTabbedAutoHideDockingWindow::CreateInstance();
	if(pPaneWindow)
	{
		DWORD dwStyle=WS_OVERLAPPEDWINDOW | WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
		hWndPane = pPaneWindow->Create(m_hWnd,rcFloat,sName,dwStyle);
		DockWindow(
			*pPaneWindow,
			dockwins::CDockingSide(dockwins::CDockingSide::sLeft),
			0 /*nBar*/,
			float(0.0)/*fPctPos*/,
			rcDock.Width() /* nWidth*/,
			rcDock.Height() /* nHeight*/);

		view.Create(hWndPane, rcDefault, NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | /*WS_HSCROLL |*/ WS_VSCROLL | WS_EX_CLIENTEDGE); //| TVS_HASBUTTONS);
		view.SetIcon(hIcon, ICON_SMALL);


		m_PaneWindowIcons.insert(m_PaneWindowIcons.end(), hIcon);
		m_PaneWindows.insert(m_PaneWindows.end(), pPaneWindow);

		pPaneWindow->SetReflectNotifications(true);
		pPaneWindow->SetClient(view);

		if(hDockTo)
		{
			pPaneWindow->DockTo(hDockTo, (int)m_PaneWindows.size());
		}	
	}

	return hWndPane;
}

HWND CMainFrame::CreateVGMFilesTreeViewPane(CVGMFileTreeView& view, LPCTSTR sName, HICON hIcon, CRect& rcFloat, CRect& rcDock, HWND hDockTo)
{
	HWND hWndPane = NULL;

	// Task List
	CTabbedAutoHideDockingWindow* pPaneWindow = CTabbedAutoHideDockingWindow::CreateInstance();
	if(pPaneWindow)
	{
		DWORD dwStyle=WS_OVERLAPPEDWINDOW | WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
		hWndPane = pPaneWindow->Create(m_hWnd,rcFloat,sName,dwStyle);
		DockWindow(
			*pPaneWindow,
			dockwins::CDockingSide(dockwins::CDockingSide::sLeft),
			0 /*nBar*/,
			float(0.0)/*fPctPos*/,
			rcDock.Width() /* nWidth*/,
			rcDock.Height() /* nHeight*/);

		view.Create(hWndPane, rcDefault, NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN /*| WS_HSCROLL*/ | WS_VSCROLL | WS_EX_CLIENTEDGE | TVS_SHOWSELALWAYS); //| TVS_HASBUTTONS);
		view.SetIcon(hIcon, ICON_SMALL);

		m_PaneWindowIcons.insert(m_PaneWindowIcons.end(), hIcon);
		m_PaneWindows.insert(m_PaneWindows.end(), pPaneWindow);

		pPaneWindow->SetReflectNotifications(true);
		pPaneWindow->SetClient(view);

		if(hDockTo)
		{
			pPaneWindow->DockTo(hDockTo, (int)m_PaneWindows.size());
		}	
	}

	return hWndPane;
}

HWND CMainFrame::CreateVGMFileListViewPane(CVGMFileListView& view, LPCTSTR sName, HICON hIcon, CRect& rcFloat, CRect& rcDock, HWND hDockTo)
{
	HWND hWndPane = NULL;

	// Task List
	CTabbedAutoHideDockingWindow* pPaneWindow = CTabbedAutoHideDockingWindow::CreateInstance();
	if(pPaneWindow)
	{
		DWORD dwStyle=WS_OVERLAPPEDWINDOW | WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
		hWndPane = pPaneWindow->Create(m_hWnd,rcFloat,sName,dwStyle);
		DockWindow(
			*pPaneWindow,
			dockwins::CDockingSide(dockwins::CDockingSide::sLeft),
			0 /*nBar*/,
			float(0.0)/*fPctPos*/,
			rcDock.Width() /* nWidth*/,
			rcDock.Height() /* nHeight*/);

		view.Create(hWndPane, rcDefault, NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN |/* WS_HSCROLL |*/ WS_VSCROLL | WS_EX_CLIENTEDGE); //| TVS_HASBUTTONS);
		view.SetIcon(hIcon, ICON_SMALL);

		m_PaneWindowIcons.insert(m_PaneWindowIcons.end(), hIcon);
		m_PaneWindows.insert(m_PaneWindows.end(), pPaneWindow);

		pPaneWindow->SetReflectNotifications(true);
		pPaneWindow->SetClient(view);

		if(hDockTo)
		{
			pPaneWindow->DockTo(hDockTo, (int)m_PaneWindows.size());
		}	
	}

	return hWndPane;
}

HWND CMainFrame::CreateVGMCollListViewPane(CVGMCollListView& view, LPCTSTR sName, HICON hIcon, CRect& rcFloat, CRect& rcDock, HWND hDockTo)
{
	HWND hWndPane = NULL;

	// Task List
	CTabbedAutoHideDockingWindow* pPaneWindow = CTabbedAutoHideDockingWindow::CreateInstance();
	if(pPaneWindow)
	{
		DWORD dwStyle=WS_OVERLAPPEDWINDOW | WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
		hWndPane = pPaneWindow->Create(m_hWnd,rcFloat,sName,dwStyle);
		DockWindow(
			*pPaneWindow,
			dockwins::CDockingSide(dockwins::CDockingSide::sBottom),
			0 /*nBar*/,
			float(0.0)/*fPctPos*/,
			rcDock.Width() /* nWidth*/,
			rcDock.Height() /* nHeight*/);

		view.Create(hWndPane, rcDefault, NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_HSCROLL | WS_VSCROLL | WS_EX_CLIENTEDGE); //| TVS_HASBUTTONS);
		view.SetIcon(hIcon, ICON_SMALL);

		m_PaneWindowIcons.insert(m_PaneWindowIcons.end(), hIcon);
		m_PaneWindows.insert(m_PaneWindows.end(), pPaneWindow);

		pPaneWindow->SetReflectNotifications(true);
		pPaneWindow->SetClient(view);

		if(hDockTo)
		{
			pPaneWindow->DockTo(hDockTo, (int)m_PaneWindows.size());
		}	
	}

	return hWndPane;
}

HWND CMainFrame::CreateCollDialogPane(CCollDialog& dlg, LPCTSTR sName, CRect& rcFloat, CRect& rcDock, HWND hDockTo)
{
	HWND hWndPane = NULL;

	// Task List
	CTabbedAutoHideDockingWindow* pPaneWindow = CTabbedAutoHideDockingWindow::CreateInstance();
	if(pPaneWindow)
	{
		DWORD dwStyle=WS_OVERLAPPEDWINDOW | WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
		hWndPane = pPaneWindow->Create(m_hWnd,rcFloat,sName,dwStyle);
		DockWindow(
			*pPaneWindow,
			dockwins::CDockingSide(dockwins::CDockingSide::sBottom),
			0 /*nBar*/,
			float(0.0)/*fPctPos*/,
			rcDock.Width() /* nWidth*/,
			rcDock.Height() /* nHeight*/);

		dlg.Create(hWndPane);
		//m_CollDialog.SetIcon(hIcon, ICON_SMALL);

		//m_PaneWindowIcons.insert(m_PaneWindowIcons.end(), hIcon);
		m_PaneWindows.insert(m_PaneWindows.end(), pPaneWindow);

		pPaneWindow->SetReflectNotifications(false);
		pPaneWindow->SetClient(dlg);

		if(hDockTo)
		{
			pPaneWindow->DockTo(hDockTo, (int)m_PaneWindows.size());
		}	
	}
	return hWndPane;
}

HWND CMainFrame::CreateLogListViewPane(CLogListView& view, LPCTSTR sName, HICON hIcon, CRect& rcFloat, CRect& rcDock, HWND hDockTo)
{
	HWND hWndPane = NULL;

	// Task List
	CTabbedAutoHideDockingWindow* pPaneWindow = CTabbedAutoHideDockingWindow::CreateInstance();
	if(pPaneWindow)
	{
		DWORD dwStyle=WS_OVERLAPPEDWINDOW | WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
		hWndPane = pPaneWindow->Create(m_hWnd,rcFloat,sName,dwStyle);
		DockWindow(
			*pPaneWindow,
			dockwins::CDockingSide(dockwins::CDockingSide::sBottom),
			0 /*nBar*/,
			float(0.0)/*fPctPos*/,
			rcDock.Width() /* nWidth*/,
			rcDock.Height() /* nHeight*/);

		view.Create(hWndPane, rcDefault, NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN |/* WS_HSCROLL |*/ WS_VSCROLL | WS_EX_CLIENTEDGE); //| TVS_HASBUTTONS);
		view.SetIcon(hIcon, ICON_SMALL);

		m_PaneWindowIcons.insert(m_PaneWindowIcons.end(), hIcon);
		m_PaneWindows.insert(m_PaneWindows.end(), pPaneWindow);

		pPaneWindow->SetReflectNotifications(true);
		pPaneWindow->SetClient(view);

		if(hDockTo)
		{
			pPaneWindow->DockTo(hDockTo, (int)m_PaneWindows.size());
		}

		pPaneWindow->PinUp(dockwins::CDockingSide::sBottom, 100, false);
	}

	return hWndPane;
}

void CMainFrame::OnAddVGMFile(VGMFile* vgmfile)
{
	//CItemTreeView* newItemView = new CItemTreeView();
	//itemViewMap[vgmfile] = newItemView;
	//newItemView->Create(hwndItemTreePane, rcDefault, NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_HSCROLL | WS_VSCROLL | WS_EX_CLIENTEDGE | TVS_SHOWSELALWAYS | TVS_HASBUTTONS);
	//hwndToItemTreeView[newItemView->m_hWnd] = newItemView;

	//m_ItemView.SetIcon(hIcon, ICON_SMALL);
}

void CMainFrame::OnRemoveVGMFile(VGMFile* vgmfile)
{
	CFileFrame* theFileFrame = frameMap[vgmfile];
	if (theFileFrame)
		theFileFrame->OnFileClose(0, 0, 0, *((BOOL*)0));
			//m_tabbedClient.GetTabOwner().RemoveTab(thevgmfileFrame->m_hWnd);
			//frameMap[vgmfile]->SendMessageW(ID_FILE_CLOSE);//DestroyWindow();
			//delete theView;	I'm under the impression that MDI implementation of wtl does this for me
}


//TODO:  Change this to a VGMFile* param?
void CMainFrame::SelectItem(VGMItem* pItem)
{
	if (!pItem)
		return;
	WriteItemToStatusBar(pItem);
	if (pItem->GetType() == VGMItem::ITEMTYPE_VGMFILE)						//if we were passed a VGMFile
	{
		CFileFrame* selFrame = frameMap[(VGMFile*)pItem];
		if (selFrame)
			m_tabbedClient.GetTabOwner().DisplayTab(frameMap[(VGMFile*)pItem]->m_hWnd, FALSE);//.GetTabCtrl().SetCurSel(0);
	}
	//call SelectItem for the item's FileFrame, if it exists
	CFileFrame* selectedItemsFrame = frameMap[pItem->vgmfile];
	if (selectedItemsFrame)
		selectedItemsFrame->SelectItem(pItem);
	theVGMFileListView.SelectItem(pItem);
}

void CMainFrame::SelectColl(VGMColl* coll)
{
	if (!coll)
	{
		UIEnable(ID_PLAY, 0);
		theCollDialog.Clear();
		return;
	}
	else
	{
		UIEnable(ID_PLAY, 1);
		theCollDialog.DisplayCollection(coll);	//update the Coll Info dialog with this collection
		//WriteItemToStatusBar(pItem);
	}
		
}

VGMFile* CMainFrame::GetActiveFile(void)
{
	HWND hActiveTab = MDIGetActive();
	if (hActiveTab != NULL)
	{
		map<VGMFile*, CFileFrame*>::iterator it = frameMap.begin();
		while(it != frameMap.end())
		{
			if (hActiveTab == (*it).second->m_hWnd)
			{
				return (*it).first;
			}
			++it;
		}
	}
	return NULL;
}

// CDropFilesHandler

BOOL CMainFrame::IsReadyForDrop(void)
{
	return TRUE;
}

BOOL CMainFrame::HandleDroppedFile(LPCTSTR szBuff)
{
	//ATLTRACE("%s\n", szBuff);
	//USES_CONVERSION;
	//char* str = W2A(szBuff); 
	winroot.OpenRawFile(szBuff);
	return TRUE;
}

void CMainFrame::EndDropFiles(void)
{
}
