// PlainTextView.h : interface of the CPlainTextView class
//
/////////////////////////////////////////////////////////////////////////////

#if !defined(AFX_VGMFileTreeView_H__053AD676_0AE2_11D6_8BF1_00500477589F__INCLUDED_)
#define AFX_VGMFileTreeView_H__053AD676_0AE2_11D6_8BF1_00500477589F__INCLUDED_

#pragma once

using namespace stdext;

#include "VGMFile.h"
#include "VGMTransWindow.h"

template <class T> class VGMTransWindow;

typedef CWinTraits<WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_HSCROLL | WS_VSCROLL | WS_EX_CLIENTEDGE | TVS_HASBUTTONS> CVGMFileTreeViewWinTraits;

class CVGMFileTreeView : public CWindowImpl<CVGMFileTreeView, CTreeViewCtrlEx, CVGMFileTreeViewWinTraits>,
						 public VGMTransWindow<CVGMFileTreeView>
					//public CEditCommands<CMyTreeView>
{
protected:
	typedef CVGMFileTreeView thisClass;
	typedef CWindowImpl<CVGMFileTreeView, CTreeViewCtrlEx, CVGMFileTreeViewWinTraits> baseClass;

public:
	CVGMFileTreeView();

protected:
	WTL::CImageList m_ImageList;
	int nIconFolderIndexNormal, nIconFolderIndexSelected;
	int nIconIndexNormal, nIconIndexSelected;
	hash_map<VGMFile*, CTreeItem> items;

public:
	DECLARE_WND_SUPERCLASS(NULL, CTreeViewCtrlEx::GetWndClassName())

	BOOL PreTranslateMessage(MSG* pMsg);

	BEGIN_MSG_MAP(thisClass)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MSG_WM_CONTEXTMENU(OnContextMenu)
		MSG_WM_LBUTTONDBLCLK(OnLButtonDblClk)
		REFLECTED_NOTIFY_CODE_HANDLER(TVN_SELCHANGED, OnTvnSelchanged) 
		REFLECTED_NOTIFY_CODE_HANDLER(NM_RCLICK, OnNMRClick)
		if(uMsg == WM_FORWARDMSG)
			if(PreTranslateMessage((LPMSG)lParam)) return TRUE;
		//CHAIN_MSG_MAP_ALT(CEditCommands<CPlainTextView>, 1)

		DEFAULT_REFLECTION_HANDLER()
	END_MSG_MAP()

	LRESULT OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled);
	void OnLButtonDblClk(UINT nFlags, CPoint point);
	LRESULT OnTvnSelchanged(int idCtrl, LPNMHDR pnmh, BOOL& bHandled);
	LRESULT OnNMRClick(int idCtrl, LPNMHDR pnmh, BOOL& bHandled);
	LRESULT OnContextMenu(HWND hwndCtrl, CPoint ptClick );
	
	void Init(int cx, int cy);
	void AddFile(VGMFile* newFile);
	void RemoveFile(VGMFile* theFile);
	BOOL SelectFile(VGMFile* vgmfile);

	BOOL SelectItem(VGMItem* item);

// Helpers

};

extern CVGMFileTreeView VGMFilesView;

/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_PlainTextView_H__053AD676_0AE2_11D6_8BF1_00500477589F__INCLUDED_)
