#include "pch.h"
#include "resource.h"
#include "RawFileListView.h"
#include "mainfrm.h"
#include "HexView.h"
#include "Root.h"
#include "BmpBtn.h"


CRawFileListView rawFileListView;

BOOL CRawFileListView::PreTranslateMessage(MSG* pMsg)
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

LRESULT CRawFileListView::OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
	// "base::OnCreate()"
	LRESULT lRet = DefWindowProc(uMsg, wParam, lParam);

	// "OnInitialUpdate"
	Init();

	bHandled = TRUE;
	return lRet;
}

LRESULT CRawFileListView::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{
	BOOL bDestroy = m_ImageList.Destroy();
	bDestroy; //avoid level 4 warning

	// Say that we didn't handle it so that the treeview and anyone else
	//  interested gets to handle the message
	bHandled = FALSE;
	return 0;
}


void CRawFileListView::OnPaint ( CDCHandle /*dc*/ )
{
//	this->GetItem
//	CPaintDC dc(*this);
	
//	Invalidate(false);

	

	//dc.SetBkColor ( RGB(255,153,0) );
//	dc.Rectangle(0, 30, 30, -30);
	//dc.
	SetMsgHandled(false);
}

void CRawFileListView::OnSize(UINT nType, CSize size)
{
	SetColumnWidth(0, size.cx);
	ShowScrollBar(SB_HORZ, FALSE); 
}

void CRawFileListView::Init()
{
	InsertColumn ( 0, _T("File Name"), LVCFMT_LEFT, 130, 0 );
	//InsertColumn ( 1, _T("Type"), LVCFMT_LEFT, 90, 0 );

	InitImageLists();

	  // On XP, set some additional properties of the list ctrl.
	if ( g_bXPOrLater )
    {
        // Turning on LVS_EX_DOUBLEBUFFER also enables the transparent
        // selection marquee.
        SetExtendedListViewStyle ( LVS_EX_DOUBLEBUFFER, LVS_EX_DOUBLEBUFFER );

        // Each tile will have 2 additional lines (3 lines total).
        LVTILEVIEWINFO lvtvi = { sizeof(LVTILEVIEWINFO), LVTVIM_COLUMNS };

        lvtvi.cLines = 2;
        lvtvi.dwFlags = LVTVIF_AUTOSIZE;
        SetTileViewInfo ( &lvtvi );
	}
}

void CRawFileListView::InitImageLists()
{
	nGenericFileIcon = -1;

	HICON hiGenericFile = (HICON)::LoadImage(_Module.GetResourceInstance(), 
		MAKEINTRESOURCE(IDI_RAWFILE), 
		IMAGE_ICON, 16, 16, LR_SHARED);

    BOOL bCreate = m_ImageList.Create ( 16, 16, ILC_COLOR32 | ILC_MASK, 3, 1 );
	m_ImageList.SetBkColor(GetBkColor());

    nGenericFileIcon = m_ImageList.AddIcon ( hiGenericFile );
    DestroyIcon ( hiGenericFile );
	SetImageList(m_ImageList, LVSIL_SMALL);
}



void CRawFileListView::AddFile(RawFile* newFile)
{
	LVITEMW newItem;// = new LVITEMW();
	newItem.lParam = (DWORD_PTR)newFile;
	newItem.iItem = GetItemCount();
	newItem.iSubItem = 0;
	newItem.iImage = nGenericFileIcon;
	//newItem.iGroupId = 0;
	newItem.pszText = (LPWSTR)newFile->GetFileName();
	newItem.mask = LVIF_PARAM | LVIF_TEXT | LVIF_IMAGE;// | LVIF_GROUPID;
	InsertItem(&newItem);


	//InsertItem(LVIF_PARAM | LVIF_TEXT | LVIF_IMAGE, GetItemCount(), newFile->GetFileName(), 0, 0, nIconIndexNormal, (DWORD_PTR)newFile);



//	CTreeItem newItem = this->InsertItem(newFile->GetFileName(), nIconIndexNormal, nIconIndexSelected, TVI_ROOT, NULL);
//	newItem.SetData((DWORD_PTR)newFile);

//	items[newFile] = newItem;

	//lItems.push_back(newItem);

//	CBmpBtn* pButton = new CBmpBtn(BMPBTN_AUTOSIZE, m_ImageList, GetBkColor(), (DWORD)newFile);

//	CRect rcNew;// = CRect(0,0,20,-20);
//	GetItemRect(newItem.m_hTreeItem ,&rcNew, LVIR_BOUNDS);
//	rcNew.left = rcNew.right-16;

	// change the OK button extended style
	//DWORD dw = BMPBTN_AUTOSIZE |  BMPBTN_SHAREIMAGELISTS;  /*BMPBTN_AUTO3D_SINGLE |*/
	//pButton->SetBitmapButtonExtendedStyle(dw);

	//pButton->SetImageList(m_ImageList); // OK button imagelist
//	pButton->SetImages(0);			//this can be appended to add icons for different states
//	pButton->SetToolTipText(L"Close File");
	//pButton->SubclassWindow(GetDlgItem(IDCANCEL));

//	static int closeButtonCounter = 0;

//	unsigned int id = IDC_FIRST_FILE_CLOSE_BUTTON+closeButtonCounter++;
//	IDtoTreeItem[id] = newItem;
//	TreeItemtoBtn[newItem] = pButton;
//	btns.push_back(pButton);

//	pButton->Create(m_hWnd, rcNew, L"", WS_CHILD | BS_PUSHBUTTON | WS_VISIBLE/* | BS_FLAT*/, 0, id);
//	if (closeButtonCounter > IDC_LAST_FILE_CLOSE_BUTTON - IDC_FIRST_FILE_CLOSE_BUTTON)
//		closeButtonCounter = 0;
	//pButton->Create(m_hWnd, rcNew, L"My Button", BS_FLAT);//m_nIDCount++);
	    
	//pButton->ShowWindow(SW_SHOWNORMAL);
}

