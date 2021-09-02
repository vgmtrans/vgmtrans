#include "pch.h"
#include "resource.h"
#include "HexView.h"
#include "HexViewFrame.h"
#include "mainfrm.h"
#include "VGMFileTreeView.h"
#include "ItemTreeView.h"
#include "WinVGMRoot.h"

using namespace std;

//CItemTreeView itemTreeView;


CItemTreeView::CItemTreeView(CHexViewFrame* parentFrame)
: parFrame(parentFrame)
{
}

BOOL CItemTreeView::PreTranslateMessage(MSG* pMsg)
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

LRESULT CItemTreeView::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{
	BOOL bDestroy = m_ImageList.Destroy();
	bDestroy; //avoid level 4 warning

	//for (unordered_map<VGMItem*, CTreeItem>::iterator iter = items.begin(); iter != items.end(); iter++)
	//	delete iter->first;
	items.clear();

	bHandled = FALSE;
	return 0;
}


LRESULT CItemTreeView::OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
	// "base::OnCreate()"
	LRESULT lRet = DefWindowProc(uMsg, wParam, lParam);
	bHandled = TRUE;
	bPopulated = false;
	bPopulating = false;

	curFile = NULL;
	parItemCache = NULL;
	parentTreeItemCache = TVI_ROOT;

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


LRESULT CItemTreeView::OnTvnSelchanged(int idCtrl, LPNMHDR pnmh, BOOL& bHandled)
{
	CTreeViewCtrlEx* treeview = reinterpret_cast<CTreeViewCtrlEx*>(pnmh);
	NM_TREEVIEW* pNMTreeView = (NM_TREEVIEW*)pnmh;
	//pNMTreeView->actio
	
	if (pNMTreeView->action != TVC_UNKNOWN)
	{
		CTreeItem treeitem = treeview->GetSelectedItem();
		VGMItem* item = (VGMItem*)treeitem.GetData();
		//winroot.SelectItem(item);
		parFrame->SelectItem(item);

	}

	bHandled = false;
	return 0;
}


LRESULT CItemTreeView::OnNMRClick(int idCtrl, LPNMHDR pnmh, BOOL& bHandled)
{
	SendMessage(WM_CONTEXTMENU, (WPARAM)m_hWnd, GetMessagePos());
	return 0;
}

LRESULT CItemTreeView::OnContextMenu(HWND hwndCtrl, CPoint ptClick )
{
	if (ptClick.x == -1 && ptClick.y == -1)
		ptClick = (CPoint) GetMessagePos();

	ScreenToClient(&ptClick);

	UINT uFlags;
	CTreeItem treeitem = HitTest( ptClick, &uFlags );

	if( treeitem == NULL )
		return 0;

	VGMFile* pvgmfile = (VGMFile*)treeitem.GetData();
	ClientToScreen(&ptClick);
	ItemContextMenu(hwndCtrl, ptClick, pvgmfile);

	return 0;
}

void CItemTreeView::Init(int cx, int cy)
{
	int iconList[VGMItem::ICON_MAX];
	//{ IDI_UNKNOWN, IDI_SEQ, IDI_NOTE IDI_UNKNOWN, IDI_UNKNOWN };
	for (int i=0; i < VGMItem::ICON_MAX; i++)
		iconList[i] = i + IDI_VGMFILE_SEQ;

	// NOTE: Don't Load using the LR_LOADTRANSPARENT bit, because the icon
	//  already properly deals with transparency (setting it causes problems).
	//  We will load this as LR_SHARED so that we don't have to do a DeleteIcon.

	for (int i=0; i<sizeof(iconList)/sizeof(iconList[0]); i++)
	{
		HICON hIcon = NULL;
		hIcon = (HICON)::LoadImage(
				_Module.GetResourceInstance(),
				MAKEINTRESOURCE(iconList[i]),
				IMAGE_ICON, cx, cy, LR_SHARED);

		m_ImageList.AddIcon(hIcon);
	}
	
/*
	hIcon = (HICON)::LoadImage(
				_Module.GetResourceInstance(),
				MAKEINTRESOURCE(IDI_SEQ),
				IMAGE_ICON, cx, cy, LR_SHARED);

	m_ImageList.AddIcon(hIcon);

	hIcon = (HICON)::LoadImage(
				_Module.GetResourceInstance(),
				MAKEINTRESOURCE(IDI_FOLDER_OPEN),
				IMAGE_ICON, cx, cy, LR_SHARED);

	m_ImageList.AddIcon(hIcon);

	hIcon = (HICON)::LoadImage(
				_Module.GetResourceInstance(),
				MAKEINTRESOURCE(IDI_NOTE),
				IMAGE_ICON, cx, cy, LR_SHARED);
*/
	// Hook up the image list to the tree view
	SetImageList(m_ImageList, TVSIL_NORMAL);
}

