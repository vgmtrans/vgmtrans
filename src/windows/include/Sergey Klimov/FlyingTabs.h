// Copyright (c) 2002
// Sergey Klimov (kidd@ukr.net)

#ifndef __WTL_DW__FLYING_TABS_H__
#define __WTL_DW__FLYING_TABS_H__

#pragma once

#include <cassert>
#include <vector>
#include <queue>
#include <algorithm>
#include <functional>

#include "DDTracker.h"

//#include <atlgdix.h>
#ifndef __ATLGDIX_H__
	#error FlyingTabs.h requires atlgdix.h to be included first
#endif

// NOTE: See CustomTabCtrl.h and DotNetTabCtrl.h for copyright information.
// Please download the latest versions of these files from from The Codeproject article
// "Custom Tab Controls, Tabbed Frame and Tabbed MDI" by Daniel Bowen (dbowen@es.com)
// http://www.codeproject.com/wtl/TabbingFramework.asp
//#include <CustomTabCtrl.h>
//#include <DotNetTabCtrl.h>
#ifndef __CUSTOMTABCTRL_H__
	#error FlyingTabs.h requires CustomTabCtrl.h to be included first
#endif
#ifndef __DOTNET_TABCTRL_H__
	#error FlyingTabs.h requires DotNetTabCtrl.h to be included first
#endif

