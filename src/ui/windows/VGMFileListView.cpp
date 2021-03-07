// WTLCabViewView.cpp : implementation of the CWTLCabViewView class
/////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "resource.h"
#include "VGMFileListView.h"
#include "DragDropSource.h"
#include "WinVGMRoot.h"
#include "mainfrm.h"

using namespace std;

CVGMFileListView theVGMFileListView;

struct __declspec(uuid("DE5BF786-477A-11d2-839D-00C04FD918D0")) IDragSourceHelper;

/////////////////////////////////////////////////////////////////////////////
// Construction

CVGMFileListView::CVGMFileListView() : m_nSortedCol(-1), m_bSortAscending(true)
{
}


/////////////////////////////////////////////////////////////////////////////
// Message handlers

BOOL CVGMFileListView::PreTranslateMessage ( MSG* pMsg )
{
    return FALSE;
}


LRESULT CVGMFileListView::OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
	// "base::OnCreate()"
	LRESULT lRet = DefWindowProc(uMsg, wParam, lParam);


	// "OnInitialUpdate"
	//int cx = 16, cy = 16;
	//BOOL bCreate = FALSE;
	//bCreate = m_ImageList.Create(cx, cy, ILC_COLOR32 | ILC_MASK, 4, 4);
	//m_ImageList.SetBkColor( RGB(255,255,255) );
//	if(bCreate)
//	{
//		Init(cx, cy);
//	}

	/*CTreeItem tiRoot = this->InsertItem(_T("Root"), nIconFolderIndexNormal, nIconFolderIndexSelected, TVI_ROOT, NULL);

	CTreeItem tiFolder1 = this->InsertItem(_T("Folder"), nIconFolderIndexNormal, nIconFolderIndexSelected, tiRoot, NULL);
	this->InsertItem(_T("Item"), nIconIndexNormal, nIconIndexSelected, tiFolder1, NULL);

	CTreeItem tiFolder2 = this->InsertItem(_T("Folder"), nIconFolderIndexNormal, nIconFolderIndexSelected, tiRoot, NULL);
	this->InsertItem(_T("Item"), nIconIndexNormal, nIconIndexSelected, tiFolder2, NULL);
	this->InsertItem(_T("Item"), nIconIndexNormal, nIconIndexSelected, tiFolder2, NULL);

	tiRoot.Expand();
	tiFolder1.Expand();
	tiFolder2.Expand();*/

	Init();

	bHandled = TRUE;
	//return lRet;
	return NULL;
}

void CVGMFileListView::OnDestroy()
{
    // Destroy the state image list. The other image lists shouldn't be destroyed
    // since they are copies of the system image list. Destroying those on Win 9x
    // is a Bad Thing to do.
    if ( m_imlState )
        m_imlState.Destroy();

    SetMsgHandled(false);
}

void CVGMFileListView::OnSize(UINT nType, CSize size)
{
	int cx = size.cx;
//	SetColumnWidth(1, LVSCW_AUTOSIZE);
	SetColumnWidth(0, cx - GetColumnWidth(1));
	ShowScrollBar(SB_HORZ, FALSE);				//this is a hacky solution.  Not sure why horiz scrollbar is there in the first place
}

void CVGMFileListView::OnKeyDown(TCHAR nChar, UINT repeats, UINT code)
{
	switch (nChar)
	{
	case VK_DELETE:
		OnCloseFile(0, 0, 0);
		//SendMessage(IDC_CLOSEFILE, 0, 0);
		break;
	}
}

