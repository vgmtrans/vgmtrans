#include "pch.h"
#include "resource.h"
#include "RawFileTreeView.h"
#include "mainfrm.h"
#include "HexView.h"
#include "Root.h"
#include "BmpBtn.h"

using namespace std;

CRawFileTreeView rawFileTreeView;

BOOL CRawFileTreeView::PreTranslateMessage(MSG* pMsg)
{
	if(pMsg)
	{
		if((pMsg->hwnd == m_hWnd) || ::IsChild(m_hWnd, pMsg->hwnd))
		{
			// We'll have the Accelerator send the WM_COMMAND to our view
			//if(m_hAccel != NULL && ::TranslateAccelerator(m_hWnd, m_hAccel, pMsg))
			//{
			//	return TRUE;
			//}
		}
	}
	return FALSE;
}

LRESULT CRawFileTreeView::OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
	// "base::OnCreate()"
	LRESULT lRet = DefWindowProc(uMsg, wParam, lParam);

	// "OnInitialUpdate"
	int cx = 16, cy = 16;
	BOOL bCreate = FALSE;
	bCreate = m_ImageList.Create(cx, cy, ILC_COLOR32 | ILC_MASK, 4, 4);
	if(bCreate)
	{
		Init(cx, cy);
	}

	/*CTreeItem tiRoot = this->InsertItem(_T("Root"), nIconFolderIndexNormal, nIconFolderIndexSelected, TVI_ROOT, NULL);

	CTreeItem tiFolder1 = this->InsertItem(_T("Folder"), nIconFolderIndexNormal, nIconFolderIndexSelected, tiRoot, NULL);
	this->InsertItem(_T("Item"), nIconIndexNormal, nIconIndexSelected, tiFolder1, NULL);

	CTreeItem tiFolder2 = this->InsertItem(_T("Folder"), nIconFolderIndexNormal, nIconFolderIndexSelected, tiRoot, NULL);
	this->InsertItem(_T("Item"), nIconIndexNormal, nIconIndexSelected, tiFolder2, NULL);
	this->InsertItem(_T("Item"), nIconIndexNormal, nIconIndexSelected, tiFolder2, NULL);

	tiRoot.Expand();
	tiFolder1.Expand();
	tiFolder2.Expand();*/

	bHandled = TRUE;
	return lRet;
}

LRESULT CRawFileTreeView::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{
	BOOL bDestroy = m_ImageList.Destroy();
	bDestroy; //avoid level 4 warning

	// Say that we didn't handle it so that the treeview and anyone else
	//  interested gets to handle the message
	bHandled = FALSE;
	return 0;
}

/*void CRawFileTreeView::OnPaint(HDC hDC)
{
	//CDCHandle  dc(hDC);
	//CString string;
	
	CDCHandle  dc(hDC);
    CRect      rc;
    SYSTEMTIME st;
    CString    sTime;
 
        // Get our window's client area.
        GetClientRect ( rc );
 
        // Build the string to show in the window.
        GetLocalTime ( &st );
        sTime.Format ( _T("The time is %d:%02d:%02d"), 
                       st.wHour, st.wMinute, st.wSecond );
 
        // Set up the DC and draw the text.
//        dc.SaveDC();
 
 //       dc.SetBkColor ( RGB(255,153,0) );
        dc.SetTextColor ( RGB(0,0,0) );
        dc.ExtTextOut ( 0, 0, ETO_OPAQUE, rc, sTime, 
                        sTime.GetLength(), NULL );
 
        // Restore the DC.
//        dc.RestoreDC(-1);
//	SetMsgHandled(false);
}*/

void CRawFileTreeView::OnPaint ( CDCHandle /*dc*/ )
{
//	this->GetItem
	CPaintDC dc(*this);
	
	Invalidate(false);

	

	//dc.SetBkColor ( RGB(255,153,0) );
	dc.Rectangle(0, 30, 30, -30);
	//dc.
	SetMsgHandled(false);
}

void CRawFileTreeView::OnSize(UINT nType, CSize size)
{
	ShowScrollBar(SB_HORZ, FALSE); 
}


