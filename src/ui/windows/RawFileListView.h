/**
 * VGMTrans (c) - 2002-2021
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#pragma once

#include "VGMTransWindow.h"

class RawFile;
class CBmpBtn;

#define VIEW_STYLES                                                                                \
  (LVS_REPORT | LVS_SHOWSELALWAYS | LVS_NOCOLUMNHEADER | LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS | \
   LVS_AUTOARRANGE)
#define VIEW_EX_STYLES (LVS_EX_FULLROWSELECT)

class CRawFileListView : public CWindowImpl<CRawFileListView, CListViewCtrl,
                                            CWinTraitsOR<VIEW_STYLES, VIEW_EX_STYLES>>,
                         public VGMTransWindow<CRawFileListView> {
protected:
  typedef CRawFileListView thisClass;
  typedef CWindowImpl<CRawFileListView, CListViewCtrl, CWinTraitsOR<VIEW_STYLES, VIEW_EX_STYLES>>
      baseClass;

protected:
  WTL::CImageList m_ImageList;
  int nGenericFileIcon{};

public:
  DECLARE_WND_SUPERCLASS(NULL, CListViewCtrl::GetWndClassName())

  BOOL PreTranslateMessage(MSG* pMsg);

  BEGIN_MSG_MAP(thisClass)
  MESSAGE_HANDLER(WM_CREATE, OnCreate)
  MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
  MSG_WM_SIZE(OnSize)
  MSG_WM_KEYDOWN(OnKeyDown)
  MSG_WM_CONTEXTMENU(OnContextMenu)
  COMMAND_ID_HANDLER_EX(ID_SAVERAWFILE, OnSaveAsRaw)
  COMMAND_ID_HANDLER_EX(IDC_CLOSEFILE, OnCloseFile)
  REFLECTED_NOTIFY_CODE_HANDLER(TVN_SELCHANGED, OnTvnSelchanged)

  if (uMsg == WM_FORWARDMSG)
    if (PreTranslateMessage((LPMSG)lParam))
      return TRUE;

  DEFAULT_REFLECTION_HANDLER()
  END_MSG_MAP()

  LRESULT OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
  LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled);
  void OnPaint(CDCHandle /*unused*/);
  void OnSize(UINT nType, CSize size);
  void OnKeyDown(TCHAR vkey, UINT repeats, UINT code);
  void OnLButtonDblClk(UINT nFlags, CPoint point);
  LRESULT OnTvnSelchanged(int idCtrl, LPNMHDR pnmh, BOOL& bHandled);
  LRESULT OnNMRClick(int idCtrl, LPNMHDR pnmh, BOOL& bHandled);
  LRESULT OnContextMenu(HWND hwndCtrl, CPoint ptClick);
  void OnSaveAsRaw(UINT uCode, int nID, HWND hwndCtrl);
  void OnCloseFile(UINT uCode, int nID, HWND hwndCtrl);

  void Init();
  void InitImageLists();
  void AddFile(RawFile* newFile);
  void RemoveFile(RawFile* theFile);
};

extern CRawFileListView rawFileListView;

#undef VIEW_STYLES
#undef VIEW_EX_STYLES