LRESULT CVGMFileListView::OnContextMenu(HWND hwndCtrl, CPoint ptClick )
{
	//bHandled = TRUE;
	//CTreeViewCtrlEx* treeview = (CTreeViewCtrlEx*)hwndCtrl;

	// if Shift-F10
	if (ptClick.x == -1 && ptClick.y == -1)
		ptClick = (CPoint) GetMessagePos();

	ScreenToClient(&ptClick);

	UINT uFlags;
	//HTREEITEM htItem = GetTreeCtrl().HitTest( ptMousePos, &uFlags );
	int iItem = HitTest( ptClick, &uFlags );

	if( iItem == -1 )
		return 0;

	vector<VGMFile*> vgmfiles;
	UINT selectedCount = GetSelectedCount();
	if (selectedCount > 1)
	{
		iItem = -1;
		for (UINT i = 0; i < selectedCount; i++)
		{
			iItem = GetNextItem(iItem, LVNI_SELECTED);
			ATLASSERT(iItem != -1);
			vgmfiles.push_back((VGMFile*)GetItemData(iItem));
		}
	}
	else
	{
		vgmfiles.push_back((VGMFile*)GetItemData(iItem));
	}

	if (vgmfiles.size() > 1)
	{
		CMenu mnuContext;
		mnuContext.LoadMenu(IDR_RAWFILE);
		CMenuHandle pPopup = mnuContext.GetSubMenu(0);
		ClientToScreen(&ptClick);
		pPopup.TrackPopupMenu( TPM_LEFTALIGN, ptClick.x, ptClick.y, hwndCtrl );
	}
	else
	{
		VGMFile* pvgmfile = vgmfiles[0];
		ClientToScreen(&ptClick);
		ItemContextMenu(hwndCtrl, ptClick, pvgmfile);
	}

	return NULL;
}


/////////////////////////////////////////////////////////////////////////////
// Notify handlers

LRESULT CVGMFileListView::OnColumnClick ( NMHDR* phdr )
{
CWaitCursor w;
int nCol = ((NMLISTVIEW*) phdr)->iSubItem;

    // If the user clicked the column that is already sorted, reverse the sort
    // direction. Otherwise, go back to ascending order.
    if ( nCol == m_nSortedCol )
        m_bSortAscending = !m_bSortAscending;
    else
        m_bSortAscending = true;

    if ( g_bXPOrLater )
        {
        HDITEM hdi = { HDI_FORMAT };
        CHeaderCtrl wndHdr = GetHeader();

        // Remove the sort arrow indicator from the previously-sorted column.
        if ( -1 != m_nSortedCol )
            {
            wndHdr.GetItem ( m_nSortedCol, &hdi );
            hdi.fmt &= ~(HDF_SORTDOWN | HDF_SORTUP);
            wndHdr.SetItem ( m_nSortedCol, &hdi );
            }

        // Add the sort arrow to the new sorted column.
        hdi.mask = HDI_FORMAT;
        wndHdr.GetItem ( nCol, &hdi );
        hdi.fmt |= m_bSortAscending ? HDF_SORTUP : HDF_SORTDOWN;
        wndHdr.SetItem ( nCol, &hdi );
        }

    // Store the column being sorted, and do the sort
    m_nSortedCol = nCol;

    SortItems ( SortCallback, (LPARAM)(DWORD_PTR) this );

    // Have the list ctrl indicate the sorted column by changing its color.
    if ( g_bXPOrLater )
        SetSelectedColumn ( nCol );

    return true;   // retval ignored
}
/*
LRESULT CWTLCabViewView::OnMarqueeBegin ( NMHDR* phdr )
{
    // Don't allow the marquee if the list is empty.
    return (GetItemCount() == 0);
}

LRESULT CWTLCabViewView::OnKeyDown ( NMHDR* phdr )
{
NMLVKEYDOWN* pnmlv = (NMLVKEYDOWN*) phdr;

    // Select all items if the user presses ^A
    if ( 'A' == pnmlv->wVKey && (GetKeyState(VK_CONTROL) & 0x8000) )
        SetItemState ( -1, LVIS_SELECTED, LVIS_SELECTED );

    return 0;   // retval ignored
}*/

/*LRESULT CVGMFileListView::OnDeleteItem ( NMHDR* phdr )
{
	NMLISTVIEW* pnmlv = (NMLISTVIEW*) phdr;
    if (pnmlv->lParam != 0)
		this->SendMessageW(WM_CLOSE);
        //delete (CCompressedFileInfo*) pnmlv->lParam;

    return 0;   // retval ignored
}*/


/////////////////////////////////////////////////////////////////////////////
// Operations