namespace dockwins{

#define CTCN_TABLEAVCTRL CTCN_LAST-1

#ifndef CTCS_VERTICAL
#define CTCS_VERTICAL TCS_VERTICAL
#endif

class CFlyingTabCtrl :
	public CDotNetTabCtrlImpl<CFlyingTabCtrl, CTabViewTabItem>
{
	typedef CDotNetTabCtrlImpl<CFlyingTabCtrl, CTabViewTabItem>	baseClass;
	typedef CFlyingTabCtrl										thisClass;
protected:
	class CTabMoveTracker : public CDDTrackerBaseT<CTabMoveTracker>
	{
		typedef thisClass CTabCtrl;
	public:
		CTabMoveTracker(CTabCtrl& ctrlTab,int index)
			:m_ctrlTab(ctrlTab),m_curItem(index),m_prevItem(-1)
		{
			DWORD style = ctrlTab.GetWindowLong(GWL_STYLE);
			m_bHorizontal=(style&CTCS_VERTICAL)==0;
		}
		bool IsHorisontal() const
		{
			return m_bHorizontal;
		}
		void OnMove(long x, long y)
		{
			int pos = IsHorisontal() ? x : y;
			CTCHITTESTINFO tchti = { 0 };
			tchti.pt.x = x;
			tchti.pt.y = y;
			int index=m_ctrlTab.HitTest(&tchti);
			if(index!=-1)
			{
				if(( index!=m_curItem)
					&&
					 !( ( index==m_prevItem)
						&& ( (pos-m_prevPos)*(m_prevItem-m_curItem) <=0) ) )
				{
					m_ctrlTab.SwapItemPositions(m_curItem,index, false, false);
//					m_ctrlTab.MoveItem(m_curItem,index);
					m_curItem=index;
					m_ctrlTab.SetCurSel(m_curItem);
					m_prevItem=m_ctrlTab.HitTest(&tchti);
				}
			}
			else
				::ReleaseCapture();
/*
			{
				NMHDR nmh;
				nmh.hwndFrom = m_ctrlTab.m_hWnd;
				nmh.idFrom=m_ctrlTab.GetDlgCtrlID();
				nmh.code=CTCN_TABLEAVCTRL;
				if(!::SendMessage(m_ctrlTab.GetParent(), WM_NOTIFY, nmh.idFrom, (LPARAM)&nmh))
																				::ReleaseCapture();
			}
*/
			m_prevPos = pos;
		}
	protected:
		CTabCtrl&	m_ctrlTab;
		int			m_curItem;
		int			m_prevItem;
		int			m_prevPos;
		bool		m_bHorizontal;
	};
protected:
    void UpdateTabAreaHeight()
    {
        // Take into consideration system metrics when determining
        //  the height of the tab area
        const int nNominalHeight = 24;
        const int nNominalFontLogicalUnits = 11;        // 8 point Tahoma with 96 DPI

        // Use the actual font of the tab control
        assert(IsWindow());
        HFONT hFont = GetFont();
        if(hFont != NULL)
        {
            CClientDC dc(this->m_hWnd);
            CFontHandle hFontOld = dc.SelectFont(hFont);
            TEXTMETRIC tm = {0};
            dc.GetTextMetrics(&tm);
            dc.SelectFont(hFontOld);
			m_tabBarHeight = (BYTE)(nNominalHeight + (nNominalHeight * ((double)tm.tmAscent / (double)nNominalFontLogicalUnits) - nNominalHeight) / 2);
        }
		else
			m_tabBarHeight = 24;
    }
	void AdjustTabRect(LPRECT pRc) const
	{
		int tabHeight = m_tabBarHeight ;
		DWORD style = GetWindowLong(GWL_STYLE);
		if(style&CTCS_VERTICAL)
		{
			if(style&CTCS_BOTTOM)
				pRc->left=pRc->right - tabHeight;
			else
				pRc->right=pRc->left + tabHeight;
		}
		else
		{
			if(style&CTCS_BOTTOM)
				pRc->top=pRc->bottom - tabHeight;
			else
				pRc->bottom=pRc->top + tabHeight;
		}
	}
public:
	bool GetTabsRect(LPRECT pRc) const
	{
		return (GetClientRect(pRc)!=FALSE);
	}
/*
    int InsertItem(int nItem, LPCTSTR sText = NULL, int nImage = -1, HWND hWndTabView = NULL, LPCTSTR sToolTip = NULL, bool bSelectItem = false)
    {
		return baseClass::InsertItem(nItem, sText, nImage, hWndTabView, sToolTip, bSelectItem);
	}
	int InsertItem(int nItem, CTabViewTabItem* pItem, bool bSelectItem = false)
	{
		return baseClass::InsertItem(nItem, pItem, bSelectItem);
	}
*/
	int InsertItem(int index, LPCTSTR ptxt = NULL, int image = -1, DWORD_PTR param=0)
	{
		CTabViewTabItem* pItem = baseClass::CreateNewItem();
		if(pItem)
		{
			pItem->SetText(ptxt);
			pItem->SetImageIndex(image);
			pItem->SetTabView(reinterpret_cast<HWND>(param));
			pItem->SetToolTip(ptxt);

			return baseClass::InsertItem(index,pItem,true);
		}
		return -1;
	}
	DWORD_PTR GetItemData(int index) const
	{
		return reinterpret_cast<DWORD_PTR>(GetItem(index)->GetTabView());
	}

	void DeleteItem(CTabViewTabItem* pItem)
	{
		baseClass::DeleteItem(pItem);
	}
	BOOL DeleteItem(size_t nItem, bool bUpdateSelection = true, bool bNotify = true)
	{
		return baseClass::DeleteItem(nItem, bUpdateSelection, bNotify);
	}

	BOOL DeleteItem(int index,bool bUpdateSelection = true)
	{
		return DeleteItem(index, bUpdateSelection , false);
	}
    BOOL SetWindowPos(HWND hWndInsertAfter, LPCRECT lpRect, UINT nFlags)
    {
		CRect rc(lpRect);
		AdjustTabRect(rc);
		BOOL bRes=baseClass::SetWindowPos(hWndInsertAfter,&rc,nFlags);
		return bRes;
	}

	BOOL AdjustRect( BOOL bLarger, LPRECT pRc) const
	{
		int tabHeight = bLarger ? -m_tabBarHeight : m_tabBarHeight ;
		DWORD style = GetWindowLong(GWL_STYLE);
		if(style&CTCS_VERTICAL)
		{
			if(style&CTCS_BOTTOM)
				pRc->right-=tabHeight;
			else
				pRc->left+=tabHeight;
		}
		else
		{
			if(style&CTCS_BOTTOM)
				pRc->bottom-=tabHeight;
			else
				pRc->top+=tabHeight;

		}
		return TRUE;
	}

	DECLARE_WND_CLASS(_T("CFlyingTabCtrl"))
	BEGIN_MSG_MAP(thisClass)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_SETFONT, OnSetFont)
		MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)
		MESSAGE_HANDLER(WM_LBUTTONDBLCLK, OnLButtonDblClk)
		CHAIN_MSG_MAP(baseClass)
	END_MSG_MAP()

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
	{
		UpdateTabAreaHeight();
		bHandled=FALSE;
		return NULL;
	}

	LRESULT OnSetFont(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
	{
		UpdateTabAreaHeight();
		bHandled=FALSE;
		return NULL;
	}

	LRESULT OnLButtonDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	{
		LRESULT lRes=baseClass::OnLButtonDown(uMsg,wParam,lParam,bHandled);
		bHandled=TRUE;
		CTCHITTESTINFO tchti = { 0 };
		tchti.pt.x = GET_X_LPARAM(lParam);
		tchti.pt.y = GET_Y_LPARAM(lParam);
		int index=HitTest(&tchti);
		if(index!=-1)
		{
			ClientToScreen(&tchti.pt);
			if(DragDetect(m_hWnd,tchti.pt))
			{
				CTabMoveTracker tracker(*this,index);
				if(TrackDragAndDrop(tracker,m_hWnd))
				{
					CTCHITTESTINFO tchti = { 0 };
					::GetCursorPos(&tchti.pt);
					ScreenToClient(&tchti.pt);
					int index=HitTest(&tchti);
					if(index==-1)
					{
						MSG msg;
						while(PeekMessage(&msg, NULL, NULL, NULL, PM_REMOVE))
							DispatchMessage(&msg);
						NMHDR nmh;
						nmh.hwndFrom = m_hWnd;
						nmh.idFrom=GetDlgCtrlID();
						nmh.code=CTCN_TABLEAVCTRL;
						::SendMessage(GetParent(), WM_NOTIFY, nmh.idFrom, (LPARAM)&nmh);
					}
				}
			}
		}
		return lRes;
	}

	LRESULT OnLButtonDblClk(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/)
	{
		CTCHITTESTINFO tchti = { 0 };
		tchti.pt.x = GET_X_LPARAM(lParam);
		tchti.pt.y = GET_Y_LPARAM(lParam);
		int index=HitTest(&tchti);
		if(index!=-1)
		{
			NMHDR nmh;
			nmh.hwndFrom = m_hWnd;
			nmh.idFrom=GetDlgCtrlID();
			nmh.code=NM_DBLCLK;
			::SendMessage(GetParent(), WM_NOTIFY, nmh.idFrom, (LPARAM)&nmh);
		}
		return 0;
	}
protected:
	int m_tabBarHeight;
};
}//namespace dockwins
#endif // __WTL_DW__FLYING_TABS_H__