void CRawFileTreeView::Init(int cx, int cy)
{
	nIconIndexNormal = -1;
	nIconIndexSelected = -1;
	HICON hIcon = NULL;

	// NOTE: Don't Load using the LR_LOADTRANSPARENT bit, because the icon
	//  already properly deals with transparency (setting it causes problems).
	//  We will load this as LR_SHARED so that we don't have to do a DeleteIcon.
	hIcon = (HICON)::LoadImage(
				_Module.GetResourceInstance(),
				MAKEINTRESOURCE(IDI_RAWFILE),
				IMAGE_ICON, cx, cy, LR_SHARED);
	
	nIconIndexNormal = m_ImageList.AddIcon(hIcon);
	nIconIndexSelected = nIconIndexNormal;
	m_ImageList.SetBkColor(this->GetBkColor());

	/*nIconFolderIndexNormal = -1;
	nIconFolderIndexSelected = -1;
	hIcon = (HICON)::LoadImage(
				_Module.GetResourceInstance(),
				MAKEINTRESOURCE(IDI_FOLDER_CLOSED),
				IMAGE_ICON, cx, cy, LR_SHARED);*/

	/*nIconFolderIndexNormal = m_ImageList.AddIcon(hIcon);

	hIcon = (HICON)::LoadImage(
				_Module.GetResourceInstance(),
				MAKEINTRESOURCE(IDI_FOLDER_OPEN),
				IMAGE_ICON, cx, cy, LR_SHARED);

	nIconFolderIndexSelected = m_ImageList.AddIcon(hIcon);*/

	// Hook up the image list to the tree view
	this->SetImageList(m_ImageList, TVSIL_NORMAL);
}



void CRawFileTreeView::AddFile(RawFile* newFile)
{
	CTreeItem newItem = this->InsertItem(newFile->GetFileName(), nIconIndexNormal, nIconIndexSelected, TVI_ROOT, NULL);
	newItem.SetData((DWORD_PTR)newFile);

	items[newFile] = newItem;
	//lItems.push_back(newItem);

	CBmpBtn* pButton = new CBmpBtn(BMPBTN_AUTOSIZE, m_ImageList, GetBkColor(), (DWORD)newFile);

	CRect rcNew;// = CRect(0,0,20,-20);
	GetItemRect(newItem.m_hTreeItem ,&rcNew, LVIR_BOUNDS);
	rcNew.left = rcNew.right-16;

	// change the OK button extended style
	//DWORD dw = BMPBTN_AUTOSIZE |  BMPBTN_SHAREIMAGELISTS;  /*BMPBTN_AUTO3D_SINGLE |*/
	//pButton->SetBitmapButtonExtendedStyle(dw);

	//pButton->SetImageList(m_ImageList); // OK button imagelist
	pButton->SetImages(0);			//this can be appended to add icons for different states
	pButton->SetToolTipText(L"Close File");
	//pButton->SubclassWindow(GetDlgItem(IDCANCEL));

	static int closeButtonCounter = 0;

	unsigned int id = IDC_FIRST_FILE_CLOSE_BUTTON+closeButtonCounter++;
	IDtoTreeItem[id] = newItem;
//	TreeItemtoBtn[newItem] = pButton;
//	btns.push_back(pButton);

	pButton->Create(m_hWnd, rcNew, L"", WS_CHILD | BS_PUSHBUTTON | WS_VISIBLE/* | BS_FLAT*/, 0, id);
	if (closeButtonCounter > IDC_LAST_FILE_CLOSE_BUTTON - IDC_FIRST_FILE_CLOSE_BUTTON)
		closeButtonCounter = 0;
	//pButton->Create(m_hWnd, rcNew, L"My Button", BS_FLAT);//m_nIDCount++);
	    
	//pButton->ShowWindow(SW_SHOWNORMAL);
}

void CRawFileTreeView::RemoveFile(RawFile* theFile)
{
//	IDtoTreeItem.erase(GetDlgItem(TreeItemtoBtn[items[theFile]]->m_hWnd));
	//TreeItemtoBtn[newItem] = pButton;
	//for (list<CBmpBtn*>::iterator iter = btns.begin(); iter != btns.end(); ++iter)
	//{
	//	iter->


	items[theFile].Delete();			//remove CTreeItem from view
	items.erase(items.find(theFile));	//remove the CTreeItem from the item map
}

