#include "stdafx.h"
#include "resource.h"
#include "HexView.h"
#include "VGMFileTreeView.h"
#include "ItemTreeView.h"
//#include "MainFrm.h"
#include "WinVGMRoot.h"

CVGMFileTreeView VGMFilesView;


CVGMFileTreeView::CVGMFileTreeView()
{
}

BOOL CVGMFileTreeView::PreTranslateMessage(MSG* pMsg)
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

LRESULT CVGMFileTreeView::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{
	BOOL bDestroy = m_ImageList.Destroy();
	bDestroy; //avoid level 4 warning

	bHandled = FALSE;
	return 0;
}


LRESULT CVGMFileTreeView::OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
	// "base::OnCreate()"
	LRESULT lRet = DefWindowProc(uMsg, wParam, lParam);
	bHandled = TRUE;

	// "OnInitialUpdate"
	int cx = 16, cy = 16;
	BOOL bCreate = FALSE;
	bCreate = m_ImageList.Create(cx, cy, ILC_COLOR32 | ILC_MASK, 4, 4);
	m_ImageList.SetBkColor( RGB(255,255,255) );
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


void CVGMFileTreeView::OnLButtonDblClk(UINT nFlags, CPoint point)
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

	

	pMainFrame->ShowVGMFileFrame((VGMFile*)item.GetData());

	//pMainFrame->ShowFileHexView(true);

	//if( htItem == NULL )
	//	return;

	//VGMItem* myVGMItem;
	//VGMItemMap.Lookup(htItem, (void *&)myVGMItem);			//look up the VGMItem from the HTREEITEM key
	//myVGMItem->OnDblClick();
	//bHandled = true;
}

LRESULT CVGMFileTreeView::OnTvnSelchanged(int idCtrl, LPNMHDR pnmh, BOOL& bHandled)
{
	CTreeViewCtrlEx* treeview = reinterpret_cast<CTreeViewCtrlEx*>(pnmh);
	NM_TREEVIEW* pNMTreeView = (NM_TREEVIEW*)pnmh;
	
	if (pNMTreeView->action != TVC_UNKNOWN)
	{
		CTreeItem treeitem = treeview->GetSelectedItem();
		VGMFile* vgmfile = (VGMFile*)treeitem.GetData();
		//fileView.SetCurFile(vgmfile->file);
		//fileView.SelectItem(vgmfile);
		//WriteItemUpdateStatusBarWithItem(vgmfile);
		//itemTreeView.ChangeCurVGMFile(vgmfile);

		winroot.SelectItem(vgmfile);
		//pMainFrame->OnPlay();
	}

	bHandled = false;
	return 0;
}


void CVGMFileTreeView::Init(int cx, int cy)
{
	nIconIndexNormal = -1;
	nIconIndexSelected = -1;
	HICON hIcon = NULL;

	// NOTE: Don't Load using the LR_LOADTRANSPARENT bit, because the icon
	//  already properly deals with transparency (setting it causes problems).
	//  We will load this as LR_SHARED so that we don't have to do a DeleteIcon.
	hIcon = (HICON)::LoadImage(
				_Module.GetResourceInstance(),
				MAKEINTRESOURCE(IDI_VGMFILE_SEQ),
				IMAGE_ICON, cx, cy, LR_SHARED);

	nIconIndexNormal = m_ImageList.AddIcon(hIcon);
	nIconIndexSelected = nIconIndexNormal;


	nIconFolderIndexNormal = -1;
	nIconFolderIndexSelected = -1;
	hIcon = (HICON)::LoadImage(
				_Module.GetResourceInstance(),
				MAKEINTRESOURCE(IDI_FOLDER_CLOSED),
				IMAGE_ICON, cx, cy, LR_SHARED);

	nIconFolderIndexNormal = m_ImageList.AddIcon(hIcon);

	hIcon = (HICON)::LoadImage(
				_Module.GetResourceInstance(),
				MAKEINTRESOURCE(IDI_FOLDER_OPEN),
				IMAGE_ICON, cx, cy, LR_SHARED);

	nIconFolderIndexSelected = m_ImageList.AddIcon(hIcon);

	// Hook up the image list to the tree view
	SetImageList(m_ImageList, TVSIL_NORMAL);
}

