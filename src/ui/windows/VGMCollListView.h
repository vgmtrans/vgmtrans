/**
 * VGMTrans (c) - 2002-2021
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#pragma once

#include "VGMColl.h"
#include "VGMTransWindow.h"

template <class T>
class VGMTransWindow;

#define VIEW_STYLES                                                                              \
  (LVS_LIST | LVS_SHOWSELALWAYS | /*LVS_SINGLESEL |*/ LVS_NOCOLUMNHEADER | LVS_SHAREIMAGELISTS | \
   LVS_AUTOARRANGE)
#define VIEW_EX_STYLES 0

class CVGMCollListView : public CWindowImpl<CVGMCollListView, CListViewCtrl,
                                            CWinTraitsOR<VIEW_STYLES, VIEW_EX_STYLES>>,
                         public VGMTransWindow<CVGMCollListView> {
protected:
  typedef CVGMCollListView thisClass;

public:
  DECLARE_WND_SUPERCLASS(NULL, CListViewCtrl::GetWndClassName())

  BOOL PreTranslateMessage(MSG* pMsg);

  BEGIN_MSG_MAP(thisClass)
  MESSAGE_HANDLER(WM_CREATE, OnCreate)
  MSG_WM_DESTROY(OnDestroy)
  MSG_WM_CHAR(OnChar)
  MSG_WM_CONTEXTMENU(OnContextMenu)

  REFLECTED_NOTIFY_CODE_HANDLER(LVN_ITEMCHANGED, OnLvnItemChanged)
  if (uMsg == WM_FORWARDMSG)
    if (PreTranslateMessage((LPMSG)lParam))
      return TRUE;

  REFLECT_NOTIFICATIONS()
  END_MSG_MAP()

  LRESULT OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
  void OnDestroy(void);
  void OnChar(UINT nChar, UINT nRepCnt, UINT nFlags);
  LRESULT OnContextMenu(HWND hwndCtrl, CPoint ptClick);

  // Notification Handlers
  LRESULT OnLvnItemChanged(int idCtrl, LPNMHDR pnmh, BOOL& bHandled);
  LRESULT OnNMRClick(int idCtrl, LPNMHDR pnmh, BOOL& bHandled);

  void Init();
  void Clear();
  void SetViewMode(int nMode);
  void AddColl(VGMColl* newColl);
  void RemoveColl(VGMColl* theColl);
  BOOL SelectColl(VGMColl* coll);

protected:
  int m_nSortedCol{-1};
  BOOL m_bSortAscending{TRUE};

  CImageList m_imlSmall, m_imlLarge, m_imlTiles, m_imlState;
  CComPtr<IImageList> m_TileIml;

  void InitImageLists();
  void InitColumns();
};

extern CVGMCollListView theVGMCollListView;

#undef VIEW_STYLES
#undef VIEW_EX_STYLES
