/**
 * VGMTrans (c) - 2002-2021
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

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
// Message handlers

BOOL CVGMFileListView::PreTranslateMessage(MSG* pMsg) {
  return FALSE;
}

LRESULT CVGMFileListView::OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
  LRESULT lRet = DefWindowProc(uMsg, wParam, lParam);

  Init();

  bHandled = TRUE;
  return NULL;
}

void CVGMFileListView::OnDestroy() {
  // Destroy the state image list. The other image lists shouldn't be destroyed
  // since they are copies of the system image list. Destroying those on Win 9x
  // is a Bad Thing to do.
  if (m_imlState)
    m_imlState.Destroy();

  SetMsgHandled(FALSE);
}

void CVGMFileListView::OnSize(UINT nType, CSize size) {
  int cx = size.cx;
  SetColumnWidth(0, cx - GetColumnWidth(1));
  ShowScrollBar(SB_HORZ, FALSE);  // this is a hacky solution.  Not sure why horiz scrollbar is
                                  // there in the first place
}

void CVGMFileListView::OnKeyDown(TCHAR nChar, UINT repeats, UINT code) {
  switch (nChar) {
    case VK_DELETE:
      OnCloseFile(0, 0, 0);
      break;
  }
}

LRESULT CVGMFileListView::OnContextMenu(HWND hwndCtrl, CPoint ptClick) {
  // if Shift-F10
  if (ptClick.x == -1 && ptClick.y == -1) {
    ptClick = (CPoint)GetMessagePos();
  }

  ScreenToClient(&ptClick);

  UINT uFlags;
  int iItem = HitTest(ptClick, &uFlags);
  if (iItem == -1) {
    return 0;
  }

  std::vector<VGMFile*> vgmfiles;
  UINT selectedCount = GetSelectedCount();
  if (selectedCount > 1) {
    iItem = -1;
    for (UINT i = 0; i < selectedCount; i++) {
      iItem = GetNextItem(iItem, LVNI_SELECTED);
      ATLASSERT(iItem != -1);
      vgmfiles.push_back((VGMFile*)GetItemData(iItem));
    }
  } else {
    vgmfiles.push_back((VGMFile*)GetItemData(iItem));
  }

  if (vgmfiles.size() > 1) {
    CMenu mnuContext;
    mnuContext.LoadMenu(IDR_RAWFILE);
    CMenuHandle pPopup = mnuContext.GetSubMenu(0);
    ClientToScreen(&ptClick);
    pPopup.TrackPopupMenu(TPM_LEFTALIGN, ptClick.x, ptClick.y, hwndCtrl);
  } else {
    VGMFile* pvgmfile = vgmfiles[0];
    ClientToScreen(&ptClick);
    ItemContextMenu(hwndCtrl, ptClick, pvgmfile);
  }

  return NULL;
}

/////////////////////////////////////////////////////////////////////////////
// Notify handlers

LRESULT CVGMFileListView::OnColumnClick(NMHDR* phdr) {
  CWaitCursor w;
  int nCol = ((NMLISTVIEW*)phdr)->iSubItem;

  // If the user clicked the column that is already sorted, reverse the sort
  // direction. Otherwise, go back to ascending order.
  if (nCol == m_nSortedCol)
    m_bSortAscending = !m_bSortAscending;
  else
    m_bSortAscending = true;

  HDITEM hdi = {HDI_FORMAT};
  CHeaderCtrl wndHdr = GetHeader();

  // Remove the sort arrow indicator from the previously-sorted column.
  if (-1 != m_nSortedCol) {
    wndHdr.GetItem(m_nSortedCol, &hdi);
    hdi.fmt &= ~(HDF_SORTDOWN | HDF_SORTUP);
    wndHdr.SetItem(m_nSortedCol, &hdi);
  }

  // Add the sort arrow to the new sorted column.
  hdi.mask = HDI_FORMAT;
  wndHdr.GetItem(nCol, &hdi);
  hdi.fmt |= m_bSortAscending ? HDF_SORTUP : HDF_SORTDOWN;
  wndHdr.SetItem(nCol, &hdi);

  // Store the column being sorted, and do the sort
  m_nSortedCol = nCol;

  SortItems(SortCallback, (LPARAM)(DWORD_PTR)this);

  // Have the list ctrl indicate the sorted column by changing its color.
  SetSelectedColumn(nCol);

  return TRUE;
}

/////////////////////////////////////////////////////////////////////////////
// Operations

void CVGMFileListView::Init() {
  selectedVGMFile = NULL;

  InitColumns();
  InitImageLists();

  SetExtendedListViewStyle(LVS_EX_HEADERDRAGDROP, LVS_EX_HEADERDRAGDROP);

  // Turning on LVS_EX_DOUBLEBUFFER also enables the transparent
  // selection marquee.
  SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER, LVS_EX_DOUBLEBUFFER);

  // Each tile will have 2 additional lines (3 lines total).
  LVTILEVIEWINFO lvtvi = {sizeof(LVTILEVIEWINFO), LVTVIM_COLUMNS};

  lvtvi.cLines = 2;
  lvtvi.dwFlags = LVTVIF_AUTOSIZE;
  SetTileViewInfo(&lvtvi);
}

void CVGMFileListView::Clear() {
  DeleteAllItems();

  if (-1 != m_nSortedCol) {
    // Remove the sort arrow indicator from the sorted column.
    HDITEM hdi = {HDI_FORMAT};
    CHeaderCtrl wndHdr = GetHeader();

    wndHdr.GetItem(m_nSortedCol, &hdi);
    hdi.fmt &= ~(HDF_SORTDOWN | HDF_SORTUP);
    wndHdr.SetItem(m_nSortedCol, &hdi);

    // Remove the sorted column color from the list.
    SetSelectedColumn(-1);

    m_nSortedCol = -1;
    m_bSortAscending = true;
  }
}

void CVGMFileListView::SetViewMode(int nMode) {
  ATLASSERT(nMode >= LV_VIEW_ICON && nMode <= LV_VIEW_TILE);

  if (LV_VIEW_TILE == nMode) {
    SetImageList(m_imlTiles, LVSIL_NORMAL);
  } else {
    SetImageList(m_imlLarge, LVSIL_NORMAL);
  }

  SetView(nMode);
}

LRESULT CVGMFileListView::OnLvnItemActivate(int idCtrl, LPNMHDR pnmh, BOOL& bHandled) {
  NMLISTVIEW* listview = (NMLISTVIEW*)pnmh;

  if (listview->iItem == -1)
    return NULL;

  pMainFrame->ShowVGMFileFrame((VGMFile*)GetItemData(listview->iItem));
  bHandled = true;
  return NULL;
}

LRESULT CVGMFileListView::OnLvnItemChanged(int idCtrl, LPNMHDR pnmh, BOOL& bHandled) {
  // CListViewCtrl* listview = reinterpret_cast<CListViewCtrl*>(pnmh);
  NM_LISTVIEW* pNMListView = (NM_LISTVIEW*)pnmh;

  if (pNMListView->iItem != -1 && pNMListView->uNewState & LVIS_FOCUSED) {
    VGMFile* vgmfile = (VGMFile*)pNMListView->lParam;
    winroot.SelectItem(vgmfile);
  }
  bHandled = false;
  return 0;
}

BOOL CVGMFileListView::SelectItem(VGMItem* item) {
  if (item) {
    VGMFile* itemsVGMFile = item->vgmfile;
    if (selectedVGMFile == itemsVGMFile)
      return NULL;
    LVFINDINFO findinfo;
    findinfo.flags = LVFI_PARAM;
    findinfo.lParam = (DWORD_PTR)itemsVGMFile;
    if (itemsVGMFile) {
      selectedVGMFile = itemsVGMFile;
      // return CListViewCtrl::SelectItem(FindItem(&findinfo, -1));//items[itemsVGMFile]);
    }
  }
  return NULL;
}

BOOL CVGMFileListView::SelectFile(VGMFile* vgmfile) {
  if (vgmfile) {
    LVFINDINFO findinfo;
    findinfo.flags = LVFI_PARAM;
    findinfo.lParam = (DWORD_PTR)vgmfile;
    return CListViewCtrl::SelectItem(FindItem(&findinfo, -1));  // items[vgmfile]);
  }
  return false;
}

void CVGMFileListView::AddFile(VGMFile* newFile) {
  LVITEMW newItem;
  newItem.lParam = (DWORD_PTR)newFile;
  newItem.iItem = GetItemCount();
  newItem.iSubItem = 0;
  switch (newFile->GetFileType()) {
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
  // newItem.iGroupId = 0;
  newItem.pszText = (LPWSTR)newFile->GetName()->c_str();
  newItem.mask = LVIF_PARAM | LVIF_TEXT | LVIF_IMAGE;
  InsertItem(&newItem);

  switch (newFile->GetFileType()) {
    case FILETYPE_INSTRSET:
      SetItemText(newItem.iItem, 1, L"Instrument Set");
      break;
    case FILETYPE_SAMPCOLL:
      SetItemText(newItem.iItem, 1, L"Sample Collection");
      break;
    case FILETYPE_SEQ:
      SetItemText(newItem.iItem, 1, L"Sequence");
      break;
  }
}

void CVGMFileListView::RemoveFile(VGMFile* theFile) {
  LVFINDINFO findinfo;
  findinfo.flags = LVFI_PARAM;
  findinfo.lParam = (DWORD_PTR)theFile;
  DeleteItem(FindItem(&findinfo, -1));
}

LRESULT CVGMFileListView::OnListBeginDrag(NMHDR* phdr) {
  return 0;
}

bool CVGMFileListView::GetDraggedFileInfo(std::vector<CDraggedFileInfo>& vec) {
  int nIdx = -1;

  while ((nIdx = GetNextItem(nIdx, LVNI_SELECTED)) != -1) {
    VGMFile* vgmfile;

    vgmfile = (VGMFile*)GetItemData(nIdx);
    ATLASSERT(vgmfile != NULL);

    vec.push_back(CDraggedFileInfo(vgmfile->GetName()->c_str(), nIdx));
  }

  return !vec.empty();
}

/////////////////////////////////////////////////////////////////////////////
// Other methods

void CVGMFileListView::InitColumns() {
  InsertColumn(0, _T("File Name"), LVCFMT_LEFT, 130, 0);
  InsertColumn(1, _T("Type"), LVCFMT_LEFT, 90, 0);
}

void CVGMFileListView::InitImageLists() {
  nSampCollIcon = -1;
  nInstrSetIcon = -1;
  nSeqIcon = -1;

  // Create a state image list
  HICON hiSampColl =
      (HICON)::LoadImage(_Module.GetResourceInstance(), MAKEINTRESOURCE(IDI_VGMFILE_SAMPCOLL),
                         IMAGE_ICON, 16, 16, LR_SHARED);
  HICON hiInstrSet =
      (HICON)::LoadImage(_Module.GetResourceInstance(), MAKEINTRESOURCE(IDI_VGMFILE_INSTRSET),
                         IMAGE_ICON, 16, 16, LR_SHARED);
  HICON hiSeq = (HICON)::LoadImage(_Module.GetResourceInstance(), MAKEINTRESOURCE(IDI_VGMFILE_SEQ),
                                   IMAGE_ICON, 16, 16, LR_SHARED);
  HICON hiMisc = (HICON)::LoadImage(_Module.GetResourceInstance(), MAKEINTRESOURCE(IDI_BINARY),
                                    IMAGE_ICON, 16, 16, LR_SHARED);

  BOOL bCreate = m_imlState.Create(16, 16, ILC_COLOR32 | ILC_MASK, 3, 1);
  m_imlState.SetBkColor(this->GetBkColor());  // RGB(255,255,255));

  nSampCollIcon = m_imlState.AddIcon(hiSampColl);
  nInstrSetIcon = m_imlState.AddIcon(hiInstrSet);
  nSeqIcon = m_imlState.AddIcon(hiSeq);
  nMiscIcon = m_imlState.AddIcon(hiMisc);

  DestroyIcon(hiSampColl);
  DestroyIcon(hiInstrSet);
  DestroyIcon(hiSeq);
  DestroyIcon(hiMisc);

  SetImageList(m_imlState, LVSIL_SMALL);
}

int CALLBACK CVGMFileListView::SortCallback(LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort) {
  ATLASSERT(NULL != lParam1);
  ATLASSERT(NULL != lParam2);
  ATLASSERT(NULL != lParamSort);

  VGMFile& info1 = *(VGMFile*)lParam1;
  VGMFile& info2 = *(VGMFile*)lParam2;
  CVGMFileListView* pThis = (CVGMFileListView*)lParamSort;

  return pThis->SortCallback(info1, info2);
}

int CVGMFileListView::SortCallback(const VGMFile& info1, const VGMFile& info2) const {
  int nRet = 0;
  LPCTSTR sz1, sz2;

  ATLASSERT(-1 != m_nSortedCol);
  // Default to comparing the file names, unless something in the switch
  // changes these pointers.
  sz1 = info1.GetName()->c_str();
  sz2 = info2.GetName()->c_str();

  switch (m_nSortedCol) {
    case 0:  // file name
      break;

    case 1:  // type description
      // ATLASSERT(!info1.sFileType.IsEmpty());
      // ATLASSERT(!info2.sFileType.IsEmpty());

      if (info1.GetType() > info2.GetType())
        nRet = 1;
      else
        nRet = -1;
      break;

      DEFAULT_UNREACHABLE;
  }

  // If the primary comparison in the switch returned equality (nRet is still 0),
  // then compare sz1/sz2 so equal items will still be sorted by file names.
  if (0 == nRet)
    nRet = lstrcmpi(sz1, sz2);

  return m_bSortAscending ? nRet : -nRet;
}

void CVGMFileListView::OnCloseFile(UINT uCode, int nID, HWND hwndCtrl) {
  for (int i = GetNextItem(-1, LVNI_SELECTED); i != -1; i = GetNextItem(-1, LVNI_SELECTED)) {
    LVITEM item;
    item.iItem = i;
    item.iSubItem = 0;
    item.mask = LVIF_PARAM;
    GetItem(&item);
    pRoot->RemoveVGMFile((VGMFile*)item.lParam);
  }
}