void CVGMFileListView::Init()
{
	selectedVGMFile = NULL;

    InitColumns();
    InitImageLists();


	//GetHeader().ModifyStyleEx(HDS_BUTTONS, 0);

    SetExtendedListViewStyle ( LVS_EX_HEADERDRAGDROP, LVS_EX_HEADERDRAGDROP );

    // On XP, set some additional properties of the list ctrl.
	if ( g_bXPOrLater )
    {
        // Turning on LVS_EX_DOUBLEBUFFER also enables the transparent
        // selection marquee.
        SetExtendedListViewStyle( LVS_EX_DOUBLEBUFFER, LVS_EX_DOUBLEBUFFER );

        // Each tile will have 2 additional lines (3 lines total).
        LVTILEVIEWINFO lvtvi = { sizeof(LVTILEVIEWINFO), LVTVIM_COLUMNS };

        lvtvi.cLines = 2;
        lvtvi.dwFlags = LVTVIF_AUTOSIZE;
        SetTileViewInfo ( &lvtvi );
	}
}

void CVGMFileListView::Clear()
{
    DeleteAllItems();

    if ( -1 != m_nSortedCol )
    {
    if ( g_bXPOrLater )
        {
        // Remove the sort arrow indicator from the sorted column.
        HDITEM hdi = { HDI_FORMAT };
        CHeaderCtrl wndHdr = GetHeader();

        wndHdr.GetItem ( m_nSortedCol, &hdi );
        hdi.fmt &= ~(HDF_SORTDOWN | HDF_SORTUP);
        wndHdr.SetItem ( m_nSortedCol, &hdi );

        // Remove the sorted column color from the list.
        SetSelectedColumn(-1);
        }

    m_nSortedCol = -1;
    m_bSortAscending = true;
    }
}

void CVGMFileListView::SetViewMode ( int nMode )
{
    ATLASSERT(nMode >= LV_VIEW_ICON && nMode <= LV_VIEW_TILE);

    if ( g_bXPOrLater )
        {
        if ( LV_VIEW_TILE == nMode )
            SetImageList ( m_imlTiles, LVSIL_NORMAL );
        else
            SetImageList ( m_imlLarge, LVSIL_NORMAL );

        SetView ( nMode );
        }
    else
        {
        DWORD dwViewStyle;

        ATLASSERT(LV_VIEW_TILE != nMode);

        switch ( nMode )
            {
            case LV_VIEW_ICON:      dwViewStyle = LVS_ICON; break;
            case LV_VIEW_SMALLICON: dwViewStyle = LVS_SMALLICON; break;
            case LV_VIEW_LIST:      dwViewStyle = LVS_LIST; break;
            case LV_VIEW_DETAILS:   dwViewStyle = LVS_REPORT; break;
            DEFAULT_UNREACHABLE;
            }

        ModifyStyle ( LVS_TYPEMASK, dwViewStyle );
        }
}
/*
void CWTLCabViewView::AddFile (
    LPCTSTR szFilename, long cbyUncompressedSize, USHORT uDate,
    USHORT uTime, USHORT uAttribs )
{
int nIdx;
TCHAR szSize[64], szDate[64], szTime[64];
CString sDateTime, sAttrs;
FILETIME ft = {0}, ftLocal = {0};
SYSTEMTIME st = {0};

    // Get the file size with the size in KB or MB or whatever suffix.
    StrFormatByteSize ( cbyUncompressedSize, szSize, countof(szSize) );

    // Format the modified date/time using the user's locale settings.
    DosDateTimeToFileTime ( uDate, uTime, &ft );
    FileTimeToLocalFileTime ( &ft, &ftLocal );
    FileTimeToSystemTime ( &ftLocal, &st );

    GetDateFormat ( LOCALE_USER_DEFAULT, DATE_SHORTDATE, &st, NULL, szDate, countof(szDate) );
    GetTimeFormat ( LOCALE_USER_DEFAULT, TIME_NOSECONDS, &st, NULL, szTime, countof(szTime) );

    sDateTime.Format ( _T("%s %s"), szDate, szTime );

    // Make a string that shows the attributes.
    if ( uAttribs & _A_RDONLY )
        sAttrs += 'R';
    if ( uAttribs & _A_HIDDEN )
        sAttrs += 'H';
    if ( uAttribs & _A_SYSTEM )
        sAttrs += 'S';
    if ( uAttribs & _A_ARCH )
        sAttrs += 'A';

    // Get the file's type description and the index of its icon in the system imagelist.
SHFILEINFO info = {0};

    SHGetFileInfo ( szFilename, FILE_ATTRIBUTE_NORMAL, &info, sizeof(info),
                    SHGFI_USEFILEATTRIBUTES|SHGFI_TYPENAME|SHGFI_SYSICONINDEX );

    // Add a new list item for this file.
    nIdx = InsertItem ( GetItemCount(), szFilename, info.iIcon );
    SetItemText ( nIdx, 1, info.szTypeName );
    SetItemText ( nIdx, 2, szSize );
    SetItemText ( nIdx, 3, sDateTime );
    SetItemText ( nIdx, 4, sAttrs );

CCompressedFileInfo* pInfo = new CCompressedFileInfo;

    pInfo->sFilename = szFilename;
    pInfo->sFileType = info.szTypeName;
    pInfo->dwFileSize = cbyUncompressedSize;
    pInfo->ftDateTime = ft;
    pInfo->uAttribs = uAttribs;

    SetItemData ( nIdx, (DWORD_PTR) pInfo );

    // On XP+, set up the additional tile view text for the item.
    if ( g_bXPOrLater )
        {
        UINT aCols[] = { 1, 2 };
        LVTILEINFO lvti = { sizeof(LVTILEINFO), nIdx, countof(aCols), aCols };

        SetTileInfo ( &lvti );
        }
}
*/

