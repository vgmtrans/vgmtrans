// WTLCabViewView.cpp : implementation of the CWTLCabViewView class
/////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "resource.h"
#include "VGMCollListView.h"
#include "DragDropSource.h"
#include "WinVGMRoot.h"
#include "CollDialog.h"

CVGMCollListView theVGMCollListView;

struct __declspec(uuid("DE5BF786-477A-11d2-839D-00C04FD918D0")) IDragSourceHelper;

/////////////////////////////////////////////////////////////////////////////
// Construction

CVGMCollListView::CVGMCollListView() : m_nSortedCol(-1), m_bSortAscending(true)
{
}


/////////////////////////////////////////////////////////////////////////////
// Message handlers

BOOL CVGMCollListView::PreTranslateMessage ( MSG* pMsg )
{
    return FALSE;
}


LRESULT CVGMCollListView::OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
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

	//selectedColl = NULL;
	Init();

	bHandled = TRUE;
	//return lRet;
	return NULL;
}

void CVGMCollListView::OnDestroy()
{
    // Destroy the state image list. The other image lists shouldn't be destroyed
    // since they are copies of the system image list. Destroying those on Win 9x
    // is a Bad Thing to do.
    if ( m_imlState )
        m_imlState.Destroy();

    SetMsgHandled(false);
}

void CVGMCollListView::OnChar(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	switch (nChar)
		{
		case VK_SPACE:
			winroot.Play();
			break;
	}
}

void CVGMCollListView::OnDoubleClick(UINT nFlags, CPoint point) {
    winroot.Play();
}


LRESULT CVGMCollListView::OnContextMenu(HWND hwndCtrl, CPoint ptClick )
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

	VGMColl* pcoll = (VGMColl*)GetItemData(iItem);//(VGMFile*)treeitem.GetData();
	ClientToScreen(&ptClick);
	ItemContextMenu(hwndCtrl, ptClick, pcoll);

	return 0;
}


/////////////////////////////////////////////////////////////////////////////
// Notify handlers

/*LRESULT CVGMCollListView::OnColumnClick ( NMHDR* phdr )
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

    return 0;   // retval ignored
}*/
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

