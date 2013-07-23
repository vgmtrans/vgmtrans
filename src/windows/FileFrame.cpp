#include "stdafx.h"
#include "FileFrame.h"
#include "mainfrm.h"
#include "VGMFile.h"
#include "VGMFileListView.h"
#include "VGMSeq.h"

CFileFrame::CFileFrame(VGMFile* theVGMFile)
: m_vgmfile(theVGMFile), m_pCmdBar(NULL), m_nHexViewTabIndex(-1)
{
}

LRESULT CFileFrame::OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
	LRESULT lRet = DefWindowProc(uMsg, wParam, lParam);
	bHandled = TRUE;

	CreateTabWindow(m_hWnd, rcDefault, (CTCS_BOTTOM | CTCS_TOOLTIPS | CTCS_HOTTRACK));

	//fileView.SetParent(m_hWnd);
	//fileView.ShowWindow(true);
	m_HexViewFrame.SetCurFile(m_vgmfile);
	m_HexViewFrame.Create(m_hWnd, rcDefault, _T("HexView Frame"), WS_CHILD | /*WS_VISIBLE |*/ WS_CLIPSIBLINGS | WS_CLIPCHILDREN, WS_EX_CLIENTEDGE);
	//m_TrackViewFrame.Create(m_hWnd, rcDefault, _T("Track Frame"), WS_CHILD | /*WS_VISIBLE |*/ WS_CLIPSIBLINGS | WS_CLIPCHILDREN, WS_EX_CLIENTEDGE);


	//m_ItemView.Create(pMainFrame->hwndItemTreePane, rcDefault, NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_HSCROLL | WS_VSCROLL | WS_EX_CLIENTEDGE | TVS_SHOWSELALWAYS | TVS_HASBUTTONS);
	//m_ItemView.SetIcon(hIcon, ICON_SMALL);
	//pMainFrame->SetItemTreePaneView(m_ItemView);
	
	
	m_nHexViewTabIndex =    this->AddTab(m_HexViewFrame,       _T("Hex View"));
	//m_nTrackViewTabIndex =  this->AddTab(m_TrackViewFrame, _T("Track View"));
	this->HideTabControl();

	// NOTE: You can mark a tab item to be highlighted like the following:
	//  (its meant to work similar to TCM_HIGHLIGHTITEM for regular tab controls)
	//this->GetTabCtrl().GetItem(1)->SetHighlighted(true);

	this->UpdateTabToolTip(m_HexViewFrame, _T("Hexadecimal view with item list"));
	//this->UpdateTabToolTip(m_TrackViewFrame, _T("Track View"));

	//m_FileView.get_Control(&m_punkBrowser);
	//if(m_punkBrowser)
	//{
	//	DispEventAdvise(m_punkBrowser, &DIID_DWebBrowserEvents2);
	//}
	return lRet;
}

LRESULT CFileFrame::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{
	pMainFrame->OnCloseVGMFileFrame(m_vgmfile);

	bHandled = FALSE;
	return 0;
}

LRESULT CFileFrame::OnSettingChange(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
{
	// System settings or metrics have changed.  Propogate this message
	// to all the child windows so they can update themselves as appropriate.
	this->SendMessageToDescendants(uMsg, wParam, lParam, TRUE);

	return 0;
}

LRESULT CFileFrame::OnForwardMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/)
{
	LPMSG pMsg = (LPMSG)lParam;

	if(baseClass::PreTranslateMessage(pMsg))
		return TRUE;

	int nActiveTab = this->GetTabCtrl().GetCurSel();
	if(nActiveTab == m_nHexViewTabIndex)
	{
		return m_HexViewFrame.PreTranslateMessage(pMsg);
	}
	//else if(nActiveTab == m_nHtmlSourceViewTabIndex)
	//{
	//	return m_HtmlSourceView.PreTranslateMessage(pMsg);
	//}

	return FALSE;
}

LRESULT CFileFrame::OnShowTabContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
{
	bHandled = TRUE;

	POINT ptPopup = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};

	// Build up the menu to show
	CMenu mnuContext;

	// Load from resource
	//mnuContext.LoadMenu(IDR_CONTEXT);

	// or build dynamically
	// (being sure to enable/disable menu items as appropriate,
	// and giving the appropriate IDs)
	if(mnuContext.CreatePopupMenu())
	{
		int cchWindowText = this->GetWindowTextLength();
		CString sWindowText;
		this->GetWindowText(sWindowText.GetBuffer(cchWindowText+1), cchWindowText+1);
		sWindowText.ReleaseBuffer();

		CString sSave(_T("&Save '"));
		sSave += sWindowText;
		sSave += _T("'");

		mnuContext.AppendMenu((MF_ENABLED | MF_STRING), ID_FILE_SAVE, sSave);
		mnuContext.AppendMenu((MF_ENABLED | MF_STRING), ID_FILE_CLOSE, _T("&Close\tCtrl+F4"));
		//mnuContext.AppendMenu(MF_SEPARATOR);
		//mnuContext.AppendMenu((MF_ENABLED | MF_STRING), ID_VIEW_SOURCE, _T("&View Source"));

		if(m_pCmdBar != NULL)
		{
			// NOTE: The CommandBarCtrl in our case is the mainframe's, so the commands
			//  would actually go to the main frame if we don't specify TPM_RETURNCMD.
			//  In the main frame's message map, if we don't specify
			//  CHAIN_MDI_CHILD_COMMANDS, we are not going to see those command
			//  messages. We have 2 choices here - either specify TPM_RETURNCMD,
			//  then send/post the message to our window, or don't specify
			//  TPM_RETURNCMD, and be sure to have CHAIN_MDI_CHILD_COMMANDS
			//  in the main frame's message map.

			//m_pCmdBar->TrackPopupMenu(mnuContext, TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_TOPALIGN | TPM_VERTICAL,
			//	ptPopup.x, ptPopup.y);

			DWORD nSelection = m_pCmdBar->TrackPopupMenu(mnuContext, TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_TOPALIGN | TPM_VERTICAL | TPM_RETURNCMD,
				ptPopup.x, ptPopup.y);
			if(nSelection != 0)
			{
				this->PostMessage(WM_COMMAND, MAKEWPARAM(nSelection, 0));
			}
		}
		else
		{
			mnuContext.TrackPopupMenuEx(TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_TOPALIGN | TPM_VERTICAL,
				ptPopup.x, ptPopup.y, m_hWnd, NULL);
		}
	}

	return 0;
}

LRESULT CFileFrame::OnFileSave(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	this->MessageBox(_T("OnFileSave"));
	return 0;
}

LRESULT CFileFrame::OnFileClose(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	this->SendMessage(WM_SYSCOMMAND, SC_CLOSE, 0L);
	return 0;
}


LRESULT CFileFrame::OnMDIActivate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
	//WM_MDIACTIVATE is sent to both the activated and deactived window...
	//in both messages, lParam == the activated window, wParam == the deactivated.
	if ((HWND)lParam == m_hWnd)	//so, if this window is truly being activated (it's not receiving a deactivation message)
	{						
		//pMainFrame->SetItemTreeViewPane(m_vgmfile);
		theVGMFileListView.SelectItem(m_vgmfile);
	}
	bHandled = FALSE;
	return 0;
}


void CFileFrame::SelectItem(VGMItem* item)
{
	m_HexViewFrame.SelectItem(item);
}