/*void CVGMFileListView::OnLButtonDblClk(UINT nFlags, CPoint point)
{
		// TODO: Add your message handler code here and/or call default
		// if Shift-F10
	if (point.x == -1 && point.y == -1)
		point = (CPoint) GetMessagePos();

	//ScreenToClient(&ptMousePos);

	UINT uFlags;


	int item = HitTest( point, &uFlags );

	if (item == -1)
		return;

	

	pMainFrame->ShowVGMFileFrame((VGMFile*)GetItemData(item));

	//pMainFrame->ShowFileHexView(true);

	//if( htItem == NULL )
	//	return;

	//VGMItem* myVGMItem;
	//VGMItemMap.Lookup(htItem, (void *&)myVGMItem);			//look up the VGMItem from the HTREEITEM key
	//myVGMItem->OnDblClick();
	//bHandled = true;
}*/

LRESULT CVGMFileListView::OnLvnItemActivate(int idCtrl, LPNMHDR pnmh, BOOL& bHandled)
{
	NMLISTVIEW* listview = (NMLISTVIEW*)pnmh;

	if (listview->iItem == -1)
		return NULL;

	pMainFrame->ShowVGMFileFrame((VGMFile*)GetItemData(listview->iItem));
	bHandled = true;
	return NULL;
}

LRESULT CVGMFileListView::OnLvnItemChanged(int idCtrl, LPNMHDR pnmh, BOOL& bHandled)
{
	//CListViewCtrl* listview = reinterpret_cast<CListViewCtrl*>(pnmh);
	NM_LISTVIEW* pNMListView = (NM_LISTVIEW*)pnmh;
	
	if (pNMListView->iItem != -1 && pNMListView->uNewState & LVIS_FOCUSED)
	{
		
		VGMFile* vgmfile = (VGMFile*)pNMListView->lParam;
		winroot.SelectItem(vgmfile);
	}
	bHandled = false;
	return 0;
}

BOOL CVGMFileListView::SelectItem(VGMItem* item)
{
	if (item)		//test for NULL pointer
	{
		VGMFile* itemsVGMFile = item->vgmfile;//item->GetRawFile()->GetVGMFileFromOffset(item->dwOffset);
		if(selectedVGMFile == itemsVGMFile)
			return NULL;
		LVFINDINFO findinfo;
		findinfo.flags = LVFI_PARAM;
		findinfo.lParam = (DWORD_PTR)itemsVGMFile;
		if (itemsVGMFile)
		{
			selectedVGMFile = itemsVGMFile;
			//return CListViewCtrl::SelectItem(FindItem(&findinfo, -1));//items[itemsVGMFile]);
		}
	}
	return NULL;
}


