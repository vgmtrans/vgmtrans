/**
 * VGMTrans (c) - 2002-2021
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#include "pch.h"
#include "resource.h"
#include "RawFileListView.h"
#include "mainfrm.h"
#include "HexView.h"
#include "Root.h"
#include "BmpBtn.h"

CRawFileListView rawFileListView;

BOOL CRawFileListView::PreTranslateMessage(MSG* pMsg) {
  return FALSE;
}

LRESULT CRawFileListView::OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
  LRESULT lRet = DefWindowProc(uMsg, wParam, lParam);

  Init();

  bHandled = TRUE;
  return lRet;
}

LRESULT CRawFileListView::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/,
                                    BOOL& bHandled) {
  BOOL bDestroy = m_ImageList.Destroy();
  bDestroy;  // avoid level 4 warning

  bHandled = FALSE;
  return 0;
}

void CRawFileListView::OnPaint(CDCHandle /*dc*/) {
  SetMsgHandled(false);
}

void CRawFileListView::OnSize(UINT nType, CSize size) {
  SetColumnWidth(0, size.cx);
  ShowScrollBar(SB_HORZ, FALSE);
}

void CRawFileListView::Init() {
  InsertColumn(0, _T("File Name"), LVCFMT_LEFT, 130, 0);
  // InsertColumn ( 1, _T("Type"), LVCFMT_LEFT, 90, 0 );

  InitImageLists();

  // Turning on LVS_EX_DOUBLEBUFFER also enables the transparent
  // selection marquee.
  SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER, LVS_EX_DOUBLEBUFFER);

  // Each tile will have 2 additional lines (3 lines total).
  LVTILEVIEWINFO lvtvi = {sizeof(LVTILEVIEWINFO), LVTVIM_COLUMNS};

  lvtvi.cLines = 2;
  lvtvi.dwFlags = LVTVIF_AUTOSIZE;
  SetTileViewInfo(&lvtvi);
}

void CRawFileListView::InitImageLists() {
  nGenericFileIcon = -1;

  HICON hiGenericFile = (HICON)::LoadImage(
      _Module.GetResourceInstance(), MAKEINTRESOURCE(IDI_RAWFILE), IMAGE_ICON, 16, 16, LR_SHARED);

  BOOL bCreate = m_ImageList.Create(16, 16, ILC_COLOR32 | ILC_MASK, 3, 1);
  m_ImageList.SetBkColor(GetBkColor());

  nGenericFileIcon = m_ImageList.AddIcon(hiGenericFile);
  DestroyIcon(hiGenericFile);
  SetImageList(m_ImageList, LVSIL_SMALL);
}

void CRawFileListView::AddFile(RawFile* newFile) {
  LVITEMW newItem;  // = new LVITEMW();
  newItem.lParam = (DWORD_PTR)newFile;
  newItem.iItem = GetItemCount();
  newItem.iSubItem = 0;
  newItem.iImage = nGenericFileIcon;
  newItem.pszText = (LPWSTR)newFile->GetFileName();
  newItem.mask = LVIF_PARAM | LVIF_TEXT | LVIF_IMAGE;
  InsertItem(&newItem);
}

void CRawFileListView::RemoveFile(RawFile* theFile) {
  LVFINDINFO findinfo;
  findinfo.flags = LVFI_PARAM;
  findinfo.lParam = (DWORD_PTR)theFile;
  DeleteItem(FindItem(&findinfo, -1));  // items[theFile]);
}

void CRawFileListView::OnKeyDown(TCHAR nChar, UINT repeats, UINT code) {
  switch (nChar) {
    case VK_DELETE:
      OnCloseFile(0, 0, 0);
      break;
  }
}

LRESULT CRawFileListView::OnTvnSelchanged(int idCtrl, LPNMHDR pnmh, BOOL& bHandled) {
  CTreeViewCtrlEx* treeview = reinterpret_cast<CTreeViewCtrlEx*>(pnmh);
  CTreeItem treeitem = treeview->GetSelectedItem();

  bHandled = false;
  return 0;
}

LRESULT CRawFileListView::OnNMRClick(int idCtrl, LPNMHDR pnmh, BOOL& bHandled) {
  SendMessage(WM_CONTEXTMENU, (WPARAM)m_hWnd, GetMessagePos());
  return 0;
}

LRESULT CRawFileListView::OnContextMenu(HWND hwndCtrl, CPoint ptClick) {
  // Build up the menu to show
  CMenu mnuContext;

  // if Shift-F10
  if (ptClick.x == -1 && ptClick.y == -1)
    ptClick = (CPoint)GetMessagePos();

  ScreenToClient(&ptClick);

  UINT uFlags;
  // HTREEITEM htItem = GetTreeCtrl().HitTest( ptMousePos, &uFlags );
  int iItem = HitTest(ptClick, &uFlags);

  if (iItem == -1)
    return 0;

  // Load from resource
  mnuContext.LoadMenu(IDR_RAWFILE);
  CMenuHandle pPopup = mnuContext.GetSubMenu(0);
  ClientToScreen(&ptClick);
  pPopup.TrackPopupMenu(TPM_LEFTALIGN, ptClick.x, ptClick.y, hwndCtrl);

  return 0;
}

void CRawFileListView::OnSaveAsRaw(UINT uCode, int nID, HWND hwndCtrl) {
  std::vector<RawFile*> files;
  for (int i = GetNextItem(-1, LVNI_SELECTED); i != -1; i = GetNextItem(i, LVNI_SELECTED)) {
    LVITEM item;
    item.iItem = i;
    item.iSubItem = 0;
    item.mask = LVIF_PARAM;
    GetItem(&item);
    files.push_back((RawFile*)item.lParam);
  }

  for (std::vector<RawFile*>::iterator it = files.begin(); it != files.end(); ++it) {
    (*it)->OnSaveAsRaw();
  }
}

void CRawFileListView::OnCloseFile(UINT uCode, int nID, HWND hwndCtrl) {
  int iItem;
  while ((iItem = GetNextItem(-1, LVNI_SELECTED)) != -1) {
    LVITEM item;
    item.iItem = iItem;
    item.iSubItem = 0;
    item.mask = LVIF_PARAM;
    GetItem(&item);
    pRoot->CloseRawFile((RawFile*)item.lParam);
  }
}
