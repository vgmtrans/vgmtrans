/////////////////////////////////////////////////////////////////////////////
// TabbedMDI.h - Classes that help implement a "Tabbed MDI" interface.
//
// Classes:
//   CTabbedMDIFrameWindowImpl - 
//      Instead of having CMainFrame inherit from
//      CMDIFrameWindowImpl, you can have it inherit from
//      CTabbedMDIFrameWindowImpl. For an out-of-the box WTL MDI
//      application, there are 3 instances of CMDIFrameWindowImpl
//      to replace with CTabbedMDIFrameWindowImpl.
//   CTabbedMDIChildWindowImpl - 
//      If you want your MDI child window to have a corresponding
//      tab in the MDI tab window, inherit from this class instead
//      of from CMDIChildWindowImpl.
//   CTabbedMDIClient - 
//      The CTabbedMDIFrameWindowImpl contains CTabbedMDIClient,
//      which subclasses the "MDI Client" window
//      (from the OS, that manages the MDI child windows).
//      It handles sizing/positioning the tab window,
//      calling the appropriate Display, Remove, UpdateText
//      for the tabs with the HWND of the active child,
//      etc.  You can use CTabbedMDIClient without using
//      CTabbedMDIFrameWindowImpl. To do so, simply call
//      SetTabOwnerParent(m_hWnd) then SubclassWindow(m_hWndMDIClient)
//      on a CTabbedMDIClient member variable after calling
//      CreateMDIClient in your main frame class.
//   CMDITabOwner -
//      The MDITabOwner is the parent of the actual tab window
//      (such as CDotNetTabCtrl), and sibling to the "MDI Client" window.
//      The tab owner tells the MDI child when to display a context
//      menu for the tab (the default menu is the window's system menu).
//      The tab owner changes the active MDI child when the
//      active tab changes.  It also does the real work of
//      hiding and showing the tabs.  It also handles adding,
//      removing, and renaming tabs based on an HWND.
//   CTabbedMDICommandBarCtrl/CTabbedMDICommandBarCtrlImpl -
//      In your MDI application, instead of using CMDICommandBarCtrl,
//      use CTabbedMDICommandBarCtrl.  It addresses a couple of bugs
//      in WTL 7.0's CMDICommandBarCtrl, and allows you to enable
//      or disable whether you want to see the document icon
//      and min/max/close button in the command bar when the
//      child is maximized.  To add additional functionality,
//      derive your own class from CTabbedMDICommandBarCtrlImpl.
//      
//     
//
// Written by Daniel Bowen (dbowen@es.com)
// Copyright (c) 2002-2005 Daniel Bowen.
//
// Depends on CustomTabCtrl.h originally by Bjarke Viksoe (bjarke@viksoe.dk)
//  with the modifications by Daniel Bowen
//
// This code may be used in compiled form in any way you desire. This
// file may be redistributed by any means PROVIDING it is 
// not sold for profit without the authors written consent, and 
// providing that this notice and the authors name is included.
//
// This file is provided "as is" with no expressed or implied warranty.
// The author accepts no liability if it causes any damage to you or your
// computer whatsoever.
//
// If you find bugs, have suggestions for improvements, etc.,
// please contact the author.
//
// History (Date/Author/Description):
// ----------------------------------
//
// 2005/07/13: Daniel Bowen
// - Namespace qualify the use of more ATL and WTL classes.
// - CTabbedMDIFrameWindowImpl:
//   * Add GetMDITabCtrl
//
// 2005/04/12: Daniel Bowen
// - CTabbedMDIClient::OnNcPaint - 
//   * CDC dc(this->GetWindowDC());
//       should be
//     CWindowDC dc(this->m_hWnd);
//
// 2005/04/08: Daniel Bowen
// - Generalize support for having the tab control automatically hidden
//   if the number of tabs is below a certain count.
// - CMDITabOwnerImpl -
//   * Move KeepTabsHidden support into base class CCustomTabOwnerImpl
//   * Move HideMDITabsWhenMDIChildNotMaximized to CMDITabOwnerImpl and have
//     CTabbedMDIClient forward it's call of the same to the tab owner.
//   * Change old "OnAddFirstTab" and "OnRemoveLastTab" to work with
//     new refactored support in CCustomTabOwnerImpl.  It's now
//     OnAddTab and OnRemoveTab with the help of ShowTabControl and
//     HideTabControl.  The work that used to be done
//     in OnAddFirstTab and OnRemoveLastTab is now done in
//     ForceShowMDITabControl and ForceHideMDITabControl.
//   * Add ShowTabControlIfChildMaximized and HideTabControlIfChildNotMaximized
//     that CTabbedMDIClient calls (since this class now tracks
//     HideMDITabsWhenMDIChildNotMaximized)
// - CTabbedMDIClient -
//   * Move HideMDITabsWhenMDIChildNotMaximized to CMDITabOwnerImpl and have
//     CTabbedMDIClient forward it's call of the same to the tab owner.
//
// 2005/03/14: Daniel Bowen
// - Fix warnings when compiling for 64-bit.
//
// 2005/02/03: Daniel Bowen
// - Move registered window messages into TabbedMDIMessages.h
//
// 2004/11/29: Daniel Bowen
// - Update all WM_NOTIFY handlers to check that the notification is
//   from the tab control (and not from a sibling like a list view control)
//
// 2004/06/28: Daniel Bowen
// - CMDITabOwnerImpl -
//   * Fix GetTabStyles to return DWORD instead of bool
// - CTabbedMDIChildWindowImpl -
//   * OnShowTabContextMenu - Add warning in Debug builds if using non SC_* command.
//     To use use non SC_* commands, you should override handling
//      UWM_MDICHILDSHOWTABCONTEXTMENU
//     in a derived class, and do your own context menu there.
//     See the "TabDemo" sample for an example.
// - Clean up warnings on level 4
//
// 2004/05/14: Daniel Bowen
// - CMDITabOwnerImpl - 
//   * Update OnClick handling so it only sets focus to the tab view
//     if the selected tab is being clicked. Without this update,
//     other code that tries to minimize flickering when switching
//     the active view doesn't get called.
// - CTabbedMDIClient
//   * Fix bug in SaveModified that checks incoming argument for NULL
//
// 2004/04/29: Daniel Bowen
// - Require WTL version 7.1 or later (because of WTL's CMDICommandBarCtrlImpl)
// - New and changed registered messages for tabbed MDI children:
//   * UWM_MDICHILDISMODIFIED - Asks whether the document(s) referenced by a 
//     tabbed MDI child has been modified, and if so, the LPARAM has an
//     ITabbedMDIChildModifiedItem* with which to fill out information.
//     Return TRUE if there are one or more modified documents for the child.
//   * UWM_MDICHILDSAVEMODIFIED (change meaning slightly) - Tells the tabbed MDI
//     child to save any modifications without asking.  The LPARAM is an
//     ITabbedMDIChildModifiedItem* with additional information.  Before, this message
//     was used to ask about saving modifications, then saving if the user chose to.
//     The asking about modifications is now consolidated in "SaveAllModified"
//     (or by doing FindModified, CSaveModifiedItemsDialog, SaveModified).
//   * UWM_MDICHILDCLOSEWITHNOPROMPT - Closes the tabbed MDI child bypassing WM_CLOSE
//     (so that the user isn't prompted to save modifications - for when they've
//     already been asked).
// - CTabbedMDIClient
//   * New "HideMDITabsWhenMDIChildNotMaximized".  If you call this
//     with TRUE, then the MDI tabs are shown when the MDI children
//     are maximized, and hidden when they are not maximized.
//   * Change "SaveAllModified".  It can be used to implement "save all"
//     that doesn't prompt, or it can be used to prompt about saving any
//     modified documents using a dialog with a checkbox list for modified items
//     (which only works with tabbed MDI children).
//   * FindModified - Asks all tabbed MDI children whether they have been
//     modified, and uses ITabbedMDIChildModifiedList and ITabbedMDIChildModifiedItem
//     for extended information.
//   * SaveModified - Iterates items in a ITabbedMDIChildModifiedList and
//     sends UWM_MDICHILDSAVEMODIFIED to save modifications. If an item has
//     sub-items, the "top-level" item is responsible for ensuring that
//     modifications are saved.
//   * CloseAll - Close all MDI child windows.  If bPreferNoPrompt is true,
//     try sending UWM_MDICHILDCLOSEWITHNOPROMPT first to the window, so that
//      the child is closed and the user is not prompted about any modifications.
//     Otherwise send WM_SYSCOMMAND with SC_CLOSE, which sends WM_CLOSE.
// - CMDITabOwnerImpl
//   * Have the MDI tab styles default to using the new style
//     CTCS_DRAGREARRANGE
//   * Respond to NM_CLICK, CTCN_ACCEPTITEMDRAG and CTCN_CANCELITEMDRAG
//     from the tab control, and set focus to the tab item's view
//
// 2003/12/16: Daniel Bowen
// - CTabbedMDICommandBarCtrlImpl
//   * Update OnMDISetMenu to match CMDICommandBarCtrlImpl::OnMDISetMenu in WTL 7.1
//   * Update RefreshMaximizedState to match corresponding code in
//     CMDICommandBarCtrlImpl::OnAllHookMessages in WTL 7.1
//   * Have RefreshMaximizedState be overrideable (call through pT->)
//   * Have RefreshMaximizedState's 2nd parameter indicate whether the document
//     icon has changed, instead of whether the child has changed
//     (to match what WTL 7.1 is doing in OnAllHookMessages)
///  * NOTE: There is a problem in WTL 7.1 OnAllHookMessages that
//     CTabbedMDICommandBarCtrlImpl fixes.  See
//     http://groups.yahoo.com/group/wtl/message/7627
//     for a description of the fix.  Future versions of WTL should
//     also have this fix.
//
// 2003/08/11: Daniel Bowen
// - CMDITabOwner:
//   * Add new "KeepTabsHidden" method.
//   * Have the old CMDITabOwner be CMDITabOwnerImpl with new template parameter
//     for most derived class so that others can derive from it and
//     have things work right.  CMDITabOwner now is very simple and
//     just inherits from CMDITabOwnerImpl
// - Use "LongToHandle" with return value from GetClassLong
//
// 2003/06/27: Daniel Bowen
// - CMDITabOwner:
//   * Use typedefs for thisClass, baseClass, customTabOwnerClass
//   * Replace
//      DECLARE_WND_CLASS(_T("MdiTabOwner"))
//     with
//      DECLARE_WND_CLASS_EX(_T("MdiTabOwner"), 0, COLOR_APPWORKSPACE)
//     (gets rid of CS_DBLCLKS, CS_HREDRAW and CS_VREDRAW, sets background brush)
//   * Have OnAddFirstTab and OnRemoveLastTab call base class versions
//     (in case we ever do anything interesting there)
// - Check failure of window creation (DefWindowProc when handling WM_CREATE)
//   and return immediately if it failed.
//
// 2003/06/03: Daniel Bowen
// - Fix compile errors for VC 7.1
//
// 2003/02/27: Daniel Bowen
// - Use _U_RECT instead of WTL::_U_RECT.
//   For VC7, this means you must #define _WTL_NO_UNION_CLASSES
//   before including the WTL header files, or you will
//   get compile errors (the ATL7 union classes are defined
//   in atlwin.h).
//
// 2002/12/11: Daniel Bowen
// - New UWM_MDICHILDSAVEMODIFIED message sent to MDI child frames.
//   The child frame receiving this should see if the "document"
//   has been modified, and needs to be saved (usually with a 
//   yes/no/cancel message box).  If the user chooses to cancel,
//   return non-zero from the UWM_MDICHILDSAVEMODIFIED handler.
// - CTabbedMDIClient -
//   * SaveAllModified - Iterates the MDI child windows, and
//      sends UWM_MDICHILDSAVEMODIFIED. If a child returns non-zero,
//      the iteration stops.  You can also specify whether to
//      close the child window after sending UWM_MDICHILDSAVEMODIFIED.
//   * CloseAll - Close All MDI child windows.  When handling WM_CLOSE,
//      the MDI child window should *not* have a "Cancel" option
//      if prompting to save a modified document.
//
// 2002/11/27: Daniel Bowen
// - CTabbedMDIChildWindowImpl -
//   * When handling WM_MOUSEACTIVATE in CTabbedMDIChildWindowImpl,
//     let the message get to the top window before possibly doing
//     MDIActivate (more like the MFC code in CMDIChildWnd::OnMouseActivate)
//
// 2002/11/21: Daniel Bowen
// - CMDITabOwner - 
//   * ModifyTabStyles for use before CMDITabOwner is created as a window
// - CTabbedMDIClient -
//   * Expose SetDrawFlat and GetDrawFlat
//   * Updates so that drawing flat draws correctly
// - CTabbedMDIChildWindowImpl - 
//   * Handle WM_MOUSEACTIVATE, and call MDIActivate if
//     MDI child is not already active.  This solves the problem
//     where you have a dialog window as the view of the MDI child,
//     and clicking on a child control (edit box, etc.) doesn't
//     give focus or activate the MDI child (or activate the app
//     if its not active).  This code will ideally make its
//     way into future versions of WTL.
//
// 2002/09/25: Daniel Bowen
// - CTabbedMDICommandBarCtrl -
//   * Break out CTabbedMDICommandBarCtrl into CTabbedMDICommandBarCtrlImpl
//     and CTabbedMDICommandBarCtrl (just like CMDICommandBarCtrlImpl
//     and CMDICommandBarCtrl).
//     You can derive from CTabbedMDICommandBarCtrlImpl
//     if you would like to extend functionality (such as providing
//     your own handling of WM_MDISETMENU).  See the commented out
//     sample class after CTabbedMDICommandBarCtrl.
// - CMDITabOwner -
//   * Expose "SetTabStyles" and "GetTabStyles" so that you can change
//     the tab related styles to something different than the default
// - UWM_MDI... messages -
//   * The TabbedMDI related classes use a handful of custom window
//     messages.  These messages are guaranteed to be unique across
//     all windows for a particular windows session by using
//     RegisterWindowMessage
//
//     Initially, these message IDs were declared as static variables
//     and initialized here in the header.
//     However, that gave them "internal linkeage".  Essentially,
//     this meant that there were multiple copies of these variables.
//     In Visual C++ 6, there was also a bug that caused the variables
//     not to be initialized properly in release mode. So the class
//     CTabbedMDIChildWindowImpl ensured their initialization in its
//     constructor.  The problem was, only the version of the variables
//     in the same translation unit got initialized by doing it this way.
//
//     These variables are now declared using __declspec(selectany)
//     so that there will not be multiple copies.  RegisterWindowMessage
//     for each message ID is now called in the constructor of the
//     struct "RegisterTabbedMDIMessages". If you are using _ATL_MIN_CRT
//     or define _TABBEDMDI_MESSAGES_EXTERN_REGISTER, then you must
//     have an instance of the "RegisterTabbedMDIMessages" struct in
//     one .cpp file.  Otherwise, a global instance of the struct will
//     be declared in this header file (whose constructor will be called
//     by the CRT at load time).  If you are not referencing
//     TabbedMDI.h in stdafx.h, and have multiple translation units
//     including it, then you'll need to do it the 
//     _TABBEDMDI_MESSAGES_EXTERN_REGISTER way.  Also, if you do
//     use _ATL_MIN_CRT, you will get a warning unless you define
//     _TABBEDMDI_MESSAGES_NO_WARN_ATL_MIN_CRT
//
// 2002/08/26: Daniel Bowen
// - CTabbedMDIClient -
//   * Add new template parameter "TTabOwner" to allow easily
//     changing the tab owner class used
//
// 2002/06/26: Daniel Bowen
// - CTabbedMDIFrameWindowImpl - Expose "TClient" and "TTabCtrl".
// - CMDITabOwner - Expose "TTabCtrl".
// - CTabbedMDIClient -
//   * Expose "TTabCtrl"
//   * New method "GetTabOwner" to get a reference to the C++ class
//     instance implementing the MDI tab owner window.
//   * New method "UseMDIChildIcon" to specify that you want the MDI
//     tabs to include the document icon for the MDI child on the
//     corresponding tab
// 
// 2002/06/12: Daniel Bowen
// - Publish codeproject article.  For history prior
//   to the release of the article, please see the article
//   and the section "Note to previous users"