void CRawFileTreeView::OnKeyDown(TCHAR nChar, UINT repeats, UINT code)
{
	switch (nChar)
	{
	case VK_DELETE:
		OnCloseFile(0, 0, 0);
		//SendMessage(IDC_CLOSEFILE, 0, 0);
		break;
	}
}


LRESULT CRawFileTreeView::OnCloseButton(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled)
{
	pRoot->CloseRawFile((RawFile*)IDtoTreeItem[wID].GetData());		//if WTL had FromHandle() (to get an object from HWND) there'd be no need for this map<> shit.  but, alas
	bHandled = false;
	return 0;
}

/*LRESULT CRawFileTreeView::OnCloseButtonClick(int idCtrl, LPNMHDR pnmh, BOOL& bHandled)
{
	int i = 0;
	bHandled = false;
	return 0;
}*/

LRESULT CRawFileTreeView::OnTvnSelchanged(int idCtrl, LPNMHDR pnmh, BOOL& bHandled)
{
	CTreeViewCtrlEx* treeview = reinterpret_cast<CTreeViewCtrlEx*>(pnmh);

	CTreeItem treeitem = treeview->GetSelectedItem();
	//fileView.SetCurFile((RawFile*)treeitem.GetData());

	bHandled = false;
	return 0;
}


void CRawFileTreeView::OnLButtonDblClk(UINT nFlags, CPoint point)
{
		// TODO: Add your message handler code here and/or call default
		// if Shift-F10
	if (point.x == -1 && point.y == -1)
		point = (CPoint) GetMessagePos();

	//ScreenToClient(&ptMousePos);

	UINT uFlags;


	CTreeItem item = HitTest( point, &uFlags );

	if (item == NULL)
		return;

	//pMainFrame->ShowFileHexView(true);

	//if( htItem == NULL )
	//	return;

	//VGMItem* myVGMItem;
	//VGMItemMap.Lookup(htItem, (void *&)myVGMItem);			//look up the VGMItem from the HTREEITEM key
	//myVGMItem->OnDblClick();
	//bHandled = true;
}

//void CRawFileTreeView::OnRButtonUp(UINT nFlags, CPoint point)
//{
//	SendMessage(WM_CONTEXTMENU, (WPARAM)m_hWnd, GetMessagePos());
//}

LRESULT CRawFileTreeView::OnNMRClick(int idCtrl, LPNMHDR pnmh, BOOL& bHandled)
{
	SendMessage(WM_CONTEXTMENU, (WPARAM)m_hWnd, GetMessagePos());
	return 0;
}

LRESULT CRawFileTreeView::OnContextMenu(HWND hwndCtrl, CPoint ptClick )
{
	//bHandled = TRUE;
	//CTreeViewCtrlEx* treeview = (CTreeViewCtrlEx*)hwndCtrl;

	// Build up the menu to show
	CMenu mnuContext;

	// if Shift-F10
	if (ptClick.x == -1 && ptClick.y == -1)
		ptClick = (CPoint) GetMessagePos();

	ScreenToClient(&ptClick);

	UINT uFlags;
	//HTREEITEM htItem = GetTreeCtrl().HitTest( ptMousePos, &uFlags );
	CTreeItem htItem = HitTest( ptClick, &uFlags );

	if (GetSelectedItem() == NULL)
		htItem.Select();

	if( htItem == NULL )
		return 0;

	// Load from resource
	mnuContext.LoadMenu(IDR_RAWFILE);
	CMenuHandle pPopup = mnuContext.GetSubMenu(0);
	ClientToScreen(&ptClick);
	pPopup.TrackPopupMenu( TPM_LEFTALIGN, ptClick.x, ptClick.y, hwndCtrl );

	// or build dynamically
	// (being sure to enable/disable menu items as appropriate,
	// and giving the appropriate IDs)
	/*if(mnuContext.CreatePopupMenu())
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
		mnuContext.AppendMenu(MF_SEPARATOR);
		mnuContext.AppendMenu((MF_ENABLED | MF_STRING), ID_VIEW_SOURCE, _T("&View Source"));

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
	}*/

	return 0;
}

void CRawFileTreeView::OnCloseFile(UINT uCode, int nID, HWND hwndCtrl)
{
	CTreeItem blah = GetSelectedItem();
	RawFile* blah2 = (RawFile*)blah.GetData();
	pRoot->CloseRawFile((RawFile*)GetSelectedItem().GetData());
}