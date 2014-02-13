// PlainTextView.h : interface of the CPlainTextView class
//
/////////////////////////////////////////////////////////////////////////////

#if !defined(AFX_VGMCollListView_H__053AD676_0AE2_11D6_8BF1_00500477589F__INCLUDED_)
#define AFX_VGMCollListView_H__053AD676_0AE2_11D6_8BF1_00500477589F__INCLUDED_

#pragma once

#include "VGMColl.h"
#include "VGMTransWindow.h"

template <class T> class VGMTransWindow;

#define VIEW_STYLES \
	(LVS_LIST /*LVS_REPORT*/ | LVS_SHOWSELALWAYS |/*LVS_SINGLESEL |*/ LVS_NOCOLUMNHEADER | \
   LVS_SHAREIMAGELISTS | LVS_AUTOARRANGE )
#define VIEW_EX_STYLES 0//(WS_EX_CLIENTEDGE)

//typedef CWinTraits<WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_HSCROLL | WS_VSCROLL | WS_EX_CLIENTEDGE | TVS_HASBUTTONS> CVGMFileListViewWinTraits;

class CVGMCollListView : public CWindowImpl<CVGMCollListView, CListViewCtrl, CWinTraitsOR<VIEW_STYLES,VIEW_EX_STYLES> >,//CVGMFileListViewWinTraits>,
						 public VGMTransWindow<CVGMCollListView>
					//public CEditCommands<CMyTreeView>
{
protected:
	typedef CVGMCollListView thisClass;

public:
	CVGMCollListView();

public:
	DECLARE_WND_SUPERCLASS(NULL, CListViewCtrl::GetWndClassName())

	BOOL PreTranslateMessage(MSG* pMsg);

	BEGIN_MSG_MAP(thisClass)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MSG_WM_DESTROY(OnDestroy)
		MSG_WM_CHAR(OnChar)
		MSG_WM_CONTEXTMENU(OnContextMenu)
		//MSG_WM_LBUTTONDBLCLK(OnLButtonDblClk)
		//REFLECTED_NOTIFY_CODE_HANDLER_EX(LVN_COLUMNCLICK, OnColumnClick)
		//REFLECTED_NOTIFY_CODE_HANDLER(LVN_ITEMACTIVATE, OnLvnItemActivate)
		REFLECTED_NOTIFY_CODE_HANDLER(LVN_ITEMCHANGED , OnLvnItemChanged) 
		//REFLECTED_NOTIFY_CODE_HANDLER(NM_RCLICK, OnNMRClick)
		//REFLECTED_NOTIFY_CODE_HANDLER_EX(LVN_BEGINDRAG, OnListBeginDrag)
		if(uMsg == WM_FORWARDMSG)
			if(PreTranslateMessage((LPMSG)lParam)) return TRUE;
		//CHAIN_MSG_MAP_ALT(CEditCommands<CPlainTextView>, 1)

		DEFAULT_REFLECTION_HANDLER()
	END_MSG_MAP()

	LRESULT OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
	void OnDestroy(void);
	void OnChar(UINT nChar, UINT nRepCnt, UINT nFlags);
	LRESULT OnContextMenu(HWND hwndCtrl, CPoint ptClick );
	//void OnLButtonDblClk(UINT nFlags, CPoint point);

	//Notification Handlers
	//LRESULT OnColumnClick ( NMHDR* phdr );
	//LRESULT OnLvnItemActivate(int idCtrl, LPNMHDR pnmh, BOOL& bHandled);
	LRESULT OnLvnItemChanged(int idCtrl, LPNMHDR pnmh, BOOL& bHandled);
	LRESULT OnNMRClick(int idCtrl, LPNMHDR pnmh, BOOL& bHandled);
	//LRESULT OnListBeginDrag ( NMHDR* phdr );
	
	
	
	//void Init(int cx, int cy);
	void Init();
	void Clear();
	void SetViewMode ( int nMode );
	void AddColl(VGMColl* newColl);
	void RemoveColl(VGMColl* theColl);
	BOOL SelectColl(VGMColl* coll);
	//bool GetDraggedFileInfo ( std::vector<CDraggedFileInfo>& vec );

	//BOOL SelectItem(VGMItem* item);

protected:	

	int m_nSortedCol;   // -1 if list hasn't been sorted yet
    bool m_bSortAscending;

	//WTL::CImageList m_ImageList;
	CImageList m_imlSmall, m_imlLarge, m_imlTiles, m_imlState;
    CComPtr<IImageList> m_TileIml;
	//int nIconFolderIndexNormal, nIconFolderIndexSelected;
	//int nIconIndexNormal, nIconIndexSelected;
	//hash_map<VGMFile*, int> items;
	//VGMColl* selectedColl;

	void InitImageLists();
    void InitColumns();

	//static int CALLBACK SortCallback ( LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort );
    //int SortCallback ( const VGMFile& info1, const VGMFile& info2 ) const;

// Helpers

};