void CVGMCollListView::Init()
{
    InitColumns();
    InitImageLists();
    SetExtendedListViewStyle ( LVS_EX_HEADERDRAGDROP, LVS_EX_HEADERDRAGDROP );

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

void CVGMCollListView::Clear()
{
    DeleteAllItems();
	theCollDialog.Clear();

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

void CVGMCollListView::SetViewMode ( int nMode )
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
/*
LRESULT CVGMCollListView::OnLvnItemActivate(int idCtrl, LPNMHDR pnmh, BOOL& bHandled)
{
	NMLISTVIEW* listview = (NMLISTVIEW*)pnmh;

	if (listview->iItem == -1)
		return NULL;

	pMainFrame->ShowVGMFileFrame((VGMFile*)GetItemData(listview->iItem));
	bHandled = true;
	return NULL;
}*/

LRESULT CVGMCollListView::OnLvnItemChanged(int idCtrl, LPNMHDR pnmh, BOOL& bHandled)
{
	//CListViewCtrl* listview = reinterpret_cast<CListViewCtrl*>(pnmh);
	NM_LISTVIEW* pNMListView = (NM_LISTVIEW*)pnmh;
	
	if (pNMListView->iItem != -1 && pNMListView->uNewState & LVIS_FOCUSED)
	{
		
		VGMColl* coll = (VGMColl*)pNMListView->lParam;
		winroot.SelectColl(coll);
	}
	else
		winroot.SelectColl(NULL);
	bHandled = false;
	return 0;
}
/*
BOOL CVGMCollListView::SelectColl(VGMColl* coll)
{
	if (coll)		//test for NULL pointer
	{
		LVFINDINFO findinfo;
		findinfo.flags = LVFI_PARAM;
		findinfo.lParam = (DWORD_PTR)coll;
		if (itemsVGMFile)
		{
			selectedColl = coll;
		//	return CListViewCtrl::SelectItem(FindItem(&findinfo, -1));//items[itemsVGMFile]);
		}
	}
	return NULL;
}
*/

BOOL CVGMCollListView::SelectColl(VGMColl* coll)
{
	if (coll)		//check that the item passed was not NULL
	{
		LVFINDINFO findinfo;
		findinfo.flags = LVFI_PARAM;
		findinfo.lParam = (DWORD_PTR)coll;
		return CListViewCtrl::SelectItem(FindItem(&findinfo, -1));//items[vgmfile]);
	}
	return false;
}



void CVGMCollListView::AddColl(VGMColl* newColl)
{
	LVITEMW newItem;// = new LVITEMW();
	newItem.lParam = (DWORD_PTR)newColl;
	newItem.iItem = GetItemCount();
	newItem.iSubItem = 0;
	newItem.iImage = 0;
	//newItem.iGroupId = 0;
	newItem.pszText = (LPWSTR)newColl->GetName()->c_str();
	newItem.mask = LVIF_PARAM | LVIF_TEXT | LVIF_IMAGE;// | LVIF_GROUPID;
	InsertItem(&newItem);

	//newItem->iImage = NULL;//nIconIndexNormal;
}

void CVGMCollListView::RemoveColl(VGMColl* theColl)
{
	LVFINDINFO findinfo;
	findinfo.flags = LVFI_PARAM;
	findinfo.lParam = (DWORD_PTR)theColl;
	DeleteItem(FindItem(&findinfo, -1));//items[theFile]);
	
}


LRESULT CVGMCollListView::OnNMRClick(int idCtrl, LPNMHDR pnmh, BOOL& bHandled)
{
	SendMessage(WM_CONTEXTMENU, (WPARAM)m_hWnd, GetMessagePos());
	return 0;
}

/*
LRESULT CVGMCollListView::OnListBeginDrag ( NMHDR* phdr )
{
NMLISTVIEW* pnmlv = (NMLISTVIEW*) phdr;
std::vector<CDraggedFileInfo> vec;
CComObjectStack2<CDragDropSource> dropsrc;
DWORD dwEffect = 0;
HRESULT hr;
CComPtr<IDragSourceHelper> pdsh;

    // Get a list of the files being dragged (minus files that we can't extract
    // from the current CAB).
    if ( !GetDraggedFileInfo ( vec ) )
        {
        ATLTRACE("Error: Couldn't get list of files dragged (or only partial files were dragged)\n");
        return 0;   // do nothing
        }

    // Init the drag/drop data object.
    if ( !dropsrc.Init ( vec ) )
        {
        ATLTRACE("Error: Couldn't init drop source object\n");
        return 0;   // do nothing
        }

    // On 2K+, init a drag source helper object that will do the fancy drag
    // image when the user drags into Explorer (or another target that supports
    // the drag/drop helper).
    hr = pdsh.CoCreateInstance ( CLSID_DragDropHelper );

    if ( SUCCEEDED(hr) )
        {
        CComQIPtr<IDataObject> pdo;

        if ( pdo = dropsrc.GetUnknown() )
            pdsh->InitializeFromWindow ( this->m_hWnd, &pnmlv->ptAction, pdo );
        }

    // Start the drag/drop!
    hr = dropsrc.DoDragDrop ( DROPEFFECT_COPY, &dwEffect );

    if ( FAILED(hr) )
        ATLTRACE("Error: DoDragDrop() failed, error: 0x%08X\n", hr);
    else
        {
        // If we found any files continued into other CABs, update the UI.
        
        }

    return 0;
}*/
/*
bool CVGMCollListView::GetDraggedFileInfo ( std::vector<CDraggedFileInfo>& vec )
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
*/

/////////////////////////////////////////////////////////////////////////////
// Other methods

void CVGMCollListView::InitColumns()
{
    InsertColumn ( 0, _T("Collection Name"), LVCFMT_LEFT, 130, 0 );
	InsertColumn ( 1, _T("Type"), LVCFMT_LEFT, 40, 0 );
}

void CVGMCollListView::InitImageLists()
{
    // Create a state image list
	HICON hiColl = AtlLoadIconImage ( IDI_COLL, LR_DEFAULTCOLOR, 16, 16 );
	//HICON hiNext = AtlLoadIconImage ( IDI_NEXT_CAB, LR_DEFAULTCOLOR, 16, 16 );

    m_imlState.Create ( 16, 16, ILC_COLOR8|ILC_MASK, 2, 1 );

    m_imlState.AddIcon ( hiColl );
    //m_imlState.AddIcon ( hiNext );

    DestroyIcon ( hiColl );
    //DestroyIcon ( hiNext );

    SetImageList ( m_imlState, LVSIL_SMALL );

    // On pre-XP, we're done.
    if ( !g_bXPOrLater )
        return;
/*
    // Get the 48x48 system image list. 
HRESULT hr, (WINAPI* pfnGetImageList)(int, REFIID, void**) = NULL;
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
/*
int CALLBACK CVGMCollListView::SortCallback (
    LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort )
{
    ATLASSERT(NULL != lParam1);
    ATLASSERT(NULL != lParam2);
    ATLASSERT(NULL != lParamSort);

	VGMFile& info1 = *(VGMFile*) lParam1;
	VGMFile& info2 = *(VGMFile*) lParam2;
	CVGMCollListView* pThis = (CVGMCollListView*) lParamSort;

    return pThis->SortCallback ( info1, info2 );
}


int CVGMCollListView::SortCallback (
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

	VGMItem::ItemType type1;
	VGMItem::ItemType type2;


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

        DEFAULT_UNREACHABLE;
        }

    // If the primary comparison in the switch returned equality (nRet is still 0),
    // then compare sz1/sz2 so equal items will still be sorted by file names.
    if ( 0 == nRet )
        nRet = lstrcmpi ( sz1, sz2 );

    return m_bSortAscending ? nRet : -nRet;
}
*/
