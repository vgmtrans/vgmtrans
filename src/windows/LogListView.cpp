// WTLCabViewView.cpp : implementation of the CWTLCabViewView class
/////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "resource.h"
#include "LogListView.h"
#include "WinVGMRoot.h"
#include "mainfrm.h"

CLogListView theLogListView;

/////////////////////////////////////////////////////////////////////////////
// Construction

CLogListView::CLogListView() : m_nSortedCol(-1), m_bSortAscending(true)
{
}


/////////////////////////////////////////////////////////////////////////////
// Message handlers

BOOL CLogListView::PreTranslateMessage ( MSG* pMsg )
{
	return FALSE;
}


LRESULT CLogListView::OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
	// "base::OnCreate()"
	LRESULT lRet = DefWindowProc(uMsg, wParam, lParam);

	Init();

	bHandled = TRUE;
	//return lRet;
	return NULL;
}

void CLogListView::OnDestroy()
{
	// Destroy the state image list. The other image lists shouldn't be destroyed
	// since they are copies of the system image list. Destroying those on Win 9x
	// is a Bad Thing to do.
	if ( m_imlState )
		m_imlState.Destroy();

	SetMsgHandled(false);
}

void CLogListView::OnSize(UINT nType, CSize size)
{
	int cx = size.cx;
	SetColumnWidth(0, cx - GetColumnWidth(1));
	ShowScrollBar(SB_HORZ, FALSE);				//this is a hacky solution.  Not sure why horiz scrollbar is there in the first place
}


/////////////////////////////////////////////////////////////////////////////
// Notify handlers

LRESULT CLogListView::OnColumnClick ( NMHDR* phdr )
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


/////////////////////////////////////////////////////////////////////////////
// Operations

void CLogListView::Init()
{
	InitColumns();
	InitImageLists();

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

void CLogListView::Clear()
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

void CLogListView::SetViewMode ( int nMode )
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

void CLogListView::AddLogItem(LogItem* newLog)
{
	LVITEMW newItem;
	newItem.lParam = (DWORD_PTR)newLog;
	newItem.iItem = GetItemCount();
	newItem.iSubItem = 0;
	switch (newLog->GetLogLevel())
	{
	case LOG_LEVEL_ERR:
		newItem.iImage = nErrIcon;
		break;
	case LOG_LEVEL_WARN:
		newItem.iImage = nWarnIcon;
		break;
	case LOG_LEVEL_INFO:
		newItem.iImage = nInfoIcon;
		break;
	default:
		newItem.iImage = NULL;
		break;
	}
	newItem.pszText = (LPWSTR)newLog->GetCText();
	newItem.mask = LVIF_PARAM | LVIF_TEXT | LVIF_IMAGE;
	InsertItem(&newItem);
	switch (newLog->GetLogLevel())
	{
	case LOG_LEVEL_ERR:
		SetItemText(newItem.iItem, 1, L"Error");
		break;
	case LOG_LEVEL_WARN:
		SetItemText(newItem.iItem, 1, L"Warning");
		break;
	case LOG_LEVEL_INFO:
		SetItemText(newItem.iItem, 1, L"Information");
		break;
	default:
		SetItemText(newItem.iItem, 1, L"Unknown");
		break;
	}
}

void CLogListView::RemoveLogItem(LogItem* theLog)
{
	LVFINDINFO findinfo;
	findinfo.flags = LVFI_PARAM;
	findinfo.lParam = (DWORD_PTR)theLog;
	DeleteItem(FindItem(&findinfo, -1));
}


/////////////////////////////////////////////////////////////////////////////
// Other methods

void CLogListView::InitColumns()
{
	InsertColumn ( 0, _T("Log"), LVCFMT_LEFT, 280, 0 );
	InsertColumn ( 1, _T("Type"), LVCFMT_LEFT, 120, 0 );
}

void CLogListView::InitImageLists()
{
	nErrIcon = -1;
	nWarnIcon = -1;
	nInfoIcon = -1;

	// Create a state image list
	HICON hiErr = (HICON)::LoadImage(
				_Module.GetResourceInstance(),
				MAKEINTRESOURCE(IDI_LEVEL_ERROR),
				IMAGE_ICON, 16, 16, LR_SHARED);
	HICON hiWarn = (HICON)::LoadImage(
				_Module.GetResourceInstance(),
				MAKEINTRESOURCE(IDI_LEVEL_WARNING),
				IMAGE_ICON, 16, 16, LR_SHARED);
	HICON hiInfo = (HICON)::LoadImage(
				_Module.GetResourceInstance(),
				MAKEINTRESOURCE(IDI_LEVEL_INFO),
				IMAGE_ICON, 16, 16, LR_SHARED);

	BOOL bCreate = m_imlState.Create ( 16, 16, ILC_COLOR32 | ILC_MASK, 3, 1 );
	m_imlState.SetBkColor(this->GetBkColor());

	nErrIcon = m_imlState.AddIcon ( hiErr );
	nWarnIcon = m_imlState.AddIcon ( hiWarn );
	nInfoIcon = m_imlState.AddIcon ( hiInfo );

	DestroyIcon ( hiErr );
	DestroyIcon ( hiWarn );
	DestroyIcon ( hiInfo );

	SetImageList(m_imlState, LVSIL_SMALL);

	// On pre-XP, we're done.
	if ( !g_bXPOrLater )
		return;
}

int CALLBACK CLogListView::SortCallback (
	LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort )
{
	ATLASSERT(NULL != lParam1);
	ATLASSERT(NULL != lParam2);
	ATLASSERT(NULL != lParamSort);

	LogItem& info1 = *(LogItem*) lParam1;
	LogItem& info2 = *(LogItem*) lParam2;
	CLogListView* pThis = (CLogListView*) lParamSort;

	return pThis->SortCallback ( info1, info2 );
}


int CLogListView::SortCallback (
	const LogItem& info1, const LogItem& info2 ) const
{
	int nRet = 0;
	LPCTSTR sz1, sz2;

	ATLASSERT(-1 != m_nSortedCol);

	// Default to comparing the log text, unless something in the switch
	// changes these pointers.
	sz1 = info1.GetCText();
	sz2 = info2.GetCText();

	switch ( m_nSortedCol )
	{
		case 0:     // file name
			break;

		case 1:     // type description
			if (info1.GetLogLevel() > info2.GetLogLevel())
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
