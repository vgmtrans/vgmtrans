// TabbedDockingWindow.h: interface for the CTabbedDockingWindow class.
//
// NOTE: This class depends on Sergey Klimov's docking window framework
//  and "TabbedFrame.h"
//
//////////////////////////////////////////////////////////////////////

#ifndef __TABBED_DOCKING_WINDOW_H__
#define __TABBED_DOCKING_WINDOW_H__

#pragma once

#if !defined(__WTL_DW__EXTDOCKINGWINDOW_H__) && !defined(AFX_EXTDOCKINGWINDOW_H__0CD64AFC_8687_4B20_8B8F_EE149C8C0E94__INCLUDED_)
	#error TabbedDockingWindow.h requires ExtDockingWindow.h to be included first
#endif
#if !defined(__WTL_DW__DOCKMISC_H__) && !defined(AFX_DOCKMISC_H__2A1A3052_6F61_4F89_A2C4_AAAC46D67AF1__INCLUDED_)
	#error TabbedDockingWindow.h requires DockMisc.h to be included first
#endif
#ifndef __WTL_TABBED_FRAME_H__
	#error TabbedDockingWindow.h requires TabbedFrame.h to be included first
#endif

class CTabbedDockingWindow :
	public CTabbedFrameImpl<CTabbedDockingWindow, CDotNetTabCtrl<CTabViewTabItem>, dockwins::CTitleDockingWindowImpl< CTabbedDockingWindow,ATL::CWindow,dockwins::COutlookLikeTitleDockingWindowTraits> >
{
protected:
	typedef CTabbedDockingWindow thisClass;
	typedef CTabbedFrameImpl<CTabbedDockingWindow, CDotNetTabCtrl<CTabViewTabItem>, dockwins::CTitleDockingWindowImpl< CTabbedDockingWindow,ATL::CWindow,dockwins::COutlookLikeTitleDockingWindowTraits> > baseClass;

// Constructors
public:
	CTabbedDockingWindow(bool bReflectNotifications = true) :
		baseClass(bReflectNotifications)
	{
	}

// Message Handling
public:
	DECLARE_WND_CLASS_EX(_T("TabbedDockingWindow"), CS_DBLCLKS, COLOR_APPWORKSPACE)

	BOOL PreTranslateMessage(MSG* pMsg)
	{
		//if(baseClass::PreTranslateMessage(pMsg))
		//	return TRUE;

		//return m_view.PreTranslateMessage(pMsg);

		HWND hWndFocus = ::GetFocus();
		if(m_hWndActive != NULL && ::IsWindow(m_hWndActive) &&
			(m_hWndActive == hWndFocus || ::IsChild(m_hWndActive, hWndFocus)))
		{
			//active.PreTranslateMessage(pMsg);
			if(::SendMessage(m_hWndActive, WM_FORWARDMSG, 0, (LPARAM)pMsg))
			{
				return TRUE;
			}
		}

		return FALSE;
	}

	BEGIN_MSG_MAP(thisClass)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		CHAIN_MSG_MAP(baseClass)
	END_MSG_MAP()

	LRESULT OnSize(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
	{
		if(wParam != SIZE_MINIMIZED)
		{
			//T* pT = static_cast<T*>(this);
			//pT->UpdateLayout();
			UpdateLayout();
		}
		bHandled = FALSE;
		return 1;
	}

// Overrideables
public:
	void UpdateBarsPosition(RECT& /*rect*/, BOOL /*bResizeBars = TRUE*/)
	{
	}
};

#ifdef DF_AUTO_HIDE_FEATURES

class CTabbedAutoHideDockingWindow :
	public dockwins::CBoxedDockingWindowImpl< CTabbedAutoHideDockingWindow,ATL::CWindow,dockwins::CVC7LikeExBoxedDockingWindowTraits>
{
protected:
	typedef CTabbedAutoHideDockingWindow	thisClass;
	typedef dockwins::CBoxedDockingWindowImpl< CTabbedAutoHideDockingWindow,ATL::CWindow,dockwins::CVC7LikeExBoxedDockingWindowTraits> baseClass;

// Member variables
protected:
	HWND m_hWndClient;
	bool m_bReflectNotifications;
	bool m_bClientFlatOutline;
	int m_nMenuID;

// Constructors
public:
	CTabbedAutoHideDockingWindow(HWND hWndClient = NULL) : 
		m_hWndClient(hWndClient),
		m_bReflectNotifications(false),
		m_bClientFlatOutline(false),
		m_nMenuID(0)
	{
	}

// static public Methods:
public:
	static CTabbedAutoHideDockingWindow* CreateInstance(void)
	{
		return new CTabbedAutoHideDockingWindow;
	}

// Public Methods
public:
	bool AutoHide(bool bAutoHideOwnerTabBoxAsGroup = true)
	{
		bool returnValue = false;
		if(this->IsWindow() && this->IsDocking() && !this->IsPinned())
		{
			// NOTE: "IsPinned" really should be renamed to "IsAutoHidden" or something
			// TODO: The way IsPinned works seems backwards.
			//  Its true if the window is being auto-hidden.

			HWND hWndDockingBox = this->GetOwnerDockingBar();
			if(bAutoHideOwnerTabBoxAsGroup && dockwins::CDockingBox::IsWindowBox(hWndDockingBox))
			{
				// The pane window is docked, not auto-hidden already, and in a tab box.
				// Auto-hide the whole box at once (this will auto-hide all other
				// pane windows in this tab box as well).

				dockwins::DFPINBTNPRESS btnPress = {0};
				btnPress.hdr.hWnd = m_hWnd;
				btnPress.hdr.hBar = hWndDockingBox;
				btnPress.hdr.code = DC_PINBTNPRESS;
				btnPress.bVisualize = FALSE;
				returnValue = ::SendMessage(hWndDockingBox, WMDF_DOCK, 0, (WPARAM)&btnPress) ? true : false;

				//returnValue = ownerTabBox->PinBtnPress(false);
			}
			else //if(dockwins::CDockingBox::IsWindowBox(m_hWnd))
			{
				// The pane window is docked, not auto-hidden already and *not* in a tab box.
				// Auto-hide just the pane window.

				//dockwins::DFPINBTNPRESS btnPress = {0};
				//btnPress.hdr.hWnd = m_hWnd;
				//btnPress.hdr.hBar = hWndDockingBox;
				//btnPress.hdr.code = DC_PINBTNPRESS;
				//btnPress.bVisualize = FALSE;
				//::SendMessage(m_hWnd, WMDF_DOCK, 0, (WPARAM)&btnPress);
				returnValue = this->PinBtnPress(false);
			}
		}
		return returnValue;
	}

	void SetClient(HWND hWndClient)
	{
		m_hWndClient = hWndClient;

		if(	m_hWndClient && ::IsWindow(m_hWndClient) &&
			m_hWnd && ::IsWindow(m_hWnd))
		{
			// Set our small icon to the small icon of the client
			HICON hIcon = (HICON)::SendMessage(m_hWndClient, WM_GETICON, ICON_SMALL, 0L);
			if(hIcon==NULL)
			{
// need conditional code because types don't match in winuser.h
#ifdef _WIN64
				hIcon = (HICON)::GetClassLongPtr(m_hWndClient, GCLP_HICONSM);
#else
				hIcon = (HICON)LongToHandle(::GetClassLongPtr(m_hWndClient, GCLP_HICONSM));
#endif
			}
			if(hIcon)
			{
				this->SetIcon(hIcon, ICON_SMALL);
			}

			if(m_bClientFlatOutline)
			{
				DWORD dwExStyle = (DWORD)::GetWindowLong(m_hWndClient, GWL_EXSTYLE);
				dwExStyle &= ~(WS_EX_CLIENTEDGE);
				::SetWindowLong(m_hWndClient, GWL_EXSTYLE, dwExStyle);
			}

			// Resize the client to fill our client area
			RECT rect;
			this->GetClientRect(&rect);

			if(m_bClientFlatOutline)
			{
				::SetWindowPos(m_hWndClient, NULL, rect.left+1, rect.top+1,
					rect.right - rect.left-2, rect.bottom - rect.top-2,
					SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
			}
			else
			{
				::SetWindowPos(m_hWndClient, NULL, rect.left, rect.top,
					rect.right - rect.left, rect.bottom - rect.top,
					SWP_NOZORDER | SWP_NOACTIVATE);
			}
		}
	}

	HWND GetClient(void)
	{
		return m_hWndClient;
	}

	void SetReflectNotifications(bool bReflectNotifications = true)
	{
		m_bReflectNotifications = bReflectNotifications;
	}

	bool GetReflectNotifications(void) const
	{
		return m_bReflectNotifications;
	}

	void SetClientFlatOutline(bool bFlat = true)
	{
		if(m_bClientFlatOutline!=bFlat)
		{
			ATLASSERT((m_hWndClient==NULL) && "Please call SetClientFlatOutline before setting client");
			m_bClientFlatOutline = bFlat;
		}
	}

	bool GetClientFlatOutline(void) const
	{
		return m_bClientFlatOutline;
	}

	void SetMenuID(int nMenuID)
	{
		m_nMenuID = nMenuID;
	}

	int GetMenuID(void) const
	{
		return m_nMenuID;
	}

	BOOL IsOwnerDockBarVisible()
	{
		HWND hWnd = this->GetOwnerDockingBar();
		return hWnd && ::IsWindowVisible(hWnd);
	}

	BOOL IsCurrentDockBarVisible()
	{
		HWND hWnd = m_pos.hdr.hBar;
		return hWnd && ::IsWindowVisible(hWnd);
	}

	bool GetCurrentDockingPosition(dockwins::DFDOCKPOS* dockPos)
	{
		if(dockPos == NULL)
		{
			return false;
		}

		::CopyMemory(dockPos, &m_pos, sizeof(m_pos));
		return true;
	}

	bool SetCurrentDockingPosition(dockwins::DFDOCKPOS* dockPos)
	{
		if(dockPos == NULL)
		{
			return false;
		}

		::CopyMemory(&m_pos, dockPos, sizeof(m_pos));
		return true;
	}

// Message Handling
public:
	DECLARE_WND_CLASS_EX(_T("TabbedAutoHideDockingWindow"), CS_DBLCLKS, COLOR_APPWORKSPACE)

	virtual void OnFinalMessage(HWND /*hWnd*/)
	{
		// NOTE: This class is meant to be created with "new"
		delete this;
	}

	BOOL PreTranslateMessage(MSG* pMsg)
	{
		//if(baseClass::PreTranslateMessage(pMsg))
		//	return TRUE;

		//return m_view.PreTranslateMessage(pMsg);

		HWND hWndFocus = ::GetFocus();
		if(m_hWndClient != NULL && ::IsWindow(m_hWndClient) &&
			(m_hWndClient == hWndFocus || ::IsChild(m_hWndClient, hWndFocus)))
		{
			//active.PreTranslateMessage(pMsg);
			if(::SendMessage(m_hWndClient, WM_FORWARDMSG, 0, (LPARAM)pMsg))
			{
				return TRUE;
			}
		}

		return FALSE;
	}

	BEGIN_MSG_MAP(thisClass)	
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_CLOSE, OnClose)
		//MESSAGE_HANDLER(WM_PARENTNOTIFY, OnParentNotify)
		MESSAGE_HANDLER(WM_SIZE, OnSize)		
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBackground)
		MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)

		CHAIN_MSG_MAP(baseClass)
		if(m_bReflectNotifications)
		{
			REFLECT_NOTIFICATIONS()
		}
	END_MSG_MAP()

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
	{
		bHandled = FALSE;
		return 0;
	}

	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
	{
		bHandled = FALSE;
		return 0;
	}

	LRESULT OnClose(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
	{
		// IMPORTANT!
		// The docking window framework deals with WM_CLOSE differently
		// than you might expect.  To the framework, WM_CLOSE essentially
		// means "Hide".  So if you send WM_CLOSE to this window, don't
		// expect it to destruct.  Instead, you should send WM_CLOSE to the window
		// first, then DestroyWindow if that's what you're wanting to do.
		bHandled = FALSE;

		// What we might do if WM_CLOSE didn't mean "hide"
		/*
		if(m_hWndClient != NULL)
		{
			if(::IsWindow(m_hWndClient))
			{
				LRESULT lResult = ::SendMessage(m_hWndClient, WM_CLOSE, 0, 0L);
				if(lResult)
				{
					// If the client doesn't want to close,
					// don't let DefWindowProc have at WM_CLOSE,
					// and return the response from the client
					bHandled = TRUE;
					return lResult;
				}
				// else, let DefWindowProc happen,
				// and let go of m_hWndClient
				m_hWndClient = NULL;
			}
		}
		*/
		return 0;
	}

	// If we ever need it:
	//LRESULT OnParentNotify(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
	//{
	//	bHandled = FALSE;
	//	if(LOWORD(wParam) == WM_DESTROY)
	//	{
	//		m_hWndClient = NULL;
	//	}
	//	return 0;
	//}

	LRESULT OnSize(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
	{
		if(wParam != SIZE_MINIMIZED )
		{
			// resize client window
			if(m_hWndClient != NULL)
			{
				RECT rect;
				this->GetClientRect(&rect);

				if(m_bClientFlatOutline)
				{
					::SetWindowPos(m_hWndClient, NULL, rect.left+1, rect.top+1,
						rect.right - rect.left-2, rect.bottom - rect.top-2,
						SWP_NOZORDER | SWP_NOACTIVATE);
				}
				else
				{
					::SetWindowPos(m_hWndClient, NULL, rect.left, rect.top,
						rect.right - rect.left, rect.bottom - rect.top,
						SWP_NOZORDER | SWP_NOACTIVATE);
				}
			}
		}
		bHandled = FALSE;
		return 1;
	}

	LRESULT OnEraseBackground(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
	{
		if(m_hWndClient != NULL)
		{
			if(m_bClientFlatOutline)
			{
				// Paint a flat outline
				HDC hdc = (HDC)wParam;
				if(hdc != NULL)
				{
					RECT rcClient={0};
					this->GetClientRect(&rcClient);
					::FrameRect(hdc,&rcClient,::GetSysColorBrush(COLOR_BTNSHADOW));
				}
			}

			// view will paint itself
			return 1;
		}

		// Else no client view is set, so let the default erase happen
		// (which will use the brush of the window class)
		bHandled = FALSE;
		return 0;

		//HDC hdc = (HDC)wParam;
		//if(hdc != NULL)
		//{
		//	RECT rect;
		//	::GetClipBox(hdc, &rect);
		//	::SetBkColor(hdc, ::GetSysColor(COLOR_APPWORKSPACE));
		//	::ExtTextOut(hdc, 0, 0, ETO_OPAQUE, &rect, NULL, 0, NULL);
		//}
		//return 1;
	}

	LRESULT OnSetFocus(UINT, WPARAM, LPARAM, BOOL& bHandled)
	{
		if(m_hWndClient != NULL && ::IsWindowVisible(m_hWndClient))
			::SetFocus(m_hWndClient);

		bHandled = FALSE;
		return 1;
	}

// "Overridden" from base class
public:
	void OnDocked(HDOCKBAR hBar,bool bHorizontal)
	{
		DWORD dwStyle = GetWindowLong(GWL_STYLE)&(~WS_SIZEBOX);		
		SetWindowLong( GWL_STYLE, dwStyle);

		baseClass::OnDocked(hBar,bHorizontal);
	}
	void OnUndocked(HDOCKBAR hBar)
	{
		DWORD dwStyle = GetWindowLong(GWL_STYLE) | WS_SIZEBOX;
		SetWindowLong( GWL_STYLE , dwStyle);
		
		baseClass::OnUndocked(hBar);
	}
	virtual void GetMinMaxInfo(LPMINMAXINFO pMinMaxInfo) const
	{
		pMinMaxInfo->ptMinTrackSize.y = 100;
		pMinMaxInfo->ptMinTrackSize.x = 100;
	}
};

#endif //DF_AUTO_HIDE_FEATURES

#endif