BOOL CVGMFileTreeView::SelectFile(VGMFile* vgmfile)
{
	if (vgmfile)		//check that the item passed was NULL
		return CTreeViewCtrlEx::SelectItem(items[vgmfile].m_hTreeItem);
	return NULL;
}



void CVGMFileTreeView::AddFile(VGMFile* newFile)
{
	//LPCTSTR str = A2W(newFile->name.c_str());
	CTreeItem newItem = this->InsertItem(newFile->GetName()->c_str(), nIconIndexNormal, nIconIndexSelected, TVI_ROOT, NULL);
	newItem.SetData((DWORD_PTR)newFile);

	items[newFile] = newItem;
	//lItems.push_back(newItem);
}

void CVGMFileTreeView::RemoveFile(VGMFile* theFile)
{
	//if (items[theFile].GetState(TVIS_SELECTED))
	//	itemTreeView.RemoveAllItems();
	//if (GetSelectedItem() == items[theFile])
		
	items[theFile].Delete();			//remove CTreeItem from view
	items.erase(items.find(theFile));	//remove the CTreeItem from the item map
}


LRESULT CVGMFileTreeView::OnNMRClick(int idCtrl, LPNMHDR pnmh, BOOL& bHandled)
{
	SendMessage(WM_CONTEXTMENU, (WPARAM)m_hWnd, GetMessagePos());
	return 0;
}

LRESULT CVGMFileTreeView::OnContextMenu(HWND hwndCtrl, CPoint ptClick )
{
	//bHandled = TRUE;
	//CTreeViewCtrlEx* treeview = (CTreeViewCtrlEx*)hwndCtrl;

	// if Shift-F10
	if (ptClick.x == -1 && ptClick.y == -1)
		ptClick = (CPoint) GetMessagePos();

	ScreenToClient(&ptClick);

	UINT uFlags;
	//HTREEITEM htItem = GetTreeCtrl().HitTest( ptMousePos, &uFlags );
	CTreeItem treeitem = HitTest( ptClick, &uFlags );

	if( treeitem == NULL )
		return 0;

	VGMFile* pvgmfile = (VGMFile*)treeitem.GetData();
	ClientToScreen(&ptClick);
	ItemContextMenu(hwndCtrl, ptClick, pvgmfile);
	


	// Load from resource
	//mnuContext.LoadMenu(IDR_RAWFILE);
	//CMenuHandle pPopup = mnuContext.GetSubMenu(0);
	//ClientToScreen(&ptClick);
	//pPopup.TrackPopupMenu( TPM_LEFTALIGN, ptClick.x, ptClick.y, hwndCtrl );

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
		//mnuContext.AppendMenu((MF_ENABLED | MF_STRING), ID_VIEW_SOURCE, _T("&View Source"));

		//if(m_pCmdBar != NULL)
		//{
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
			ClientToScreen(&ptClick);
			DWORD nSelection = mnuContext.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_TOPALIGN | TPM_VERTICAL | TPM_RETURNCMD,
				ptClick.x, ptClick.y, hwndCtrl);
			if(nSelection != 0)
			{
				this->PostMessage(WM_COMMAND, MAKEWPARAM(nSelection, 0));
			}
		//}
	//	else
	//	{
	//		mnuContext.TrackPopupMenuEx(TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_TOPALIGN | TPM_VERTICAL,
	//			ptPopup.x, ptPopup.y, m_hWnd, NULL);
	//	}
	}*/

	return 0;
}



BOOL CVGMFileTreeView::SelectItem(VGMItem* item)
{
	if (item)		//test for NULL pointer
	{
		VGMFile* itemsVGMFile = item->vgmfile;//item->GetRawFile()->GetVGMFileFromOffset(item->dwOffset);
		if (itemsVGMFile)
			return CTreeViewCtrlEx::SelectItem(items[itemsVGMFile].m_hTreeItem);
	}
	return NULL;
}
