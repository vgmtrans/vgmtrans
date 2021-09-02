// PlainTextView.h : interface of the CPlainTextView class
//
/////////////////////////////////////////////////////////////////////////////

#if !defined(AFX_ItemTreeView_H__053AD676_0AE2_11D6_8BF1_00500477589F__INCLUDED_)
#define AFX_ItemTreeView_H__053AD676_0AE2_11D6_8BF1_00500477589F__INCLUDED_

#pragma once

#include "VGMFile.h"
#include "VGMTransWindow.h"

class CHexViewFrame;

template <class T> class VGMTransWindow;

typedef CWinTraits<WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_HSCROLL | WS_VSCROLL | WS_EX_CLIENTEDGE |  TVS_SHOWSELALWAYS | TVS_HASBUTTONS> CItemTreeViewWinTraits;

class CItemTreeView : public CWindowImpl<CItemTreeView, CTreeViewCtrlEx, CItemTreeViewWinTraits>,
					  public VGMTransWindow<CItemTreeView>
					//public CEditCommands<CMyTreeView>
{
protected:
	typedef CItemTreeView thisClass;
	typedef CWindowImpl<CItemTreeView, CTreeViewCtrlEx, CItemTreeViewWinTraits> baseClass;

public:
	CItemTreeView(CHexViewFrame* parentFrame);

protected:
	WTL::CImageList m_ImageList;
	//int nIconFolderIndexNormal, nIconFolderIndexSelected;
	//int nIconIndexNormal, nIconIndexSelected;
	
	//unordered_map<VGMItem*, CTreeItem> items;
	std::map<VGMItem*, CTreeItem> items;

public:
	DECLARE_WND_SUPERCLASS(NULL, CTreeViewCtrlEx::GetWndClassName())

	BOOL PreTranslateMessage(MSG* pMsg);

	BEGIN_MSG_MAP(thisClass)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MSG_WM_CONTEXTMENU(OnContextMenu)
		REFLECTED_NOTIFY_CODE_HANDLER(TVN_SELCHANGED, OnTvnSelchanged)
		REFLECTED_NOTIFY_CODE_HANDLER(NM_RCLICK, OnNMRClick)
		if(uMsg == WM_FORWARDMSG)
			if(PreTranslateMessage((LPMSG)lParam)) return TRUE;
		//CHAIN_MSG_MAP_ALT(CEditCommands<CPlainTextView>, 1)

		DEFAULT_REFLECTION_HANDLER()
	END_MSG_MAP()

	LRESULT OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled);
	LRESULT OnTvnSelchanged(int idCtrl, LPNMHDR pnmh, BOOL& bHandled);
	LRESULT OnNMRClick(int idCtrl, LPNMHDR pnmh, BOOL& bHandled);
	LRESULT OnContextMenu(HWND hwndCtrl, CPoint ptClick );

	void Init(int cx, int cy);
	//void ChangeCurVGMFile(VGMFile* file);
	inline void AddItem(VGMItem* item, VGMItem* parent, const std::wstring& itemName)
	{
		int iconIndex = item->GetIcon();
		if (parent != parItemCache)
		{
			parItemCache = parent;
			parentTreeItemCache = items[parent].m_hTreeItem;
		}
		CTreeItem treeItem = InsertItem(itemName.c_str(), iconIndex, iconIndex, parentTreeItemCache, NULL);
		treeItem.SetData((DWORD_PTR)item);

		items[item] = treeItem;
		//lItems.push_back(newItem);
	}
	//void PopulateWithItem(VGMItem* item, VGMItem* parent);
	void RemoveItem(VGMItem* theItem);
	void RemoveAllItems(void);
	BOOL SelectItem(VGMItem* item);
	
	static DWORD CALLBACK PopulateItemView(VOID* lpParam);
	void WaitForPopulateEnd(void);

// Helpers
protected:
	VGMFile* curFile;
	VGMItem* parItemCache;
	HTREEITEM parentTreeItemCache;
	CHexViewFrame* parFrame;


public:
	bool bPopulated;		//have we filled up the view with TreeItems yet?
	bool bPopulating;

};

/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_PlainTextView_H__053AD676_0AE2_11D6_8BF1_00500477589F__INCLUDED_)