BOOL CVGMFileListView::SelectFile(VGMFile* vgmfile)
{
	if (vgmfile)		//check that the item passed was not NULL
	{
		LVFINDINFO findinfo;
		findinfo.flags = LVFI_PARAM;
		findinfo.lParam = (DWORD_PTR)vgmfile;
		return CListViewCtrl::SelectItem(FindItem(&findinfo, -1));//items[vgmfile]);
	}
	return false;
}



void CVGMFileListView::AddFile(VGMFile* newFile)
{
	//LPCTSTR str = A2W(newFile->name.c_str());

	//int iItem = InsertItem(GetItemCount(), (LPWSTR)newFile->GetName()->c_str());
	//SetItemText (iItem, 1, newFile->GetName()->c_str() );
	//int newItem = this->InsertItem(newFile->GetName()->c_str(), nIconIndexNormal, nIconIndexSelected, TVI_ROOT, NULL);
	//SetItemData(iItem, (DWORD_PTR)newFile);
	//newItem.SetData((DWORD_PTR)newFile);

	LVITEMW newItem;// = new LVITEMW();
	newItem.lParam = (DWORD_PTR)newFile;
	newItem.iItem = GetItemCount();
	newItem.iSubItem = 0;
	switch (newFile->GetFileType())
	{
	case FILETYPE_SAMPCOLL:
		newItem.iImage = nSampCollIcon;
		break;
	case FILETYPE_INSTRSET:
		newItem.iImage = nInstrSetIcon;
		break;
	case FILETYPE_SEQ:
		newItem.iImage = nSeqIcon;
		break;
	case FILETYPE_MISC:
		newItem.iImage = nMiscIcon;
		break;
	}
	//newItem.iGroupId = 0;
	newItem.pszText = (LPWSTR)newFile->GetName()->c_str();
	newItem.mask = LVIF_PARAM | LVIF_TEXT | LVIF_IMAGE;// | LVIF_GROUPID;
	InsertItem(&newItem);

	switch (newFile->GetFileType())
	{
	case FILETYPE_INSTRSET :
		SetItemText(newItem.iItem, 1, L"Instrument Set");
		break;
	case FILETYPE_SAMPCOLL :
		SetItemText(newItem.iItem, 1, L"Sample Collection");
		break;
	case FILETYPE_SEQ :
		SetItemText(newItem.iItem, 1, L"Sequence");
		break;
	}

	//newItem->iImage = NULL;//nIconIndexNormal;
	
	
	/*LVGROUP group;
	group.mask = LVGF_ALIGN | LVGF_HEADER | LVGF_GROUPID;
	group.pszHeader = _T("Header");
	group.iGroupId  = 0;
	group.uAlign = LVGA_HEADER_LEFT;
	InsertGroup(4, &group);*/

	//items[newFile] = iItem;
	//lItems.push_back(newItem);
}

void CVGMFileListView::RemoveFile(VGMFile* theFile)
{
	//if (items[theFile].GetState(TVIS_SELECTED))
	//	itemTreeView.RemoveAllItems();
	//if (GetSelectedItem() == items[theFile])
		
	LVFINDINFO findinfo;
	findinfo.flags = LVFI_PARAM;
	findinfo.lParam = (DWORD_PTR)theFile;
	DeleteItem(FindItem(&findinfo, -1));//items[theFile]);
	//items[thebb File].Delete();			//remove CTreeItem from view
	//items.erase(items.find(theFile));	//remove the CTreeItem from the item map
}


LRESULT CVGMFileListView::OnListBeginDrag ( NMHDR* phdr ){
    return 0;
}

