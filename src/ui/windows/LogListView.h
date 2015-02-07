// PlainTextView.h : interface of the CPlainTextView class
//
/////////////////////////////////////////////////////////////////////////////

#if !defined(AFX_LogListView_H__053AD676_0AE2_11D6_8BF1_00500477589F__INCLUDED_)
#define AFX_LogListView_H__053AD676_0AE2_11D6_8BF1_00500477589F__INCLUDED_

#pragma once

#include "LogItem.h"
#include "VGMTransWindow.h"

template <class T> class VGMTransWindow;

#define VIEW_STYLES \
	(LVS_REPORT | LVS_SHOWSELALWAYS |/*LVS_SINGLESEL |*/ /*LVS_NOCOLUMNHEADER |*/ LVS_SHOWSELALWAYS | \
	LVS_SHAREIMAGELISTS | LVS_AUTOARRANGE )
#define VIEW_EX_STYLES \
	(LVS_EX_FULLROWSELECT) //LVS_EX_AUTOSIZECOLUMNS//(WS_EX_CLIENTEDGE)

class CLogListView : public CWindowImpl<CLogListView, CListViewCtrl, CWinTraitsOR<VIEW_STYLES,VIEW_EX_STYLES> >,
						 public VGMTransWindow<CLogListView>
{
protected:
	typedef CLogListView thisClass;

public:
	CLogListView();

public:
	DECLARE_WND_SUPERCLASS(NULL, CListViewCtrl::GetWndClassName())

	BOOL PreTranslateMessage(MSG* pMsg);

	BEGIN_MSG_MAP(thisClass)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MSG_WM_SIZE(OnSize)
		MSG_WM_DESTROY(OnDestroy)
		REFLECTED_NOTIFY_CODE_HANDLER_EX(LVN_COLUMNCLICK, OnColumnClick)
		if(uMsg == WM_FORWARDMSG)
			if(PreTranslateMessage((LPMSG)lParam)) return TRUE;

		DEFAULT_REFLECTION_HANDLER()
	END_MSG_MAP()

	LRESULT OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
	void OnSize(UINT nType, CSize size);
	void OnDestroy(void);

	//Notification Handlers
	LRESULT OnColumnClick ( NMHDR* phdr );

	void Init();
	void Clear();
	void SetViewMode ( int nMode );
	void AddLogItem(LogItem* newLog);
	void RemoveLogItem(LogItem* theLog);

protected:	

	int m_nSortedCol;   // -1 if list hasn't been sorted yet
	bool m_bSortAscending;

	int nErrIcon;
	int nWarnIcon;
	int nInfoIcon;

	CImageList m_imlSmall, m_imlLarge, m_imlTiles, m_imlState;

	void InitImageLists();
	void InitColumns();

	static int CALLBACK SortCallback ( LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort );
	int SortCallback ( const LogItem& info1, const LogItem& info2 ) const;
};

extern CLogListView theLogListView;

#undef VIEW_STYLES
#undef VIEW_EX_STYLES

/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_LogListView_H__053AD676_0AE2_11D6_8BF1_00500477589F__INCLUDED_)