extern CVGMCollListView theVGMCollListView;

#undef VIEW_STYLES
#undef VIEW_EX_STYLES

/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_PlainTextView_H__053AD676_0AE2_11D6_8BF1_00500477589F__INCLUDED_)


/*
// WTLCabViewView.h : interface of the CWTLCabViewView class
/////////////////////////////////////////////////////////////////////////////

#if !defined(AFX_WTLCABVIEWVIEW_H__9300C31D_E4FF_4E4B_A98D_670F63106D35__INCLUDED_)
#define AFX_WTLCABVIEWVIEW_H__9300C31D_E4FF_4E4B_A98D_670F63106D35__INCLUDED_

#if _MSC_VER >= 1000
#pragma once
#endif // _MSC_VER >= 1000

#define VIEW_STYLES (LVS_REPORT|LVS_SHOWSELALWAYS|LVS_SHAREIMAGELISTS|LVS_AUTOARRANGE)
#define VIEW_EX_STYLES (WS_EX_CLIENTEDGE)

class CWTLCabViewView : public CWindowImpl<CWTLCabViewView, CListViewCtrl,
                                             CWinTraitsOR<VIEW_STYLES,VIEW_EX_STYLES> >
{
public:
    DECLARE_WND_SUPERCLASS(NULL, CListViewCtrl::GetWndClassName())

    // Construction
    CWTLCabViewView();

    // Maps
    BEGIN_MSG_MAP(CWTLCabViewView)
        MSG_WM_DESTROY(OnDestroy)
        REFLECTED_NOTIFY_CODE_HANDLER_EX(LVN_COLUMNCLICK, OnColumnClick)
        REFLECTED_NOTIFY_CODE_HANDLER_EX(LVN_MARQUEEBEGIN, OnMarqueeBegin)
        REFLECTED_NOTIFY_CODE_HANDLER_EX(LVN_KEYDOWN, OnKeyDown)
        REFLECTED_NOTIFY_CODE_HANDLER_EX(LVN_DELETEITEM, OnDeleteItem)
        DEFAULT_REFLECTION_HANDLER()
    END_MSG_MAP()

    // Message handlers
    BOOL PreTranslateMessage ( MSG* pMsg );

    void OnDestroy();

    // Notify handlers
    LRESULT OnColumnClick ( NMHDR* phdr );
    LRESULT OnMarqueeBegin ( NMHDR* phdr );
    LRESULT OnKeyDown ( NMHDR* phdr );
    LRESULT OnDeleteItem ( NMHDR* phdr );

    // Operations
    void Init();
    void Clear();
    void SetViewMode ( int nMode );
    void AddFile ( LPCTSTR szFilename, long cbyUncompressedSize, USHORT uDate,
                   USHORT uTime, USHORT uAttribs );
    void AddPartialFile ( LPCTSTR szFilename, LPCTSTR szStartingCabName,
                          LPCTSTR szStartingDiskName );
    void UpdateContinuedFile ( const CDraggedFileInfo& info );
    bool GetDraggedFileInfo ( std::vector<CDraggedFileInfo>& vec );

protected:
    CImageList m_imlSmall, m_imlLarge, m_imlTiles, m_imlState;
    CComPtr<IImageList> m_TileIml;

    int m_nSortedCol;   // -1 if list hasn't been sorted yet
    bool m_bSortAscending;

    void InitImageLists();
    void InitColumns();

    static int CALLBACK SortCallback ( LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort );
    int SortCallback ( const CCompressedFileInfo& info1, const CCompressedFileInfo& info2 ) const;
};


/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_WTLCABVIEWVIEW_H__9300C31D_E4FF_4E4B_A98D_670F63106D35__INCLUDED_)
*/