/*
void CWTLCabViewView::AddPartialFile (
    LPCTSTR szFilename, LPCTSTR szStartingCabName, LPCTSTR szStartingDiskName )
{
int nIdx;
SHFILEINFO info = {0};
CString sSplitFileText;

    sSplitFileText.Format ( IDS_FROM_PREV_CAB, szStartingCabName );

    // Get the file's type description and the index of its icon in the system imagelist.
    SHGetFileInfo ( szFilename, FILE_ATTRIBUTE_NORMAL, &info, sizeof(info),
                    SHGFI_USEFILEATTRIBUTES|SHGFI_TYPENAME|SHGFI_SYSICONINDEX );

    // Add a new list item for this file.
    nIdx = InsertItem ( GetItemCount(), szFilename, info.iIcon );
    SetItemText ( nIdx, 1, info.szTypeName );
    SetItemText ( nIdx, 5, sSplitFileText );

CCompressedFileInfo* pInfo = new CCompressedFileInfo;

    pInfo->sFilename = szFilename;
    pInfo->sFileType = info.szTypeName;
    pInfo->location = CCompressedFileInfo::from_prev_cab;
    pInfo->sOtherCabName = szStartingCabName;
    pInfo->bExtractable = false;

    SetItemData ( nIdx, (DWORD_PTR) pInfo );

    // Set the stage image to the icon that shows the file begins in
    // an earlier CAB.
    SetItemState ( nIdx, INDEXTOSTATEIMAGEMASK(1), LVIS_STATEIMAGEMASK );

    // On XP+, set up the additional tile view text for the item.
    if ( g_bXPOrLater )
        {
        UINT aCols[] = { 5 };
        LVTILEINFO lvti = { sizeof(LVTILEINFO), nIdx, countof(aCols), aCols };

        SetTileInfo ( &lvti );
        }
}

void CWTLCabViewView::UpdateContinuedFile ( const CDraggedFileInfo& info )
{
CString sSplitFileText;

    // Update the Split File column to show what CAB this file is continued in.
    sSplitFileText.Format ( IDS_TO_NEXT_CAB, (LPCTSTR) info.sCabName );

    SetItemText ( info.nListIdx, 5, sSplitFileText );

    // Update the item's data to keep track of the fact that the file is
    // continued in another CAB.
CCompressedFileInfo* pInfo;

    pInfo = (CCompressedFileInfo*) GetItemData ( info.nListIdx );
    ATLASSERT(NULL != pInfo);

    pInfo->location = CCompressedFileInfo::to_next_cab;
    pInfo->sOtherCabName = info.sCabName;

    // Set the stage image to the icon that shows the file continues in another CAB.
    SetItemState ( info.nListIdx, INDEXTOSTATEIMAGEMASK(2), LVIS_STATEIMAGEMASK );

    if ( info.bCabMissing )
        {
        // Set a flag indicating that the file can't be extracted, and set the
        // LVIS_CUT style so the icon is grayed out.
        pInfo->bExtractable = false;
        SetItemState ( info.nListIdx, LVIS_CUT, LVIS_CUT );
        }
}*/


bool CVGMFileListView::GetDraggedFileInfo ( std::vector<CDraggedFileInfo>& vec )
{
int nIdx = -1;

    while ( (nIdx = GetNextItem ( nIdx, LVNI_SELECTED )) != -1 )
        {
        VGMFile* vgmfile;

        //pInfo = (CCompressedFileInfo*) GetItemData ( nIdx );
		vgmfile = (VGMFile*) GetItemData ( nIdx );
        ATLASSERT(vgmfile != NULL);

        //if ( pInfo->bExtractable )
            vec.push_back ( CDraggedFileInfo ( vgmfile->GetName()->c_str(), nIdx ) );
        //else
        //    ATLTRACE("Skipping partial/unavailable file <%s>\n", (LPCTSTR) vgmfile->sFilename);
        }

    return !vec.empty();
}


/////////////////////////////////////////////////////////////////////////////
// Other methods

void CVGMFileListView::InitColumns()
{
    InsertColumn ( 0, _T("File Name"), LVCFMT_LEFT, 130, 0 );
	InsertColumn ( 1, _T("Type"), LVCFMT_LEFT, 90, 0 );

/*	int jo = EnableGroupView(true); 
	jo = this->IsGroupViewEnabled();
	LVGROUP group;

	group.cbSize = sizeof(LVGROUP);
	group.mask = LVGF_HEADER | LVGF_GROUPID | LVGF_STATE;
	group.iGroupId = 0;
	group.stateMask = (UINT)-1; 
	group.state = LVGS_NORMAL;
	group.pszHeader = L"Header";
	group.cchHeader = (int)wcslen(L"Header");//group.pszHeader); 
	jo = InsertGroup(-1, &group); */
}