#ifndef __WTL_TABBED_MDI_H__
#define __WTL_TABBED_MDI_H__

#pragma once

#ifndef __cplusplus
	#error Tabbed MDI requires C++ compilation (use a .cpp suffix)
#endif

#ifndef __ATLAPP_H__
	#error TabbedMDI.h requires atlapp.h to be included first
#endif

#ifndef __ATLWIN_H__
	#error TabbedMDI.h requires atlwin.h to be included first
#endif

#ifndef __ATLFRAME_H__
	#error TabbedFrame.h requires atlframe.h to be included first
#endif

#ifndef __ATLCTRLW_H__
	#error TabbedFrame.h requires atlctrlw.h to be included first
#endif

#if _WTL_VER < 0x0710
	#error TabbedMDI.h requires WTL 7.1 or higher
#endif

#ifndef __CUSTOMTABCTRL_H__
#include "CustomTabCtrl.h"
#endif

#ifndef __WTL_TABBED_FRAME_H__
#include "TabbedFrame.h"
#endif

#ifndef __WTL_TABBED_MDI_MESSAGES_H__
#include "TabbedMDIMessages.h"
#endif


/////////////////////////////////////////////////////////////////////////////
//
// CTabbedMDIFrameWindowImpl
//
/////////////////////////////////////////////////////////////////////////////

template <
	class T,
	class TClient = CTabbedMDIClient< CDotNetTabCtrl<CTabViewTabItem> >,
	class TBase = WTL::CMDIWindow,
	class TWinTraits = ATL::CFrameWinTraits>
class ATL_NO_VTABLE CTabbedMDIFrameWindowImpl :
	public WTL::CMDIFrameWindowImpl<T, TBase, TWinTraits >
{
public:
	// Expose the type of MDI client
	typedef typename TClient TClient;
	// Expose the type of tab control
	typedef typename TClient::TTabCtrl TTabCtrl;
	
// Member variables
protected:
	TClient m_tabbedClient;

// Methods
public:
	// Either call the normal "CreateMDIClient"

	HWND CreateMDIClient(HMENU hWindowMenu = NULL, UINT nID = ATL_IDW_CLIENT, UINT nFirstChildID = ATL_IDM_FIRST_MDICHILD)
	{
		HWND hWndClient = baseClass::CreateMDIClient(hWindowMenu, nID, nFirstChildID);

		this->SubclassMDIClient();

		return hWndClient;
	}

	// Or, after creating the client, call SubclassMDIClient
	BOOL SubclassMDIClient()
	{
		ATLASSERT(m_hWndMDIClient && ::IsWindow(m_hWndMDIClient));

		return m_tabbedClient.SubclassWindow(m_hWndMDIClient);
	}

	void SetTabOwnerParent(HWND hWndTabOwnerParent)
	{
		m_tabbedClient.SetTabOwnerParent(hWndTabOwnerParent);
	}

	HWND GetTabOwnerParent() const
	{
		return m_tabbedClient.GetTabOwnerParent();
	}

	TTabCtrl& GetMDITabCtrl()
	{
		return m_tabbedClient.GetTabOwner().GetTabCtrl();
	}

// Message Handling
public:
	typedef CTabbedMDIFrameWindowImpl< T, TBase, TWinTraits >	thisClass;
	typedef WTL::CMDIFrameWindowImpl<T, TBase, TWinTraits >	baseClass;
	BEGIN_MSG_MAP(thisClass)
		CHAIN_MSG_MAP(baseClass)
	END_MSG_MAP()
};