void CRawFileListView::RemoveFile(RawFile* theFile)
{
//	IDtoTreeItem.erase(GetDlgItem(TreeItemtoBtn[items[theFile]]->m_hWnd));
	//TreeItemtoBtn[newItem] = pButton;
	//for (list<CBmpBtn*>::iterator iter = btns.begin(); iter != btns.end(); ++iter)
	//{
	//	iter->

	LVFINDINFO findinfo;
	findinfo.flags = LVFI_PARAM;
	findinfo.lParam = (DWORD_PTR)theFile;
	DeleteItem(FindItem(&findinfo, -1));//items[theFile]);

	//items[theFile].Delete();			//remove CTreeItem from view
	//items.erase(items.find(theFile));	//remove the CTreeItem from the item map
}

void CRawFileListView::OnKeyDown(TCHAR nChar, UINT repeats, UINT code)
{
	switch (nChar)
	{
	case VK_DELETE:
		OnCloseFile(0, 0, 0);
		//SendMessage(IDC_CLOSEFILE, 0, 0);
		break;
	}
}


//LRESULT CRawFileListView::OnCloseButton(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled)
//{
//	pRoot->CloseRawFile((RawFile*)IDtoTreeItem[wID].GetData());		//if WTL had FromHandle() (to get an object from HWND) there'd be no need for this map<> shit.  but, alas
//	bHandled = false;
//	return 0;
//}

/*LRESULT CRawFileListView::OnCloseButtonClick(int idCtrl, LPNMHDR pnmh, BOOL& bHandled)
{
	int i = 0;
	bHandled = false;
	return 0;
}*/

LRESULT CRawFileListView::OnTvnSelchanged(int idCtrl, LPNMHDR pnmh, BOOL& bHandled)
{
	CTreeViewCtrlEx* treeview = reinterpret_cast<CTreeViewCtrlEx*>(pnmh);

	CTreeItem treeitem = treeview->GetSelectedItem();
	//fileView.SetCurFile((RawFile*)treeitem.GetData());

	bHandled = false;
	return 0;
}



LRESULT CRawFileListView::OnNMRClick(int idCtrl, LPNMHDR pnmh, BOOL& bHandled)
{
	SendMessage(WM_CONTEXTMENU, (WPARAM)m_hWnd, GetMessagePos());
	return 0;
}


LRESULT CRawFileListView::OnContextMenu(HWND hwndCtrl, CPoint ptClick )
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
	int iItem = HitTest( ptClick, &uFlags );

	if( iItem == -1 )
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

void CRawFileListView::OnSaveAsRaw(UINT uCode, int nID, HWND hwndCtrl)
{
	std::vector<RawFile*> files;
	for (int i = GetNextItem(-1, LVNI_SELECTED); i != -1; i = GetNextItem(i, LVNI_SELECTED) )
	{
		LVITEM item;
		item.iItem = i;
		item.iSubItem = 0;
		item.mask = LVIF_PARAM;
		GetItem(&item);
		files.push_back((RawFile*)item.lParam);
	}

	for (std::vector<RawFile*>::iterator it = files.begin(); it != files.end(); ++it)
	{
		(*it)->OnSaveAsRaw();
	}
}

void CRawFileListView::OnCloseFile(UINT uCode, int nID, HWND hwndCtrl)
{
	int iItem;
	while ((iItem = GetNextItem(-1, LVNI_SELECTED)) != -1)
	{
		LVITEM item;
		item.iItem = iItem;
		item.iSubItem = 0;
		item.mask = LVIF_PARAM;
		GetItem(&item);
		pRoot->CloseRawFile((RawFile*)item.lParam);
	}
}