void CVGMFileListView::InitImageLists()
{
	nSampCollIcon = -1;
	nInstrSetIcon = -1;
	nSeqIcon = -1;
	//SHFILEINFO info = {0};

    // Get the large/small system image lists
 /*   m_imlSmall = (HIMAGELIST) SHGetFileInfo ( _T("alyson.com"), FILE_ATTRIBUTE_NORMAL,
                                &info, sizeof(info),
                                SHGFI_USEFILEATTRIBUTES|SHGFI_ICON|SHGFI_SMALLICON|SHGFI_SYSICONINDEX );

    m_imlLarge = (HIMAGELIST) SHGetFileInfo ( _T("willow.com"), FILE_ATTRIBUTE_NORMAL,
                                &info, sizeof(info),
                                SHGFI_USEFILEATTRIBUTES|SHGFI_ICON|SHGFI_SYSICONINDEX );

    SetImageList ( m_imlLarge, LVSIL_NORMAL );
    SetImageList ( m_imlSmall, LVSIL_SMALL );*/

    // Create a state image list
//HICON hiSampColl = AtlLoadIconImage ( IDI_VGMFILE_SAMPCOLL, LR_DEFAULTCOLOR, 16, 16 );
	HICON hiSampColl = (HICON)::LoadImage(
				_Module.GetResourceInstance(),
				MAKEINTRESOURCE(IDI_VGMFILE_SAMPCOLL),
				IMAGE_ICON, 16, 16, LR_SHARED);
	HICON hiInstrSet = (HICON)::LoadImage(
				_Module.GetResourceInstance(),
				MAKEINTRESOURCE(IDI_VGMFILE_INSTRSET),
				IMAGE_ICON, 16, 16, LR_SHARED);
	HICON hiSeq = (HICON)::LoadImage(
				_Module.GetResourceInstance(),
				MAKEINTRESOURCE(IDI_VGMFILE_SEQ),
				IMAGE_ICON, 16, 16, LR_SHARED);
	HICON hiMisc = (HICON)::LoadImage(
				_Module.GetResourceInstance(),
				MAKEINTRESOURCE(IDI_BINARY),
				IMAGE_ICON, 16, 16, LR_SHARED);
//HICON hiNext = AtlLoadIconImage ( IDI_NEXT_CAB, LR_DEFAULTCOLOR, 16, 16 );

    BOOL bCreate = m_imlState.Create ( 16, 16, ILC_COLOR32 | ILC_MASK, 3, 1 );
	m_imlState.SetBkColor(this->GetBkColor());//RGB(255,255,255));

    nSampCollIcon = m_imlState.AddIcon ( hiSampColl );
	nInstrSetIcon = m_imlState.AddIcon ( hiInstrSet );
	nSeqIcon = m_imlState.AddIcon ( hiSeq );
	nMiscIcon = m_imlState.AddIcon ( hiMisc );
    //m_imlState.AddIcon ( hiNext );

    DestroyIcon ( hiSampColl );
    DestroyIcon ( hiInstrSet );
	DestroyIcon ( hiSeq );
	DestroyIcon ( hiMisc );

    //SetImageList ( m_imlState, LVSIL_STATE );
	SetImageList(m_imlState, LVSIL_SMALL);

    // On pre-XP, we're done.
    if ( !g_bXPOrLater )
        return;

    // Get the 48x48 system image list. 
/*HRESULT hr, (WINAPI* pfnGetImageList)(int, REFIID, void**) = NULL;
HMODULE hmod = GetModuleHandle ( _T("shell32") );

    if ( NULL != hmod )
        (FARPROC&) pfnGetImageList = GetProcAddress ( hmod, "SHGetImageList" );

    if ( NULL == pfnGetImageList )
        (FARPROC&) pfnGetImageList = GetProcAddress ( hmod, MAKEINTRESOURCEA(727) );  // see Q316931

    if ( NULL == pfnGetImageList )
        {
        // Couldn't get the address of SHGetImageList(), so just use the 32x32 imglist.
        m_imlTiles = m_imlLarge;
        return;
        }

    hr = pfnGetImageList ( SHIL_EXTRALARGE, IID_IImageList, (void**) &m_TileIml );

    if ( FAILED(hr) )
        {
        // Couldn't get the 48x48 image list, so fall back to using the 32x32 one.
        m_imlTiles = m_imlLarge;
        return;
        }

    // HIMAGELIST and IImageList* are interchangeable, so this cast is kosher.
    m_imlTiles = (HIMAGELIST)(IImageList*) m_TileIml;*/
}