/*void CItemTreeView::ChangeCurVGMFile(VGMFile* file)
{
	if (file == curFile)
		return;

	curFile = file;
	ShowWindow(false);
	DeleteAllItems();
	items.clear();
	file->AddAllVGMFileItems();
	ShowWindow(true);
}*/

//add a item to the Item treeview.  Pass NULL for parent to put item in root of tree.


void CItemTreeView::RemoveItem(VGMItem* theItem)
{
	items[theItem].Delete();			//remove CTreeItem from view
	items.erase(items.find(theItem));	//remove the CTreeItem from the item map
}

void CItemTreeView::RemoveAllItems(void)
{
	ShowWindow(false);
	DeleteAllItems();
	ShowWindow(true);
}


BOOL CItemTreeView::SelectItem(VGMItem* item)
{
	if (item && (item->GetType() != VGMItem::ITEMTYPE_VGMFILE))	//test for NULL pointer and that it could be an item
	{
		VGMFile* itemsVGMFile = item->vgmfile;//item->GetRawFile()->GetVGMFileFromOffset(item->dwOffset);
		if (itemsVGMFile)
		{
			if (bPopulated)
				return CTreeViewCtrlEx::SelectItem(items[item].m_hTreeItem);
		}
	}
	/*else if (!bPopulated && !bPopulating)
	{
		bPopulating = true;
		pair<VGMFile*, CItemTreeView*>* info = new pair<VGMFile*, CItemTreeView*>((VGMFile*)item, this);
		populateItemViewThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)&CItemTreeView::PopulateItemView, info, 0, &populateItemViewThreadID);
	}*/
	return NULL;
}




//void CItemTreeView::PopulateWithItem(VGMItem* item, VGMItem* parent)
//{
//	if (item->IsContainerItem())
//	{
//		VGMContainerItem* citem = (VGMContainerItem*)item;
//		for (UINT i=0; i<citem->containers.size(); i++)
//		{
//			for (UINT j=0; j<citem->containers[i]->size(); j++)
//				PopulateWithItem(*citem->containers[i])[j], citem);
//		}
//	}
//	else
//	{
//		int iconIndex = item->GetIcon();
//		if (parent != parItemCache)
//		{
//			parItemCache = parent;
//			parentTreeItemCache = items[parent].m_hTreeItem;
//		}
//		CTreeItem treeItem = InsertItem(item->name, iconIndex, iconIndex, parentTreeItemCache, NULL);
//		treeItem.SetData((DWORD_PTR)item);
//
//		items[item] = treeItem;
//	}
//}

void CItemTreeView::WaitForPopulateEnd(void)
{
//	WaitForSingleObject(populateItemViewThread, INFINITE);
}

DWORD CItemTreeView::PopulateItemView(PVOID pParam)
{
	pair<VGMFile*, CItemTreeView*>* info = reinterpret_cast<pair<VGMFile*, CItemTreeView*>*>(pParam);
	VGMFile* vgmfile = info->first;
	CItemTreeView* theItemView = info->second;
	delete info;
	//vgmfile->AddAllVGMFileItems();
	
	theItemView->parItemCache = NULL;
	vgmfile->AddToUI(NULL, (VOID*)theItemView);
	//theItemView->PopulateWithItem(vgmfile, NULL);
	theItemView->bPopulated = true;
	theItemView->bPopulating = false;
	return true;
}