template <class T, class TBase = WTL::CMDIWindow, class TWinTraits = ATL::CMDIChildWinTraits>
class ATL_NO_VTABLE CTabbedMDIChildWindowImpl : public WTL::CMDIChildWindowImpl<T, TBase, TWinTraits>
{
// Member variables
protected:
	bool m_bMaximized;

// Constructors
public:
	CTabbedMDIChildWindowImpl() :
		m_bMaximized(false)
	{
		ATLASSERT(UWM_MDICHILDACTIVATIONCHANGE != 0 && "The TabbedMDI Messages didn't get registered properly");
	}

// Overrides ofCMDIChildWindowImpl
public:

	// NOTE: CreateEx also calls this (through T*)
	HWND Create(HWND hWndParent, _U_RECT rect = NULL, LPCTSTR szWindowName = NULL,
			DWORD dwStyle = 0U, DWORD dwExStyle = 0U,
			UINT nMenuID = 0U, LPVOID lpCreateParam = NULL)
	{
		// NOTE: hWndParent is going to become m_hWndMDIClient
		//  in CMDIChildWindowImpl::Create
		ATLASSERT(::IsWindow(hWndParent));

		BOOL bMaximizeNewChild = (WS_MAXIMIZE == (T::GetWndStyle(dwStyle) & WS_MAXIMIZE));
		if(bMaximizeNewChild == FALSE)
		{
			// If WS_MAXIMIZE wasn't requested, check if the currently
			//  active MDI child (if there is one) is maximized.  If so,
			//  maximize the new child window to match.
			::SendMessage(hWndParent, WM_MDIGETACTIVE, 0, (LPARAM)&bMaximizeNewChild);
		}

		if(bMaximizeNewChild == TRUE)
		{
			::SendMessage(hWndParent, WM_SETREDRAW, FALSE, 0);

			// We'll ShowWindow(SW_SHOWMAXIMIZED) instead of using
			//  WS_MAXIMIZE (which would cause visual anomolies in some cases)
			dwStyle = (T::GetWndStyle(dwStyle) & ~WS_MAXIMIZE);
		}


		HWND hWnd = baseClass::Create(hWndParent, rect, szWindowName, dwStyle, dwExStyle, nMenuID, lpCreateParam);

		if(bMaximizeNewChild == TRUE)
		{
			::ShowWindow(hWnd, SW_SHOWMAXIMIZED);

			::SendMessage(hWndParent, WM_SETREDRAW, TRUE, 0);
			::RedrawWindow(hWndParent, NULL, NULL,
				//A little more forceful if necessary: RDW_ERASE | RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
				RDW_INVALIDATE | RDW_ALLCHILDREN);
		}

		if(hWnd != NULL && ::IsWindow(m_hWnd) && !::IsChild(hWnd, ::GetFocus()))
			::SetFocus(hWnd);

		return hWnd;
	}

// Methods
public:
	void SetTitle(LPCTSTR sNewTitle, BOOL bUpdateTabText = TRUE)
	{
		this->SetWindowText(sNewTitle);

		if(bUpdateTabText)
		{
			::SendMessage(m_hWndMDIClient, UWM_MDICHILDTABTEXTCHANGE, (WPARAM)m_hWnd, (LPARAM)sNewTitle);
		}
	}

	void SetTabText(LPCTSTR sNewTabText)
	{
		::SendMessage(m_hWndMDIClient, UWM_MDICHILDTABTEXTCHANGE, (WPARAM)m_hWnd, (LPARAM)sNewTabText);
	}

	void SetTabToolTip(LPCTSTR sNewToolTip)
	{
		::SendMessage(m_hWndMDIClient, UWM_MDICHILDTABTOOLTIPCHANGE, (WPARAM)m_hWnd, (LPARAM)sNewToolTip);
	}

// Message Handling
public:

