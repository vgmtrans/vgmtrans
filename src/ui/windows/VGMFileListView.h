/**
 * VGMTrans (c) - 2002-2021
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#pragma once

#include "VGMFile.h"
#include "VGMTransWindow.h"

template <class T>
class VGMTransWindow;

#define VIEW_STYLES \
  (LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS | LVS_AUTOARRANGE)
#define VIEW_EX_STYLES (LVS_EX_FULLROWSELECT)

class CVGMFileListView : public CWindowImpl<CVGMFileListView, CListViewCtrl,
                                            CWinTraitsOR<VIEW_STYLES, VIEW_EX_STYLES>>,
                         public VGMTransWindow<CVGMFileListView> {
protected:
  typedef CVGMFileListView thisClass;

public:
  DECLARE_WND_SUPERCLASS(NULL, CListViewCtrl::GetWndClassName())

  BOOL PreTranslateMessage(MSG* pMsg);

  BEGIN_MSG_MAP(thisClass)
  MESSAGE_HANDLER(WM_CREATE, OnCreate)
  MSG_WM_SIZE(OnSize)
  MSG_WM_DESTROY(OnDestroy)
  MSG_WM_CONTEXTMENU(OnContextMenu)
  MSG_WM_KEYDOWN(OnKeyDown)
  COMMAND_ID_HANDLER_EX(IDC_CLOSEFILE, OnCloseFile)
  REFLECTED_NOTIFY_CODE_HANDLER_EX(LVN_COLUMNCLICK, OnColumnClick)
  REFLECTED_NOTIFY_CODE_HANDLER(LVN_ITEMACTIVATE, OnLvnItemActivate)
  REFLECTED_NOTIFY_CODE_HANDLER(LVN_ITEMCHANGED, OnLvnItemChanged)
  REFLECTED_NOTIFY_CODE_HANDLER_EX(LVN_BEGINDRAG, OnListBeginDrag)
  if (uMsg == WM_FORWARDMSG)
    if (PreTranslateMessage((LPMSG)lParam))
      return TRUE;

  DEFAULT_REFLECTION_HANDLER()
  END_MSG_MAP()

  LRESULT OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
  void OnSize(UINT nType, CSize size);
  void OnDestroy(void);
  void OnCloseFile(UINT uCode, int nID, HWND hwndCtrl);
  void OnKeyDown(TCHAR vkey, UINT repeats, UINT code);
  LRESULT OnContextMenu(HWND hwndCtrl, CPoint ptClick);

  // Notification Handlers
  LRESULT OnColumnClick(NMHDR* phdr);
  LRESULT OnLvnItemActivate(int idCtrl, LPNMHDR pnmh, BOOL& bHandled);
  LRESULT OnLvnItemChanged(int idCtrl, LPNMHDR pnmh, BOOL& bHandled);
  LRESULT OnListBeginDrag(NMHDR* phdr);

  void Init();
  void Clear();
  void SetViewMode(int nMode);
  void AddFile(VGMFile* newFile);
  void RemoveFile(VGMFile* theFile);
  BOOL SelectFile(VGMFile* vgmfile);
  bool GetDraggedFileInfo(std::vector<CDraggedFileInfo>& vec);

  BOOL SelectItem(VGMItem* item);

protected:
  int m_nSortedCol{-1};
  BOOL m_bSortAscending{TRUE};

  int nSampCollIcon;
  int nInstrSetIcon;
  int nSeqIcon;
  int nMiscIcon;

  CImageList m_imlSmall, m_imlLarge, m_imlTiles, m_imlState;
  CComPtr<IImageList> m_TileIml;
  VGMFile* selectedVGMFile;

  void InitImageLists();
  void InitColumns();

  static int CALLBACK SortCallback(LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort);
  int SortCallback(const VGMFile& info1, const VGMFile& info2) const;

  // Helpers
};

extern CVGMFileListView theVGMFileListView;

#undef VIEW_STYLES
#undef VIEW_EX_STYLES
