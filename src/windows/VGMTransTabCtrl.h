#pragma once

#include "DotNetTabCtrl.h"

/*template<typename T, typename TItem = CCustomTabItem, class TBase = ATL::CWindow, class TWinTraits = CCustomTabCtrlWinTraits>
class VGMTransTabCtrlImpl : 
	public CDotNetTabCtrlImpl<T, TItem, TBase, TWinTraits>
{
protected:
	typedef VGMFileTabCtrlImpl<T, TItem, TBase, TWinTraits> thisClass;
	typedef CDotNetTabCtrlImpl<T, TItem, TBase, TWinTraits> dotNetTabClass;

// Constructor
public: 
	VGMTransTabCtrl()
	{
	}

// Message Handling
public:
	DECLARE_WND_CLASS_EX(_T("WTL_VGMTransTabCtrl"), CS_DBLCLKS, COLOR_WINDOW)

	BEGIN_MSG_MAP(thisClass)
	//	REFLECTED_NOTIFY_CODE_HANDLER(CTCN_SELCHANGE, OnSelChange)
		CHAIN_MSG_MAP(dotNetTabClass)
		DEFAULT_REFLECTION_HANDLER()
	END_MSG_MAP()

	/*LRESULT OnSelChange(int idCtrl, LPNMHDR pnmh, BOOL& bHandled)
	{
		int i = 0;
		i++;
		return 0;
	}
}*/
/*
template <class TItem = CCustomTabItem>
class VGMTransTabCtrl :
	public CDotNetTabCtrlImpl<CDotNetTabCtrl<TItem>, TItem>
{
protected:
	typedef VGMTransTabCtrl thisClass;
	typedef CDotNetTabCtrlImpl<CDotNetTabCtrl<TItem>, TItem> baseClass;

// Constructors:
public:
	VGMTransTabCtrl()
	{
	}

public:

	DECLARE_WND_CLASS_EX(_T("WTL_VGMTransTabCtrl"), CS_DBLCLKS, COLOR_WINDOW)

	BEGIN_MSG_MAP(thisClass)
		//MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
		//REFLECTED_NOTIFY_CODE_HANDLER(CTCN_SELCHANGE, OnSelChange)
		//DEFAULT_REFLECTION_HANDLER()
		CHAIN_MSG_MAP(baseClass)
	END_MSG_MAP()
/*
	LRESULT OnSelChange(int idCtrl, LPNMHDR pnmh, BOOL& bHandled)
	{
		int i = 0;
		i++;
		return 0;
	}
};*/

template< class TTabCtrl >
class CVGMTransMDITabOwner :
	public CMDITabOwnerImpl<CMDITabOwner<TTabCtrl>, TTabCtrl>
{
protected:
	typedef CVGMTransMDITabOwner thisClass;
	typedef CMDITabOwnerImpl<CMDITabOwner<TTabCtrl>, TTabCtrl> baseClass;

	BEGIN_MSG_MAP(thisClass)
		NOTIFY_CODE_HANDLER(CTCN_SELCHANGE, OnSelChange)
		CHAIN_MSG_MAP(baseClass)
	END_MSG_MAP()

	LRESULT OnSelChange(int /*idCtrl*/, LPNMHDR pnmh, BOOL& bHandled)
	{
		// Be sure the notification is from the tab control
		// (and not from a sibling like a list view control)
		if(pnmh && (m_TabCtrl == pnmh->hwndFrom))
		{
			int nNewTab = m_TabCtrl.GetCurSel();

			if(nNewTab >= 0)
			{
				TTabCtrl::TItem* pItem = m_TabCtrl.GetItem(nNewTab);
				if(pItem->UsingTabView())
				{
					HWND hWndNew = pItem->GetTabView();
					HWND hWndOld = (HWND)::SendMessage(m_hWndMDIClient, WM_MDIGETACTIVE, 0, NULL);
					if( hWndNew != hWndOld )
					{
						// We don't want any flickering when switching the active child
						//  when the child is maximized (when its not maximized, there's no flicker).
						//  There's probably more than one way to do this, but how we do
						//  it is to turn off redrawing for the MDI client window,
						//  activate the new child window, turn redrawing back on for
						//  the MDI client window, and force a redraw (not just a simple
						//  InvalidateRect, but an actual RedrawWindow to include
						//  all the child windows ).
						//  It might be nice to just turn off/on drawing for the window(s)
						//  that need it, but if you watch the messages in Spy++,
						//  the default implementation of the MDI client is forcing drawing
						//  to be on for the child windows involved.  Turning drawing off
						//  for the MDI client window itself seems to solve this problem.
						//

						LRESULT nResult = 0;

						WINDOWPLACEMENT wpOld = {0};
						wpOld.length = sizeof(WINDOWPLACEMENT);
						::GetWindowPlacement(hWndOld, &wpOld);
						if(wpOld.showCmd == SW_SHOWMAXIMIZED)
						{
							nResult = ::SendMessage(m_hWndMDIClient, WM_SETREDRAW, FALSE, 0);
						}

						nResult = ::SendMessage(m_hWndMDIClient, WM_MDIACTIVATE, (LPARAM)hWndNew, 0);

						WINDOWPLACEMENT wpNew = {0};
						wpNew.length = sizeof(WINDOWPLACEMENT);
						::GetWindowPlacement(hWndNew, &wpNew);
						if(wpNew.showCmd == SW_SHOWMINIMIZED)
						{
							::ShowWindow(hWndNew, SW_RESTORE);
						}

						if(wpOld.showCmd == SW_SHOWMAXIMIZED)
						{
							nResult = ::SendMessage(m_hWndMDIClient, WM_SETREDRAW, TRUE, 0);
							::RedrawWindow(m_hWndMDIClient, NULL, NULL,
								//A little more forceful if necessary: RDW_ERASE | RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
								RDW_INVALIDATE | RDW_ALLCHILDREN);
						}
					}
				}
			}
		}

		bHandled = FALSE;
		return 0;
	}
};