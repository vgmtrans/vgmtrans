#include "stdafx.h"
#include "HexViewFrame.h"
#include "ItemTreeView.h"
#include "HexView.h"
#include "mainfrm.h"

using namespace std;

CHexViewFrame::CHexViewFrame()
: bPopulatingItemTreeView(false), m_HexView(this), m_ItemTreeView(this)
{
}

LRESULT CHexViewFrame::OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
	LRESULT lRet = DefWindowProc(uMsg, wParam, lParam);
	bHandled = TRUE;

	if (GetCurFile() == NULL)
		throw;

	// Create the splitter windows.
	const DWORD dwSplitStyle = WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
	
	m_wndSplitter.Create(*this, rcDefault, NULL, dwSplitStyle);

	m_wndSplitter.m_cxySplitBar = 0;
    //m_wndSplitter.EnablePatternBar();

	//m_MapView.Create(m_hWnd, rcDefault, _T("http://www.microsoft.com"),WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_HSCROLL | WS_VSCROLL, WS_EX_CLIENTEDGE);
	m_HexView.Create(m_wndSplitter, rcDefault, _T("Hex View"),
		WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_HSCROLL | WS_VSCROLL | WS_EX_CLIENTEDGE | CS_HREDRAW | CS_VREDRAW );

	m_ItemTreeView.Create(m_wndSplitter, rcDefault, _T("Item View"),
		WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_EX_CLIENTEDGE | TVS_SHOWSELALWAYS | TVS_HASBUTTONS |
		CS_HREDRAW | CS_VREDRAW );

	
	pair<VGMFile*, CItemTreeView*>* info = new pair<VGMFile*, CItemTreeView*>((VGMFile*)GetCurFile(), &m_ItemTreeView);
	populateItemViewThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)&CItemTreeView::PopulateItemView, info, 0, &populateItemViewThreadID);

	 // Set the horizontal splitter as the client area window.
	 m_hWndClient = m_wndSplitter;


	  // Set up the splitter panes
	m_wndSplitter.SetSplitterPanes ( m_HexView, m_ItemTreeView  );

	 UpdateLayout();

	m_wndSplitter.m_cxyMin = 670;

     m_wndSplitter.SetSplitterPos();
	 m_wndSplitter.SetSplitterExtendedStyle(SPLIT_NONINTERACTIVE);

	  // Set up the pane containers
//		m_SongView.SetClient ( m_wndFormatList );
//		m_wndIEContainer.SetClient ( wndIE );
//		m_wndTreeContainer.SetPaneContainerExtendedStyle ( PANECNT_NOCLOSEBUTTON );
//		m_wndIEContainer.SetPaneContainerExtendedStyle ( PANECNT_VERTICAL );



//		m_SongView.Init();

	return lRet;
}

LRESULT CHexViewFrame::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{
	// Let anybody else see this that wants to
	bHandled = FALSE;
	return 0;
}

LRESULT CHexViewFrame::OnSettingChange(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
{
	// System settings or metrics have changed.  Propogate this message
	// to all the child windows so they can update themselves as appropriate.
	this->SendMessageToDescendants(uMsg, wParam, lParam, TRUE);

	return 0;
}

LRESULT CHexViewFrame::OnForwardMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/)
{
	LPMSG pMsg = (LPMSG)lParam;

	//if(baseClass::PreTranslateMessage(pMsg))
	//	return TRUE;

	HWND hWndFocus = ::GetFocus();
	if(m_hWndClient != NULL && ::IsWindow(m_hWndClient) &&
		(m_hWndClient == hWndFocus || ::IsChild(m_hWndClient, hWndFocus)))
	{
		//active.PreTranslateMessage(pMsg);
		if(::SendMessage(m_hWndClient, WM_FORWARDMSG, 0, (LPARAM)pMsg))
		{
			return TRUE;
		}
	}



//		if(nActiveTab == m_nSongViewTabIndex)
//		{
//			return m_SongView.PreTranslateMessage(pMsg);
//		}
	//else if(nActiveTab == m_nHtmlSourceViewTabIndex)
	//{
	//	return m_HtmlSourceView.PreTranslateMessage(pMsg);
	//}

	return FALSE;
}

//LRESULT OnViewSource(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//{
//	this->DisplayTab(m_HtmlSourceView, FALSE);
//	return 0;
//}


void CHexViewFrame::SelectItem(VGMItem* newItem)
{
	m_HexView.SelectItem(newItem);
	m_ItemTreeView.SelectItem(newItem);
	pMainFrame->WriteItemToStatusBar(newItem);
}