	typedef CTabbedMDIChildWindowImpl< T, TBase, TWinTraits >	thisClass;
	typedef WTL::CMDIChildWindowImpl<T, TBase, TWinTraits >	baseClass;
	BEGIN_MSG_MAP(thisClass)
		MESSAGE_HANDLER(WM_MDIACTIVATE, OnMDIActivate)
		MESSAGE_HANDLER(WM_SETTEXT, OnSetText)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)
		MESSAGE_HANDLER(WM_MOUSEACTIVATE, OnMouseActivate)
		MESSAGE_HANDLER(UWM_MDICHILDSHOWTABCONTEXTMENU, OnShowTabContextMenu)
		MESSAGE_HANDLER(UWM_MDICHILDSAVEMODIFIED, OnSaveModified)
		MESSAGE_HANDLER(UWM_MDICHILDCLOSEWITHNOPROMPT, OnCloseWithNoPrompt)
		CHAIN_MSG_MAP(baseClass)
	END_MSG_MAP()

	LRESULT OnMDIActivate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
	{
		//HWND hWndDeactivating = (HWND)wParam;
		HWND hWndActivating = (HWND)lParam;

		if(m_hWnd == hWndActivating)
		{
			::SendMessage(m_hWndMDIClient, UWM_MDICHILDACTIVATIONCHANGE, (WPARAM)m_hWnd, 0);
		}

		bHandled = FALSE;
		return 0;
	}

	LRESULT OnSetText(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
	{
		//::SendMessage(m_hWndMDIClient, UWM_MDICHILDTABTEXTCHANGE, (WPARAM)m_hWnd, lParam);

		bHandled = FALSE;
		return 0;
	}

	LRESULT OnSize(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	{
		if(wParam == SIZE_MAXIMIZED && m_bMaximized == false)
		{
			m_bMaximized = true;
			::SendMessage(m_hWndMDIClient, UWM_MDICHILDMAXIMIZED, (WPARAM)m_hWnd, lParam);
		}
		else if(wParam != SIZE_MAXIMIZED && m_bMaximized == true)
		{
			m_bMaximized = false;
			::SendMessage(m_hWndMDIClient, UWM_MDICHILDUNMAXIMIZED, (WPARAM)m_hWnd, lParam);
		}

		bHandled = FALSE;
		return 0;
	}

	LRESULT OnMouseActivate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
	{
		LRESULT lRes = this->DefWindowProc(uMsg, wParam, lParam);
		if(lRes == MA_NOACTIVATE || lRes == MA_NOACTIVATEANDEAT)
			return lRes;   // frame does not want to activate

		// activate window if necessary
		HWND hWndActive = this->MDIGetActive();
		if(m_hWnd != hWndActive)
		{
			this->MDIActivate(m_hWnd);
		}

		return lRes;
	}

	LRESULT OnSetFocus(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
	{
		// NOTE: ::IsWindowVisible(m_hWndClient) will be false if
		//  the frame is maximized.  So just use "IsWindow" instead.
		if(m_hWndClient != NULL && ::IsWindow(m_hWndClient))
		{
			::SetFocus(m_hWndClient);
		}

		bHandled = FALSE;
		return 1;
	}

	LRESULT OnShowTabContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
	{
		bHandled = TRUE;

		// NOTE: Your deriving class doesn't have to do the context menu
		//  this way (show the system menu, use TPM_RETURNCMD, etc.)
		//  It can do whatever it wants.  This is just the "fall through"
		//  implementation if you don't want to specialize.

		// NOTE: The sender of the message has already handled the case
		//  when launching the context menu from the keyboard
		//  (translating -1,-1 into a usable position)

		// We use essentially the same code as CMDICommandBarCtrl::OnNcLButtonDown
		//  (for when it handles the menu for the maximized child when clicking
		//  on the document icon to the left of the menus).
		// NOTE: On Windows 2000 and 98 and later, we'll get bitmaps in the menu.
		//  Also note that when running on NT 4 or Win 95, CMDICommandBarCtrl::OnNcLButtonDown
		//  will fail to show the system menu at all because it doesn't like
		//  the real definition of TPM_VERPOSANIMATION.  To avoid that
		//  problem, we won't even try to use TPM_VERPOSANIMATION.
		WTL::CMenuHandle menu = this->GetSystemMenu(FALSE);

		UINT command = (UINT)menu.TrackPopupMenu(TPM_LEFTBUTTON | TPM_VERTICAL | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD, 
			GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), m_hWnd);

		// See MSDN about "GetSystemMenu".  Returns codes greater than
		//  0xF000 (which happens to be SC_SIZE) are sent with WM_SYSCOMMAND
		if(command >= SC_SIZE)
		{
			::PostMessage(m_hWnd, WM_SYSCOMMAND, command, 0L);
		}
		else if(command != 0)
		{
			// Non SC_* commands don't work with WM_SYSCOMMAND.  We could handle
			// the situation here by using WM_COMMAND, but there's other places
			// where the "window menu" is dealt with that wouldn't have the same handling
			// (like CMDICommandBarCtrl::OnNcLButtonDown).  To help prevent
			// errors, do an assert to warn.  Instead of depending on this base
			// implementation, you should override handling UWM_MDICHILDSHOWTABCONTEXTMENU
			// in a derived class, and do your own context menu there.
			// See the "TabDemo" sample for an example.
			ATLASSERT(0 && 
				"You've tried to put a non SC_* command in the window menu.  "
				"Please override the default context menu handling, and use a custom menu.");
		}

		return 0;
	}

	LRESULT OnSaveModified(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
	{
		// A derived class should handle this message.
		// Return non-zero if you want to "cancel"
		bHandled = FALSE;
		return 0;
	}

	LRESULT OnCloseWithNoPrompt(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	{
		// When getting a real WM_CLOSE, you usually want to
		// prompt to save if there's been modifications, and
		// return TRUE to cancel.  However, this is here for
		// the cases when we need to close the window with no
		// prompt and no chance to cancel
		// (such as when we've already given the user the
		// chance to save the file, but they said no).
		return this->DefWindowProc(WM_CLOSE, 0, 0L);
	}
};


/////////////////////////////////////////////////////////////////////////////
//
// CMDITabOwner
//
/////////////////////////////////////////////////////////////////////////////

template< class T, class TTabCtrl >
class CMDITabOwnerImpl :
	public ATL::CWindowImpl<T>,
	public CCustomTabOwnerImpl<T, TTabCtrl>
{
public:
	// Expose the type of tab control
	typedef typename TTabCtrl TTabCtrl;

protected:
	typedef CMDITabOwnerImpl<T, TTabCtrl> thisClass;
	typedef ATL::CWindowImpl<T> baseClass;
	typedef CCustomTabOwnerImpl<T, TTabCtrl> customTabOwnerClass;

// Member variables
protected:
	HWND m_hWndMDIClient;
	DWORD m_nTabStyles;
	BOOL m_bHideMDITabsWhenMDIChildNotMaximized;

// Constructors
public:
	CMDITabOwnerImpl() :
		m_hWndMDIClient(NULL),
		m_nTabStyles(CTCS_TOOLTIPS | CTCS_BOLDSELECTEDTAB | CTCS_SCROLL | CTCS_CLOSEBUTTON | CTCS_DRAGREARRANGE),
		m_bHideMDITabsWhenMDIChildNotMaximized(FALSE)
	{
		ATLASSERT(UWM_MDICHILDACTIVATIONCHANGE != 0 && "The TabbedMDI Messages didn't get registered properly");
		m_nMinTabCountForVisibleTabs = 1;
		m_bKeepTabsHidden = (m_nMinTabCountForVisibleTabs > 0);
	}

// Methods
public:
	void SetMDIClient(HWND hWndMDIClient)
	{
		m_hWndMDIClient = hWndMDIClient;
	}

	void SetTabStyles(DWORD nTabStyles)
	{
		m_nTabStyles = nTabStyles;
	}

	DWORD GetTabStyles(void) const
	{
		return m_nTabStyles;
	}

	void ModifyTabStyles(DWORD dwRemove, DWORD dwAdd)
	{
		DWORD dwNewStyle = (m_nTabStyles & ~dwRemove) | dwAdd;
		if(m_nTabStyles != dwNewStyle)
		{
			m_nTabStyles = dwNewStyle;
		}
	}

	void HideMDITabsWhenMDIChildNotMaximized(BOOL bHideMDITabsWhenMDIChildNotMaximized = TRUE)
	{
		m_bHideMDITabsWhenMDIChildNotMaximized = bHideMDITabsWhenMDIChildNotMaximized;
	}

// Overrideables
public:

	void ForceShowMDITabControl()
	{
		if(m_hWnd && !this->IsWindowVisible())
		{
			RECT rcMDIClient;
			::GetWindowRect(m_hWndMDIClient, &rcMDIClient);
			::MapWindowPoints(NULL, ::GetParent(m_hWndMDIClient), (LPPOINT)&rcMDIClient, 2);

			this->ShowWindow(SW_SHOW);

			// the MDI client resizes and shows our window when
			//  handling messages related to SetWindowPos
			::SetWindowPos(
				m_hWndMDIClient, NULL,
				rcMDIClient.left, rcMDIClient.top,
				(rcMDIClient.right - rcMDIClient.left),(rcMDIClient.bottom - rcMDIClient.top),
				SWP_NOZORDER);
		}
	}

	void ForceHideMDITabControl()
	{
		if(m_hWnd && this->IsWindowVisible())
		{
			RECT rcTabs;
			m_TabCtrl.GetWindowRect(&rcTabs);
			::MapWindowPoints(NULL, m_TabCtrl.GetParent(), (LPPOINT)&rcTabs, 2);

			this->ShowWindow(SW_HIDE);

			RECT rcMDIClient;
			::GetWindowRect(m_hWndMDIClient, &rcMDIClient);
			::MapWindowPoints(NULL, ::GetParent(m_hWndMDIClient), (LPPOINT)&rcMDIClient, 2);

			// the MDI client resizes and shows our window when
			//  handling messages related to SetWindowPos

			// TODO: Is there a better way to do this?
			//  We're basically hiding the tabs and
			//  resizing the MDI client area to "cover up"
			//  where the tabs were
			DWORD dwStyle = m_TabCtrl.GetStyle();
			if(CTCS_BOTTOM == (dwStyle & CTCS_BOTTOM))
			{
				::SetWindowPos(
					m_hWndMDIClient, NULL,
					rcMDIClient.left, rcMDIClient.top,
					(rcMDIClient.right - rcMDIClient.left),
					(rcMDIClient.bottom - rcMDIClient.top) + (rcTabs.bottom - rcTabs.top),
					SWP_NOZORDER);
			}
			else
			{
				::SetWindowPos(
					m_hWndMDIClient, NULL,
					rcMDIClient.left, rcMDIClient.top - (rcTabs.bottom - rcTabs.top),
					(rcMDIClient.right - rcMDIClient.left),
					(rcMDIClient.bottom - rcMDIClient.top) + (rcTabs.bottom - rcTabs.top),
					SWP_NOZORDER);
			}
		}
	}

	void ShowTabControlIfChildMaximized(void)
	{
		if(m_bHideMDITabsWhenMDIChildNotMaximized)
		{
			size_t nTabCount = m_TabCtrl.GetItemCount();
			if(nTabCount >= m_nMinTabCountForVisibleTabs)
			{
				T* pT = static_cast<T*>(this);
				pT->ShowTabControl();
			}
		}
	}

	void HideTabControlIfChildNotMaximized(void)
	{
		if(m_bHideMDITabsWhenMDIChildNotMaximized)
		{
			T* pT = static_cast<T*>(this);
			pT->HideTabControl();
		}
	}

// Message Handling
public:
	DECLARE_WND_CLASS_EX(_T("MdiTabOwner"), 0, COLOR_APPWORKSPACE)

	BEGIN_MSG_MAP(thisClass)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBackground)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
		NOTIFY_CODE_HANDLER(NM_CLICK, OnClick)
		NOTIFY_CODE_HANDLER(CTCN_ACCEPTITEMDRAG, OnAcceptItemDrag)
		NOTIFY_CODE_HANDLER(CTCN_CANCELITEMDRAG, OnCancelItemDrag)
		NOTIFY_CODE_HANDLER(CTCN_DELETEITEM, OnDeleteItem)
		NOTIFY_CODE_HANDLER(CTCN_SELCHANGING, OnSelChanging)
		NOTIFY_CODE_HANDLER(CTCN_SELCHANGE, OnSelChange)
		NOTIFY_CODE_HANDLER(CTCN_CLOSE, OnTabClose)

		// NOTE: CCustomTabCtrl derived classes no longer
		//  need notifications reflected.
		// REFLECT_NOTIFICATIONS()
	END_MSG_MAP()

	LRESULT OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	{
		// "baseClass::OnCreate()"
		LRESULT lRet = this->DefWindowProc(uMsg, wParam, lParam);
		bHandled = TRUE;
		if(lRet == -1)
		{
			return -1;
		}

		CreateTabWindow(m_hWnd, rcDefault, m_nTabStyles);

		return 0;
	}

	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
	{
		DestroyTabWindow();

		// Say that we didn't handle it so that anyone else
		//  interested gets to handle the message
		bHandled = FALSE;
		return 0;
	}

	LRESULT OnEraseBackground(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
	{
		if(m_TabCtrl)
		{
			// Let the tabs do all the drawing as flicker-free as possible
			return 1;
		}

		bHandled = FALSE;
		return 0;
	}

	LRESULT OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
	{
		RECT rect = {0};
		GetClientRect(&rect);

		m_TabCtrl.SetWindowPos(NULL, &rect, SWP_NOZORDER | SWP_NOACTIVATE);
		m_TabCtrl.UpdateLayout();

		bHandled = TRUE;

		return 0;
	}

	LRESULT OnContextMenu(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	{
		bHandled = TRUE;

		int nIndex = -1;

		POINT ptPopup = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
		if(ptPopup.x == -1 && ptPopup.y == -1)
		{
			nIndex = m_TabCtrl.GetCurSel();
			RECT rect = {0};
			if(nIndex >= 0)
			{
				// If there is a selected item, popup the menu under the node,
				// if not, pop it up in the top left of the tree view
				m_TabCtrl.GetItemRect(nIndex, &rect);
			}
			::MapWindowPoints(m_hWnd, NULL, (LPPOINT)&rect, 2);
			ptPopup.x = rect.left;
			ptPopup.y = rect.bottom;
		}
		else
		{
			POINT ptClient = ptPopup;
			::MapWindowPoints(NULL, m_hWnd, &ptClient, 1);
			CTCHITTESTINFO tchti = { 0 };
			tchti.pt = ptClient;
			//If we become templated, pT->HitTest(&tchti);
			nIndex = m_TabCtrl.HitTest(&tchti);
		}

		if( nIndex >= 0 )
		{
			TTabCtrl::TItem* pItem = m_TabCtrl.GetItem(nIndex);

			HWND hWndChild = pItem->GetTabView();
			if(hWndChild != NULL)
			{
				::SendMessage(hWndChild, UWM_MDICHILDSHOWTABCONTEXTMENU, wParam, MAKELPARAM(ptPopup.x, ptPopup.y));
			}
		}

		return 0;
	}

	LRESULT OnClick(int /*idCtrl*/, LPNMHDR pnmh, BOOL& bHandled)
	{
		// Be sure the notification is from the tab control
		// (and not from a sibling like a list view control)
		if(pnmh && (m_TabCtrl == pnmh->hwndFrom))
		{
			// If they left click on an item, set focus on the tab view,
			// but only if the view was already the active tab view
			// (otherwise our code to reduce flicker when switching
			// MDI children when maximized doesn't kick in).
			NMCTCITEM* item = (NMCTCITEM*)pnmh;
			if(item && (item->iItem >= 0) && (item->iItem == m_TabCtrl.GetCurSel()))
			{
				TTabCtrl::TItem* pItem = m_TabCtrl.GetItem(item->iItem);
				if(pItem->UsingTabView())
				{
					::SetFocus(pItem->GetTabView());
				}
			}
		}

		bHandled = FALSE;
		return 0;
	}

	LRESULT OnAcceptItemDrag(int /*idCtrl*/, LPNMHDR pnmh, BOOL& bHandled)
	{
		// Be sure the notification is from the tab control
		// (and not from a sibling like a list view control)
		if(pnmh && (m_TabCtrl == pnmh->hwndFrom))
		{
			// If finished dragging, set focus on the tab view.
			NMCTC2ITEMS* item = (NMCTC2ITEMS*)pnmh;
			if(item && (item->iItem2 >= 0))
			{
				TTabCtrl::TItem* pItem = m_TabCtrl.GetItem(item->iItem2);
				if(pItem->UsingTabView())
				{
					::SetFocus(pItem->GetTabView());
				}
			}
		}

		bHandled = FALSE;
		return 0;
	}

	LRESULT OnCancelItemDrag(int /*idCtrl*/, LPNMHDR pnmh, BOOL& bHandled)
	{
		// Be sure the notification is from the tab control
		// (and not from a sibling like a list view control)
		if(pnmh && (m_TabCtrl == pnmh->hwndFrom))
		{
			// If finished dragging, set focus on the tab view.
			NMCTCITEM* item = (NMCTCITEM*)pnmh;
			if(item && (item->iItem >= 0))
			{
				TTabCtrl::TItem* pItem = m_TabCtrl.GetItem(item->iItem);
				if(pItem->UsingTabView())
				{
					::SetFocus(pItem->GetTabView());
				}
			}
		}

		bHandled = FALSE;
		return 0;
	}

	LRESULT OnDeleteItem(int /*idCtrl*/, LPNMHDR pnmh, BOOL& bHandled)
	{
		// Be sure the notification is from the tab control
		// (and not from a sibling like a list view control)
		if(pnmh && (m_TabCtrl == pnmh->hwndFrom))
		{
		}

		bHandled = FALSE;
		return 0;
	}

	LRESULT OnSelChanging(int /*idCtrl*/, LPNMHDR pnmh, BOOL& bHandled)
	{
		// Be sure the notification is from the tab control
		// (and not from a sibling like a list view control)
		if(pnmh && (m_TabCtrl == pnmh->hwndFrom))
		{
		}

		bHandled = FALSE;
		return 0;
	}

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

	LRESULT OnTabClose(int /*idCtrl*/, LPNMHDR pnmh, BOOL& bHandled)
	{
		// Be sure the notification is from the tab control
		// (and not from a sibling like a list view control)
		if(pnmh && (m_TabCtrl == pnmh->hwndFrom))
		{
			LPNMCTCITEM pnmCustomTab = (LPNMCTCITEM)pnmh;
			if(pnmCustomTab)
			{
				if(pnmCustomTab->iItem >= 0)
				{
					TTabCtrl::TItem* pItem = m_TabCtrl.GetItem(pnmCustomTab->iItem);
					if(pItem)
					{
						::PostMessage(pItem->GetTabView(), WM_SYSCOMMAND, SC_CLOSE, 0L);
					}
				}
			}
		}

		bHandled = FALSE;
		return 0;
	}

// Overrides from CCustomTabOwnerImpl
public:

	void ShowTabControl()
	{
		T* pT = static_cast<T*>(this);
		if(m_bHideMDITabsWhenMDIChildNotMaximized && m_hWndMDIClient)
		{
			HWND hWndActiveChild = (HWND)::SendMessage(m_hWndMDIClient, WM_MDIGETACTIVE, 0, 0);
			if(hWndActiveChild && ::IsZoomed(hWndActiveChild))
			{
				pT->KeepTabsHidden(false);
			}
		}
		else
		{
			pT->KeepTabsHidden(false);
		}
	}

	void HideTabControl()
	{
		T* pT = static_cast<T*>(this);
		pT->KeepTabsHidden(true);
	}

	void KeepTabsHidden(bool bKeepTabsHidden = true)
	{
		if(m_bKeepTabsHidden != bKeepTabsHidden)
		{
			m_bKeepTabsHidden = bKeepTabsHidden;

			// CalcTabAreaHeight will end up doing UpdateLayout and Invalidate
			T* pT = static_cast<T*>(this);
			pT->CalcTabAreaHeight();

			// For MDI tabs, the UpdateLayout done by CalcTabAreaHeight
			//  is not quite enough to force the tab control to show or hide.
			//  So we'll force the tab control to be shown or hidden.
			if(m_bKeepTabsHidden)
			{
				pT->ForceHideMDITabControl();
			}
			else
			{
				pT->ForceShowMDITabControl();
			}
		}
	}

	void SetTabAreaHeight(int nNewTabAreaHeight)
	{
		if(m_bKeepTabsHidden)
		{
			m_nTabAreaHeight = 0;

			//T* pT = static_cast<T*>(this);
			//pT->UpdateLayout();
			//Invalidate();
		}
		else if(m_nTabAreaHeight != nNewTabAreaHeight)
		{
			int nOldTabAreaHeight = m_nTabAreaHeight;

			m_nTabAreaHeight = nNewTabAreaHeight;

			//T* pT = static_cast<T*>(this);
			//pT->UpdateLayout();
			//Invalidate();

			if(this->IsWindowVisible() == TRUE)
			{
				RECT rcMDIClient;
				::GetWindowRect(m_hWndMDIClient, &rcMDIClient);
				::MapWindowPoints(NULL, this->GetParent(), (LPPOINT)&rcMDIClient, 2);

				// Don't ask me why these two lines are necessary.
				// Take these lines out if you want to
				// convince yourself that they are :-)
				rcMDIClient.top -= nOldTabAreaHeight;
				rcMDIClient.bottom += nOldTabAreaHeight;

				// The tab resize/reposition logic happens when handling WM_WINDOWPOSCHANGING.
				// If that ever changes, make the appropriate change here.
				::SetWindowPos(
					m_hWndMDIClient, NULL,
					rcMDIClient.left, rcMDIClient.top,
					(rcMDIClient.right - rcMDIClient.left),(rcMDIClient.bottom - rcMDIClient.top),
					SWP_NOZORDER | SWP_NOACTIVATE);
			}
		}
	}
};

template< class TTabCtrl >
class CMDITabOwner :
	public CMDITabOwnerImpl<CMDITabOwner<TTabCtrl>, TTabCtrl>
{
};

/////////////////////////////////////////////////////////////////////////////
//
// CTabbedMDIClient
//
/////////////////////////////////////////////////////////////////////////////

template< class TTabCtrl = CDotNetTabCtrl<CTabViewTabItem>, class TTabOwner = CMDITabOwner<TTabCtrl> >
class CTabbedMDIClient : public ATL::CWindowImpl<CTabbedMDIClient<TTabCtrl, TTabOwner>, ATL::CWindow>
{
public:
	// Expose the type of tab control and tab owner
	typedef typename TTabCtrl TTabCtrl;
	typedef typename TTabOwner TTabOwner;

protected:
	typedef ATL::CWindowImpl<CTabbedMDIClient, ATL::CWindow> baseClass;
	typedef CTabbedMDIClient< TTabCtrl, TTabOwner > thisClass;

// Member variables
protected:
	HWND m_hWndTabOwnerParent;
	TTabOwner m_MdiTabOwner;
	BOOL m_bUseMDIChildIcon;
	bool m_bSubclassed;
	bool m_bDrawFlat;

// Constructors
public:
	CTabbedMDIClient() :
		m_hWndTabOwnerParent(NULL),
		m_bUseMDIChildIcon(FALSE),
		m_bSubclassed(false),
		m_bDrawFlat(false)
	{
		ATLASSERT(UWM_MDICHILDACTIVATIONCHANGE != 0 && "The TabbedMDI Messages didn't get registered properly");
		if(m_bDrawFlat)
		{
			m_MdiTabOwner.ModifyTabStyles(0,CTCS_FLATEDGE);
		}
	}

	virtual ~CTabbedMDIClient()
	{
		if(this->IsWindow() && m_bSubclassed)
		{
			this->UnsubclassWindow(TRUE);
		}
	}

// Methods
public:
	void SetTabOwnerParent(HWND hWndTabOwnerParent)
	{
		m_hWndTabOwnerParent = hWndTabOwnerParent;
	}

	HWND GetTabOwnerParent(void) const
	{
		return m_hWndTabOwnerParent;
	}

	TTabOwner& GetTabOwner(void)
	{
		return m_MdiTabOwner;
	}

	void UseMDIChildIcon(BOOL bUseMDIChildIcon = TRUE)
	{
		m_bUseMDIChildIcon = bUseMDIChildIcon;
	}

	void HideMDITabsWhenMDIChildNotMaximized(BOOL bHideMDITabsWhenMDIChildNotMaximized = TRUE)
	{
		m_MdiTabOwner.HideMDITabsWhenMDIChildNotMaximized(bHideMDITabsWhenMDIChildNotMaximized);
	}

	void SetDrawFlat(bool bDrawFlat = true)
	{
		if(m_bDrawFlat!=bDrawFlat)
		{
			//ATLASSERT((m_hWnd==NULL) && "Please call SetDrawFlat before CreateWindow or SubclassWindow");
			m_bDrawFlat = bDrawFlat;
			if(m_bDrawFlat)
			{
				m_MdiTabOwner.ModifyTabStyles(0,CTCS_FLATEDGE);
			}
			else
			{
				m_MdiTabOwner.ModifyTabStyles(CTCS_FLATEDGE,0);
			}
		}
	}

	bool GetDrawFlat(void) const
	{
		return m_bDrawFlat;
	}

#ifdef __TabbedMDISave_h__

	bool SaveAllModified(bool canPrompt, bool canCancel) const
	{
		if(canPrompt)
		{
			// Prompt using our "Save modified" dialog
			ATL::CComPtr<ITabbedMDIChildModifiedList> modifiedItems;
			this->FindModified(&modifiedItems);
			if(modifiedItems)
			{
				long modifiedCount = 0;
				modifiedItems->get_Count(&modifiedCount);
				if(modifiedCount > 0)
				{
					CSaveModifiedItemsDialog dialog(modifiedItems, canCancel);

					INT_PTR response = dialog.DoModal();
					if(response == IDYES)
					{
						// The dialog will update the list and remove
						// any items that the user unchecked

						this->SaveModified(modifiedItems);
					}
					else if(response == IDCANCEL)
					{
						// Not safe to close
						return false;
					}
				}
			}
		}
		else
		{
			// Save all files, but don't ask permission.

			HWND hWndChild = ::GetTopWindow(m_hWnd);
			while(hWndChild != NULL)
			{
				::SendMessage(hWndChild, UWM_MDICHILDSAVEMODIFIED, 0, 0);
				hWndChild = ::GetNextWindow(hWndChild, GW_HWNDNEXT);
			}
		}

		// Safe to terminate the application if desired
		return true;
	}

	HRESULT FindModified(ITabbedMDIChildModifiedList** modifiedItemsOut) const
	{
		WTL::CWaitCursor	waitCursor;

		if(modifiedItemsOut == NULL)
		{
			return E_POINTER;
		}
		*modifiedItemsOut = NULL;

		long modifiedCount = 0;

		HRESULT hr = S_OK;

		// Build up a list of all the modified documents
		ATL::CComPtr<ITabbedMDIChildModifiedList> modifiedItems;
		::CreateTabbedMDIChildModifiedList(&modifiedItems);

		if(modifiedItems)
		{
			HWND hWndChild = ::GetTopWindow(m_hWnd);
			while(hWndChild != NULL)
			{
				_CSTRING_NS::CString windowText;
				int cchWindowText = ::GetWindowTextLength(hWndChild);
				LPTSTR pszText = windowText.GetBuffer(cchWindowText+1);
				cchWindowText = ::GetWindowText(hWndChild, pszText, cchWindowText+1);
				windowText.ReleaseBuffer(cchWindowText);

				CComBSTR defaultName(windowText);

				ATL::CComPtr<ITabbedMDIChildModifiedItem> modifiedItem;
				::CreateTabbedMDIChildModifiedItem(hWndChild,
					defaultName, defaultName, defaultName, 0, NULL, &modifiedItem);

				BOOL bIsModified = (BOOL)::SendMessage(hWndChild, UWM_MDICHILDISMODIFIED, 0, (LPARAM)modifiedItem.p);
				if(bIsModified)
				{
					++modifiedCount;

					modifiedItems->Insert(-1, modifiedItem);
				}

				hWndChild = ::GetNextWindow(hWndChild, GW_HWNDNEXT);
			}

			if(modifiedCount > 0)
			{
				modifiedItems.CopyTo(modifiedItemsOut);
			}
		}

		return hr;
	}

	HRESULT SaveModified(ITabbedMDIChildModifiedList* modifiedItems) const
	{
		if(modifiedItems == NULL)
		{
			return E_INVALIDARG;
		}

		WTL::CWaitCursor waitCursor;

		HRESULT hr = S_OK;

		long count = 0;
		modifiedItems->get_Count(&count);
		for(long i=0; i<count; ++i)
		{
			ATL::CComPtr<ITabbedMDIChildModifiedItem> modifiedItem;
			modifiedItems->get_Item(i, &modifiedItem);
			if(modifiedItem)
			{
				HWND hWnd = NULL;
				modifiedItem->get_Window(&hWnd);
				if(hWnd && ::IsWindow(hWnd))
				{
					::SendMessage(hWnd, UWM_MDICHILDSAVEMODIFIED, 0, (LPARAM)modifiedItem.p);
				}

				// Important!  If an item has sub-items, the "top-level"
				//  item is responsible for ensuring that modifications
				//  are saved.
			}
		}

		return hr;
	}
	
#endif // __TabbedMDISave_h__

	void CloseAll(bool bPreferNoPrompt = false) const
	{
		HWND hWndChild = ::GetTopWindow(m_hWnd);
		while(hWndChild != NULL)
		{
			HWND hWndClose = hWndChild;
			hWndChild = ::GetNextWindow(hWndChild, GW_HWNDNEXT);

			if(bPreferNoPrompt)
			{
				::SendMessage(hWndClose, UWM_MDICHILDCLOSEWITHNOPROMPT, 0, 0L);
			}

			if(::IsWindow(hWndClose))
			{
				// The window doesn't support UWM_MDICHILDCLOSEWITHNOPROMPT
				// or the caller didn't want to close with no prompt,
				// so we'll send a close message it should understand.
				::SendMessage(hWndClose, WM_SYSCOMMAND, SC_CLOSE, 0L);
			}
		}
	}

	BOOL SubclassWindow(HWND hWnd)
	{
		BOOL bSuccess = baseClass::SubclassWindow(hWnd);

		m_bSubclassed = true;

		this->InitTabs();

		m_MdiTabOwner.CalcTabAreaHeight();

		return bSuccess;
	}

	HWND UnsubclassWindow(BOOL bForce = FALSE)
	{
		m_bSubclassed = false;

		return baseClass::UnsubclassWindow(bForce);
	}

protected:
	void InitTabs()
	{
		if( !m_MdiTabOwner.IsWindow() )
		{
			if(m_hWndTabOwnerParent == NULL)
			{
				// If the tab owner's parent is not specified,
				// have the tabs as a sibling
				m_hWndTabOwnerParent = this->GetParent();
			}

			m_MdiTabOwner.Create(
				m_hWndTabOwnerParent, 
				rcDefault, NULL,
				WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN // start out not visible
				);

			m_MdiTabOwner.SetMDIClient(m_hWnd);
		}
	}

// Message Handling
public:
	DECLARE_WND_SUPERCLASS(_T("TabbedMDIClient"), _T("MDIClient"))

	BEGIN_MSG_MAP(CTabbedMDIClient)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_SETTINGCHANGE, OnSettingChange)
		MESSAGE_HANDLER(WM_WINDOWPOSCHANGING, OnWindowPosChanging)
		MESSAGE_HANDLER(WM_NCPAINT, OnNcPaint)
		//MESSAGE_HANDLER(WM_MDICREATE, OnMDICreate)
		MESSAGE_HANDLER(WM_MDIDESTROY, OnMDIDestroy)
		MESSAGE_HANDLER(UWM_MDICHILDACTIVATIONCHANGE, OnChildActivationChange)
		MESSAGE_HANDLER(UWM_MDICHILDTABTEXTCHANGE, OnChildTabTextChange)
		MESSAGE_HANDLER(UWM_MDICHILDTABTOOLTIPCHANGE, OnChildTabToolTipChange)
		MESSAGE_HANDLER(UWM_MDICHILDMAXIMIZED, OnChildMaximized)
		MESSAGE_HANDLER(UWM_MDICHILDUNMAXIMIZED, OnChildUnMaximized)
	END_MSG_MAP()

	LRESULT OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	{
		// "base::OnCreate()"
		LRESULT lRet = this->DefWindowProc(uMsg, wParam, lParam);
		bHandled = TRUE;
		if(lRet == -1)
		{
			return -1;
		}

		this->InitTabs();

		m_MdiTabOwner.CalcTabAreaHeight();

		return lRet;
	}

	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
	{
		// Say that we didn't handle it so that anyone else
		//  interested gets to handle the message
		bHandled = FALSE;
		return 0;
	}

	LRESULT OnSettingChange(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	{
		// Be sure tab gets message before we recalculate the tab area height
		//  so that it can adjust its font metrics first.
		// NOTE: This causes the tab to get the WM_SETTINGCHANGE message twice,
		//  but that's OK.
		m_MdiTabOwner.GetTabCtrl().SendMessage(uMsg, wParam, lParam);

		m_MdiTabOwner.CalcTabAreaHeight();

		bHandled = FALSE;
		return 0;
	}

	LRESULT OnWindowPosChanging(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	{
		LPWINDOWPOS pWinPos = reinterpret_cast<LPWINDOWPOS>(lParam);
		if(pWinPos)
		{
			if( m_MdiTabOwner.IsWindow() )
			{
				//ATLTRACE(_T("Resizing MDI tab and MDI client\n"));
				int nTabAreaHeight = (m_MdiTabOwner.IsWindowVisible()) ? m_MdiTabOwner.GetTabAreaHeight() : 0;

				TTabCtrl& TabCtrl = m_MdiTabOwner.GetTabCtrl();
				DWORD dwStyle = TabCtrl.GetStyle();
				if(CTCS_BOTTOM == (dwStyle & CTCS_BOTTOM))
				{
					m_MdiTabOwner.SetWindowPos(
						NULL,
						pWinPos->x, pWinPos->y + (pWinPos->cy - nTabAreaHeight),
						pWinPos->cx, nTabAreaHeight,
						(pWinPos->flags & SWP_NOMOVE) | (pWinPos->flags & SWP_NOSIZE) | SWP_NOZORDER | SWP_NOACTIVATE);

					if((pWinPos->flags & SWP_NOSIZE) == 0)
					{
						pWinPos->cy -= nTabAreaHeight;
					}
				}
				else
				{
					m_MdiTabOwner.SetWindowPos(
						NULL,
						pWinPos->x, pWinPos->y,
						pWinPos->cx, nTabAreaHeight,
						(pWinPos->flags & SWP_NOMOVE) | (pWinPos->flags & SWP_NOSIZE) | SWP_NOZORDER | SWP_NOACTIVATE);

					if((pWinPos->flags & SWP_NOMOVE) == 0)
					{
						pWinPos->y += nTabAreaHeight;
					}
					if((pWinPos->flags & SWP_NOSIZE) == 0)
					{
						pWinPos->cy -= nTabAreaHeight;
					}
				}
			}
		}

		// "base::OnWindowPosChanging()"
		LRESULT lRet = this->DefWindowProc(uMsg, wParam, lParam);
		bHandled = TRUE;

		return lRet;
	}

	LRESULT OnNcPaint(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
	{

		if(m_bDrawFlat &&
			WS_EX_CLIENTEDGE == (this->GetExStyle() & WS_EX_CLIENTEDGE))
		{
			// When we have WS_EX_CLIENTEDGE and drawing "flat",
			// we'll paint the non-client edges ourself with a more flat look.
			// NOTE: If WS_EX_CLIENTEDGE ever takes up more than 2 pixels
			// on each edge, update the drawing code.

			WTL::CWindowDC dc(this->m_hWnd);
			if(dc)
			{
				RECT rcWindow;
				this->GetWindowRect(&rcWindow);
				::OffsetRect(&rcWindow, -rcWindow.left, -rcWindow.top);
				dc.DrawEdge(&rcWindow, EDGE_ETCHED, BF_FLAT|BF_RECT);
			}

			/*
			// Note: The documentation says the flags should be
			// DCX_WINDOW|DCX_INTERSECTRGN
			// but that wasn't working.
			// On http://freespace.virgin.net/james.brown7/tutorials/tips.htm
			// they mention you also need to OR in the flag "0x10000".
			CDCHandle dc = this->GetDCEx((HRGN)wParam, DCX_WINDOW|DCX_INTERSECTRGN | 0x10000));
			if(!dc.IsNull())
			{
				RECT rcWindow;
				this->GetWindowRect(&rcWindow);
				::OffsetRect(&rcWindow, -rcWindow.left, -rcWindow.top);
				dc.DrawEdge(&rcWindow, EDGE_ETCHED, BF_FLAT|BF_RECT);

				::ReleaseDC(dc);
			}
			*/
			bHandled = TRUE;
		}
		else
		{
			bHandled = FALSE;
		}

		return 0;
	}

	LRESULT OnMDIDestroy(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	{
		// NOTE: For symmetry, we could try to handle WM_MDICREATE
		//  for tab creation.  There are 2 reasons we don't:
		//   1. With WTL, the implementation doesn't use WM_MDICREATE,
		//      so it wouldn't be reliable to rely on it
		//   2. We don't need to.  Handling the change in child
		//      activation to display the tab, if its not there,
		//      creates, DisplayTab will create the corresponding tab. 

		// Remove the tab for the child being destroyed.
		//  Before removing the tab, we want the default happen,
		//  so that the MDI Client figures out who to activate next.
		//  If we removed the tab first, the "DeleteItem" on
		//  the tab wouldn't know the next best tab to select,
		//  and so if it's removing the tab that's currently selected,
		//  it changes the selection to 0.
		//  But by having the MDI client activate the child
		//  "next in line" (Z-Order), that child will be activated,
		//  and its tab will be selected, so that by the time we remove
		//  the tab for this child, the tab won't be selected.
		LRESULT lRet = this->DefWindowProc(uMsg, wParam, lParam);
		bHandled = TRUE;

		if(wParam != NULL)
		{
			m_MdiTabOwner.RemoveTab((HWND)wParam);
		}

		return lRet;
	}

	LRESULT OnChildActivationChange(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	{
		// NOTE: We'd like to just handle WM_MDIACTIVATE when sent to
		//  the MDI client to know when the active MDI child has changed.
		//  Unfortunately, you don't HAVE to send WM_MDIACTIVATE to the
		//  MDI Client to change the active child (such as clicking
		//  a different MDI child to activate it).
		//  However, the MDI *child* will *always* receive WM_MDIACTIVATE.
		//  In fact, the child losing focus gets WM_MDIACTIVATE, 
		//  and then the child gaining focus gets WM_MDIACTIVATE
		//  (the "deactivating" and "activating" windows are sent as
		//  the WPARAM and LPARAM both times).
		//  So we'll make the "activating" child window responsible for
		//  sending us a message (UWM_MDICHILDACTIVATIONCHANGE)
		//  when it gets a WM_MDIACTIVATE message.  We'll use this
		//  to display the tab (switching the selected tab to the
		//  corresponding tab, or creating a tab for the child and
		//  setting it as selected)

		if(wParam != NULL)
		{
			m_MdiTabOwner.DisplayTab((HWND)wParam, TRUE, m_bUseMDIChildIcon);
		}

		return 0;
	}

	LRESULT OnChildTabTextChange(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
	{
		// NOTE: Our direct children are Frame windows.
		//  We make the MDI child responsible for sending us a
		//  message (UWM_MDICHILDTABTEXTCHANGE) if they want to
		//  update the text of the corresponding tab.

		if(wParam != NULL)
		{
			m_MdiTabOwner.UpdateTabText((HWND)wParam, (LPCTSTR)lParam);
		}

		return 0;
	}

	LRESULT OnChildTabToolTipChange(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
	{
		//  We make the MDI child responsible for sending us a
		//  message (UWM_MDICHILDTABTOOLTIPCHANGE) with the tooltip
		//  if that tooltip is something different than the tab text.

		if(wParam != NULL)
		{
			m_MdiTabOwner.UpdateTabToolTip((HWND)wParam, (LPCTSTR)lParam);
		}

		return 0;
	}

	LRESULT OnChildMaximized(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
	{
		HWND hWndMaximized = (HWND)wParam;
		HWND hWndActiveChild = (HWND)this->SendMessage(WM_MDIGETACTIVE, 0, 0);
		if(hWndMaximized == hWndActiveChild)
		{
			m_MdiTabOwner.ShowTabControlIfChildMaximized();
		}
		bHandled = FALSE;
		return 0;
	}

	LRESULT OnChildUnMaximized(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
	{
		HWND hWndUnMaximized = (HWND)wParam;
		HWND hWndActiveChild = (HWND)this->SendMessage(WM_MDIGETACTIVE, 0, 0);
		if(hWndUnMaximized == hWndActiveChild)
		{
			m_MdiTabOwner.HideTabControlIfChildNotMaximized();
		}
		bHandled = FALSE;
		return 0;
	}
};


template <class T, class TBase = CCommandBarCtrlBase, class TWinTraits = ATL::CControlWinTraits>
class ATL_NO_VTABLE CTabbedMDICommandBarCtrlImpl : public WTL::CMDICommandBarCtrlImpl<T, TBase, TWinTraits>
{
protected:
	typedef CTabbedMDICommandBarCtrlImpl thisClass;
	typedef CMDICommandBarCtrlImpl<T, TBase, TWinTraits> baseClass;
	typedef CCommandBarCtrlImpl<T, TBase, TWinTraits> grandparentClass;

// Extended data
protected:
	bool m_bUseMaxChildDocIconAndFrameCaptionButtons:1;

// Constructors
public:
	CTabbedMDICommandBarCtrlImpl() :
		m_bUseMaxChildDocIconAndFrameCaptionButtons(true)
	{
	}

// Public methods
public:
	void UseMaxChildDocIconAndFrameCaptionButtons(bool bUseMaxChildDocIconAndFrameCaptionButtons = true)
	{
		m_bUseMaxChildDocIconAndFrameCaptionButtons = bUseMaxChildDocIconAndFrameCaptionButtons;
	}

// Overrides
public:
	void GetSystemSettings()
	{
		baseClass::GetSystemSettings();
		m_cxLeft += 4;
	}

// Message Handling
public:
	BEGIN_MSG_MAP(thisClass)

		if(m_bUseMaxChildDocIconAndFrameCaptionButtons)
		{
			CHAIN_MSG_MAP(baseClass)
		}
		else
		{
			CHAIN_MSG_MAP(grandparentClass)
		}

	ALT_MSG_MAP(1)		// Parent window messages

		if(m_bUseMaxChildDocIconAndFrameCaptionButtons)
		{
			CHAIN_MSG_MAP_ALT(baseClass, 1)
		}
		else
		{
			CHAIN_MSG_MAP_ALT(grandparentClass, 1)
		}

	ALT_MSG_MAP(2)		// MDI client window messages

		MESSAGE_HANDLER(WM_MDISETMENU, OnMDISetMenu)

		if(m_bUseMaxChildDocIconAndFrameCaptionButtons)
		{
			MESSAGE_HANDLER(WM_MDIDESTROY, OnMDIDestroy)
			MESSAGE_HANDLER(UWM_MDICHILDMAXIMIZED, OnChildMaximized)
			MESSAGE_HANDLER(UWM_MDICHILDUNMAXIMIZED, OnChildUnMaximized)
		}
		// NOTE: If future implementations of CMDICommandBarCtrlImpl
		//  add MDI client related handlers for ALT_MSG_MAP(2),
		//  either add them here or chain to the base
	ALT_MSG_MAP(3)		// Message hook messages

		// NOTE: We don't want to depend on the command bar's
		//  hooked messages for telling us about maximization
		//  changes.  We can do the job with the normal
		//  MDI messages and our special MDI messages
		//MESSAGE_RANGE_HANDLER(0, 0xFFFF, OnAllHookMessages)
		CHAIN_MSG_MAP_ALT(grandparentClass, 3)

	END_MSG_MAP()

// MDI client window message handlers
	LRESULT OnMDISetMenu(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
	{
		m_wndMDIClient.DefWindowProc(uMsg, NULL, lParam);
		HMENU hOldMenu = GetMenu();
		BOOL bRet = AttachMenu((HMENU)wParam);
		bRet;   // avoid level 4 warning
		ATLASSERT(bRet);

#if (_WTL_VER >= 0x0710) && (_WIN32_IE >= 0x0400)
		T* pT = static_cast<T*>(this);
		pT->UpdateRebarBandIdealSize();
#endif //(_WTL_VER >= 0x0710) && (_WIN32_IE >= 0x0400)

		return (LRESULT)hOldMenu;
	}

	LRESULT OnMDIDestroy(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
	{
		HWND hWndChild = (HWND)wParam;

		if(m_hWndChildMaximized == hWndChild)
		{
			bool bMaxOld = m_bChildMaximized;
			HICON hIconOld = m_hIconChildMaximized;

			m_bChildMaximized = false;
			m_hWndChildMaximized = NULL;
			m_hIconChildMaximized = NULL;

			T* pT = static_cast<T*>(this);
			pT->RefreshMaximizedState((bMaxOld != m_bChildMaximized), (hIconOld != m_hIconChildMaximized));
		}

		bHandled = FALSE;
		return 0;
	}

	LRESULT OnChildMaximized(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
	{
		HWND hWndChild = (HWND)wParam;

		bool bMaxOld = m_bChildMaximized;
		HICON hIconOld = m_hIconChildMaximized;

		m_bChildMaximized = true;

		if(m_hWndChildMaximized != hWndChild)
		{
			ATL::CWindow wnd = m_hWndChildMaximized = hWndChild;
			m_hIconChildMaximized = wnd.GetIcon(FALSE);
			if(m_hIconChildMaximized == NULL)	// no icon set with WM_SETICON, get the class one
			{
// need conditional code because types don't match in winuser.h
#ifdef _WIN64
				m_hIconChildMaximized = (HICON)::GetClassLongPtr(wnd, GCLP_HICONSM);
				if(m_hIconChildMaximized == NULL)
				{
					m_hIconChildMaximized = (HICON) ::GetClassLongPtr(wnd, GCLP_HICON);
				}
#else
				m_hIconChildMaximized = (HICON)LongToHandle(::GetClassLongPtr(wnd, GCLP_HICONSM));
				if(m_hIconChildMaximized == NULL)
				{
					m_hIconChildMaximized = (HICON) LongToHandle(::GetClassLongPtr(wnd, GCLP_HICON));
				}
#endif
			}
		}

		T* pT = static_cast<T*>(this);
		pT->RefreshMaximizedState((bMaxOld != m_bChildMaximized), (hIconOld != m_hIconChildMaximized));

		bHandled = FALSE;
		return 0;
	}

	LRESULT OnChildUnMaximized(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
	{
		HWND hWndChild = (HWND)wParam;

		if(m_hWndChildMaximized == hWndChild)
		{
			bool bMaxOld = m_bChildMaximized;
			HICON hIconOld = m_hIconChildMaximized;

			m_bChildMaximized = false;
			m_hWndChildMaximized = NULL;
			m_hIconChildMaximized = NULL;

			T* pT = static_cast<T*>(this);
			pT->RefreshMaximizedState((bMaxOld != m_bChildMaximized), (hIconOld != m_hIconChildMaximized));
		}

		bHandled = FALSE;
		return 0;
	}

	void RefreshMaximizedState(bool bMaximizeChanged, bool bIconChanged)
	{
		// NOTE: This code comes out of CMDICommandBarCtrlImpl::OnAllHookMessages.
		//  If the base implementation changes, reflect those changes here.
		if(bMaximizeChanged)
		{
#ifdef _CMDBAR_EXTRA_TRACE
			ATLTRACE2(atlTraceUI, 0, "MDI CmdBar - All messages hook change: m_bChildMaximized = %s\n", m_bChildMaximized ? "true" : "false");
#endif
			// assuming we are in a rebar, change our size to accomodate new state
			// we hope that if we are not in a rebar, nCount will be 0
			int nCount = (int)::SendMessage(GetParent(), RB_GETBANDCOUNT, 0, 0L);
			int cxDiff = (m_bChildMaximized ? 1 : -1) * (m_cxLeft + m_cxRight);
			for(int i = 0; i < nCount; i++)
			{ 
#if (_WIN32_IE >= 0x0500)
				REBARBANDINFO rbi = { sizeof(REBARBANDINFO), RBBIM_CHILD | RBBIM_CHILDSIZE | RBBIM_IDEALSIZE | RBBIM_STYLE };
				::SendMessage(GetParent(), RB_GETBANDINFO, i, (LPARAM)&rbi);
				if(rbi.hwndChild == m_hWnd)
				{
					if((rbi.fStyle & RBBS_USECHEVRON) != 0)
					{
						rbi.fMask = RBBIM_CHILDSIZE | RBBIM_IDEALSIZE;
						rbi.cxMinChild += cxDiff;
						rbi.cxIdeal += cxDiff;
						::SendMessage(GetParent(), RB_SETBANDINFO, i, (LPARAM)&rbi);
					}
					break;
				}
#elif (_WIN32_IE >= 0x0400)
				REBARBANDINFO rbi = { sizeof(REBARBANDINFO), RBBIM_CHILD | RBBIM_CHILDSIZE | RBBIM_IDEALSIZE };
				::SendMessage(GetParent(), RB_GETBANDINFO, i, (LPARAM)&rbi);
				if(rbi.hwndChild == m_hWnd)
				{
					rbi.fMask = RBBIM_CHILDSIZE | RBBIM_IDEALSIZE;
					rbi.cxMinChild += cxDiff;
					rbi.cxIdeal += cxDiff;
					::SendMessage(GetParent(), RB_SETBANDINFO, i, (LPARAM)&rbi);
					break;
				}
#else //(_WIN32_IE < 0x0400)
				REBARBANDINFO rbi = { sizeof(REBARBANDINFO), RBBIM_CHILD | RBBIM_CHILDSIZE };
				::SendMessage(GetParent(), RB_GETBANDINFO, i, (LPARAM)&rbi);
				if(rbi.hwndChild == m_hWnd)
				{
					rbi.fMask = RBBIM_CHILDSIZE;
					rbi.cxMinChild += cxDiff;
					::SendMessage(GetParent(), RB_SETBANDINFO, i, (LPARAM)&rbi);
					break;
				}
#endif //!(_WIN32_IE >= 0x0500)
			}
		}

		if(bMaximizeChanged || bIconChanged)
		{
			// force size change and redraw everything
			RECT rect = { 0 };
			GetWindowRect(&rect);
			::MapWindowPoints(NULL, GetParent(), (LPPOINT)&rect, 2);
			SetRedraw(FALSE);
			SetWindowPos(NULL, 0, 0, 1, 1, SWP_NOZORDER | SWP_NOMOVE);
			SetWindowPos(NULL, &rect, SWP_NOZORDER | SWP_NOMOVE);
			SetRedraw(TRUE);
			RedrawWindow(NULL, NULL, RDW_FRAME | RDW_INVALIDATE | RDW_UPDATENOW);
		}
	}
};

class CTabbedMDICommandBarCtrl : public CTabbedMDICommandBarCtrlImpl<CTabbedMDICommandBarCtrl>
{
public:
	DECLARE_WND_SUPERCLASS(_T("WTL_TabbedMDICommandBar"), GetWndClassName())
};

/*
class CAnotherTabbedMDICommandBarCtrl : public CTabbedMDICommandBarCtrlImpl<CAnotherTabbedMDICommandBarCtrl>
{
protected:
	typedef CAnotherTabbedMDICommandBarCtrl thisClass;
	typedef CTabbedMDICommandBarCtrlImpl<CAnotherTabbedMDICommandBarCtrl> baseClass;

public:
	DECLARE_WND_SUPERCLASS(_T("WTL_AnotherTabbedMDICommandBar"), GetWndClassName())

	BEGIN_MSG_MAP(thisClass)
		CHAIN_MSG_MAP(baseClass)

	ALT_MSG_MAP(1)		// Parent window messages
		CHAIN_MSG_MAP_ALT(baseClass, 1)

	ALT_MSG_MAP(2)		// MDI client window messages
		MESSAGE_HANDLER(WM_MDISETMENU, OnMDISetMenu)
		CHAIN_MSG_MAP_ALT(baseClass, 2)

	ALT_MSG_MAP(3)		// Message hook messages
		CHAIN_MSG_MAP_ALT(baseClass, 3)
	END_MSG_MAP()

	LRESULT OnMDISetMenu(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	{
		bHandled = TRUE;
		m_wndMDIClient.DefWindowProc(uMsg, NULL, lParam);
		HMENU hOldMenu = GetMenu();
		BOOL bRet = AttachMenu((HMENU)wParam);

		// Other stuff

		bRet;
		ATLASSERT(bRet);
		return (LRESULT)hOldMenu;
	}
};
*/

#endif // __WTL_TABBED_MDI_H__