int CALLBACK CVGMFileListView::SortCallback (
    LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort )
{
    ATLASSERT(NULL != lParam1);
    ATLASSERT(NULL != lParam2);
    ATLASSERT(NULL != lParamSort);

	VGMFile& info1 = *(VGMFile*) lParam1;
	VGMFile& info2 = *(VGMFile*) lParam2;
	CVGMFileListView* pThis = (CVGMFileListView*) lParamSort;

    return pThis->SortCallback ( info1, info2 );
}


int CVGMFileListView::SortCallback (
    const VGMFile& info1, const VGMFile& info2 ) const
{
	int nRet = 0;
	LPCTSTR sz1, sz2;

    ATLASSERT(-1 != m_nSortedCol);
    //ATLASSERT(!info1.sFilename.IsEmpty());
    //ATLASSERT(!info2.sFilename.IsEmpty());

    // Default to comparing the file names, unless something in the switch
    // changes these pointers.
	sz1 = info1.GetName()->c_str();
    sz2 = info2.GetName()->c_str();

	//ItemType type1;
	//ItemType type2;


    switch ( m_nSortedCol )
        {
        case 0:     // file name
        break;

        case 1:     // type description
            //ATLASSERT(!info1.sFileType.IsEmpty());
            //ATLASSERT(!info2.sFileType.IsEmpty());

            if (info1.GetType() > info2.GetType())
				nRet = 1;
			else
				nRet = -1;
        break;
/*
        case 2:     // uncompressed size
            if ( info1.dwFileSize < info2.dwFileSize )
                nRet = -1;
            else if ( info1.dwFileSize > info2.dwFileSize )
                nRet = 1;
        break;

        case 3:     // modified date/time
            {
            LONG lRet = CompareFileTime ( &info1.ftDateTime, &info2.ftDateTime );

            if ( 0 != lRet )
                nRet = lRet;
            }
        break;

        case 4:     // file attributes
            if ( info1.uAttribs < info2.uAttribs )
                nRet = -1;
            else if ( info1.uAttribs > info2.uAttribs )
                nRet = 1;
        break;

        case 5:     // prev/next CAB name
            if ( info1.location < info2.location )
                nRet = -1;
            else if ( info1.location > info2.location )
                nRet = 1;
            else if ( info1.location != CCompressedFileInfo::in_this_cab )
                {
                ATLASSERT(!info1.sOtherCabName.IsEmpty());
                ATLASSERT(!info2.sOtherCabName.IsEmpty());

                nRet = lstrcmpi ( info1.sOtherCabName, info2.sOtherCabName );
                }
        break;*/

        DEFAULT_UNREACHABLE;
        }

    // If the primary comparison in the switch returned equality (nRet is still 0),
    // then compare sz1/sz2 so equal items will still be sorted by file names.
    if ( 0 == nRet )
        nRet = lstrcmpi ( sz1, sz2 );

    return m_bSortAscending ? nRet : -nRet;
}


void CVGMFileListView::OnCloseFile(UINT uCode, int nID, HWND hwndCtrl)
{
	for (int i = GetNextItem(-1, LVNI_SELECTED); i != -1; i = GetNextItem(-1, LVNI_SELECTED) )
	{
		LVITEM item;
		item.iItem = i;
		item.iSubItem = 0;
		item.mask = LVIF_PARAM;
		GetItem(&item);
		pRoot->RemoveVGMFile((VGMFile*)item.lParam);
	}
}
