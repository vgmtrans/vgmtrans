#ifndef __WTL_TABBED_MDI_MESSAGES_H__
#define __WTL_TABBED_MDI_MESSAGES_H__

#pragma once

#define UWM_MDICHILDTABTEXTCHANGE_MSG      _T("UWM_MDICHILDTABTEXTCHANGE_MSG-5DAD28E1-C961-11d5-8BDA-00500477589F")
#define UWM_MDICHILDTABTOOLTIPCHANGE_MSG   _T("UWM_MDICHILDTABTOOLTIPCHANGE_MSG-5DAD28E3-C961-11d5-8BDA-00500477589F")
#define UWM_MDICHILDACTIVATIONCHANGE_MSG   _T("UWM_MDICHILDACTIVATIONCHANGE_MSG-5DAD28E5-C961-11d5-8BDA-00500477589F")
#define UWM_MDICHILDMAXIMIZED_MSG          _T("UWM_MDICHILDMAXIMIZED_MSG-5DAD28E7-C961-11d5-8BDA-00500477589F")
#define UWM_MDICHILDUNMAXIMIZED_MSG        _T("UWM_MDICHILDUNMAXIMIZED_MSG-5DAD28E9-C961-11d5-8BDA-00500477589F")
#define UWM_MDICHILDSHOWTABCONTEXTMENU_MSG _T("UWM_MDICHILDSHOWTABCONTEXTMENU_MSG-5DAD28EB-C961-11d5-8BDA-00500477589F")
#define UWM_MDICHILDSAVEMODIFIED_MSG       _T("UWM_MDICHILDSAVEMODIFIED_MSG-5DAD28EC-C961-11d5-8BDA-00500477589F")
#define UWM_MDICHILDISMODIFIED_MSG         _T("UWM_MDICHILDISMODIFIED_MSG-5DAD28EC-C961-11d5-8BDA-00500477589F")
#define UWM_MDICHILDCLOSEWITHNOPROMPT_MSG  _T("UWM_MDICHILDCLOSEWITHNOPROMPT_MSG-5DAD28EC-C961-11d5-8BDA-00500477589F")

//-----------------------------------------------------------------------------
// UWM_MDICHILDTABTEXTCHANGE
//
// Used to set the tab text for the MDI tab corresponding to an MDI child frame.
// Sent to the MDIClient window (subclassed by CTabbedMDIClient).
//
// WPARAM: HWND of MDI child frame
// LPARAM: LPCTSTR with new tab text
//-----------------------------------------------------------------------------
#if (_MSC_VER >= 1300)
__declspec(selectany) UINT UWM_MDICHILDTABTEXTCHANGE = ::RegisterWindowMessage(UWM_MDICHILDTABTEXTCHANGE_MSG);
#else
// Initialize via RegisterTabbedMDIMessages
__declspec(selectany) UINT UWM_MDICHILDTABTEXTCHANGE = 0;
#endif

//-----------------------------------------------------------------------------
// UWM_MDICHILDTABTOOLTIPCHANGE
//
// Used to set the tab tooltip for the MDI tab corresponding to an MDI child frame.
// Sent to the MDIClient window (subclassed by CTabbedMDIClient).
//
// WPARAM: HWND of MDI child frame
// LPARAM: LPCTSTR with new tab tooltip
//-----------------------------------------------------------------------------
#if (_MSC_VER >= 1300)
__declspec(selectany) UINT UWM_MDICHILDTABTOOLTIPCHANGE = ::RegisterWindowMessage(UWM_MDICHILDTABTOOLTIPCHANGE_MSG);
#else
// Initialize via RegisterTabbedMDIMessages
__declspec(selectany) UINT UWM_MDICHILDTABTOOLTIPCHANGE = 0;
#endif

//-----------------------------------------------------------------------------
// UWM_MDICHILDACTIVATIONCHANGE
//
// Used to notify the MDIClient window that an MDI child frame has become active.
// The corresponding tab is then activated to keep things synchronized.
// Sent to the MDIClient window (subclassed by CTabbedMDIClient).
//
// WPARAM: HWND of MDI child frame being activated
// LPARAM: unused
//-----------------------------------------------------------------------------
#if (_MSC_VER >= 1300)
__declspec(selectany) UINT UWM_MDICHILDACTIVATIONCHANGE = ::RegisterWindowMessage(UWM_MDICHILDACTIVATIONCHANGE_MSG);
#else
// Initialize via RegisterTabbedMDIMessages
__declspec(selectany) UINT UWM_MDICHILDACTIVATIONCHANGE = 0;
#endif

//-----------------------------------------------------------------------------
// UWM_MDICHILDMAXIMIZED
//
// Used to notify the MDIClient window that an MDI child frame has been maximized.
// Sent to the MDIClient window (subclassed by CTabbedMDIClient).
//
// WPARAM: HWND of MDI child frame being maximized
// LPARAM: Child frame client area
//          LOWORD = new width of child frame client area
//          HIWORD = new height of child frame client area
//-----------------------------------------------------------------------------
#if (_MSC_VER >= 1300)
__declspec(selectany) UINT UWM_MDICHILDMAXIMIZED = ::RegisterWindowMessage(UWM_MDICHILDMAXIMIZED_MSG);
#else
// Initialize via RegisterTabbedMDIMessages
__declspec(selectany) UINT UWM_MDICHILDMAXIMIZED = 0;
#endif

//-----------------------------------------------------------------------------
// UWM_MDICHILDUNMAXIMIZED
//
// Used to notify the MDIClient window that an MDI child frame has been unmaximized.
// Sent to the MDIClient window (subclassed by CTabbedMDIClient).
//
// WPARAM: HWND of MDI child frame being unmaximized
// LPARAM: Child frame client area
//          LOWORD = new width of child frame client area
//          HIWORD = new height of child frame client area
//-----------------------------------------------------------------------------
#if (_MSC_VER >= 1300)
__declspec(selectany) UINT UWM_MDICHILDUNMAXIMIZED = ::RegisterWindowMessage(UWM_MDICHILDUNMAXIMIZED_MSG);
#else
// Initialize via RegisterTabbedMDIMessages
__declspec(selectany) UINT UWM_MDICHILDUNMAXIMIZED = 0;
#endif

//-----------------------------------------------------------------------------
// UWM_MDICHILDSHOWTABCONTEXTMENU
//
// Sent to an MDI child frame (derived from CTabbedMDIChildWindowImpl)
// to show a context menu when the user right clicks on a tab (or
// presses Shift+F10 while the tab control or owner has focus).
// The context menu defaults to showing the "system menu" for the window.
// Derived classes can handle this message to display a custom context menu instead.
//
// WPARAM: HWND of the window in which the user right clicked the mouse.
// LPARAM: Position of the cursor at the time of the mouse click,
//            in screen coordinates (if the mouse was not used, the values
//            are adjusted to be for the selected item)
//          LOWORD = Horizontal position of the cursor in screen coordinates
//          HIWORD = Vertical position of the cursor in screen coordinates
//-----------------------------------------------------------------------------
#if (_MSC_VER >= 1300)
__declspec(selectany) UINT UWM_MDICHILDSHOWTABCONTEXTMENU = ::RegisterWindowMessage(UWM_MDICHILDSHOWTABCONTEXTMENU_MSG);
#else
// Initialize via RegisterTabbedMDIMessages
__declspec(selectany) UINT UWM_MDICHILDSHOWTABCONTEXTMENU = 0;
#endif

//-----------------------------------------------------------------------------
// UWM_MDICHILDSAVEMODIFIED
//
// Sent to an MDI child frame (derived from CTabbedMDIChildWindowImpl)
// to tell the child to save any modifications without asking.
//
// WPARAM: reserved
// LPARAM: ITabbedMDIChildModifiedItem* with additional information.
//-----------------------------------------------------------------------------
#if (_MSC_VER >= 1300)
__declspec(selectany) UINT UWM_MDICHILDSAVEMODIFIED = ::RegisterWindowMessage(UWM_MDICHILDSAVEMODIFIED_MSG);
#else
// Initialize via RegisterTabbedMDIMessages
__declspec(selectany) UINT UWM_MDICHILDSAVEMODIFIED = 0;
#endif

//-----------------------------------------------------------------------------
// UWM_MDICHILDISMODIFIED
//
// Sent to an MDI child frame (derived from CTabbedMDIChildWindowImpl)
// to ask whether the document(s) referenced by the child has been modified.
// If so, the LPARAM has an ITabbedMDIChildModifiedItem* with which to fill
// out information. Return TRUE if there are one or more modified documents for the child.
//
// WPARAM: reserved
// LPARAM: ITabbedMDIChildModifiedItem* that can be filled out with information
//         about what has been modified.
//
// Return value: TRUE if modified, FALSE if not.
//-----------------------------------------------------------------------------
#if (_MSC_VER >= 1300)
__declspec(selectany) UINT UWM_MDICHILDISMODIFIED = ::RegisterWindowMessage(UWM_MDICHILDISMODIFIED_MSG);
#else
// Initialize via RegisterTabbedMDIMessages
__declspec(selectany) UINT UWM_MDICHILDISMODIFIED = 0;
#endif

//-----------------------------------------------------------------------------
// UWM_MDICHILDCLOSEWITHNOPROMPT
//
// Sent to an MDI child frame (derived from CTabbedMDIChildWindowImpl)
// to close the tabbed MDI child bypassing WM_CLOSE (so that the user isn't
// prompted to save modifications - for when they've already been asked).
//
// WPARAM: unused
// LPARAM: unused
//-----------------------------------------------------------------------------
#if (_MSC_VER >= 1300)
__declspec(selectany) UINT UWM_MDICHILDCLOSEWITHNOPROMPT = ::RegisterWindowMessage(UWM_MDICHILDCLOSEWITHNOPROMPT_MSG);
#else
// Initialize via RegisterTabbedMDIMessages
__declspec(selectany) UINT UWM_MDICHILDCLOSEWITHNOPROMPT = 0;
#endif



// NOTE: For VC 7.0+, we could simply have each one initialize on its own, like:
//
//   __declspec(selectany) UINT UWM_MDICHILDTABTEXTCHANGE = ::RegisterWindowMessage(UWM_MDICHILDTABTEXTCHANGE_MSG);
//
//  However, VC 6.0 doesn't like that.  So that this works on VC 6.0 as well,
//  we'll do the RegisterWindowMessage in the constructor of a structure
//  who we'll declare globally.  It's important to note that doing
//   __declspec(selectany) RegisterTabbedMDIMessages g_RegisterTabbedMDIMessages;
//  will work in debug (and run the constructor), but not work in release.

struct RegisterTabbedMDIMessages
{
	RegisterTabbedMDIMessages()
	{
		UWM_MDICHILDTABTEXTCHANGE =      ::RegisterWindowMessage(UWM_MDICHILDTABTEXTCHANGE_MSG);
		UWM_MDICHILDTABTOOLTIPCHANGE =   ::RegisterWindowMessage(UWM_MDICHILDTABTOOLTIPCHANGE_MSG);
		UWM_MDICHILDACTIVATIONCHANGE =   ::RegisterWindowMessage(UWM_MDICHILDACTIVATIONCHANGE_MSG);
		UWM_MDICHILDMAXIMIZED =          ::RegisterWindowMessage(UWM_MDICHILDMAXIMIZED_MSG);
		UWM_MDICHILDUNMAXIMIZED =        ::RegisterWindowMessage(UWM_MDICHILDUNMAXIMIZED_MSG);
		UWM_MDICHILDSHOWTABCONTEXTMENU = ::RegisterWindowMessage(UWM_MDICHILDSHOWTABCONTEXTMENU_MSG);
		UWM_MDICHILDSAVEMODIFIED =       ::RegisterWindowMessage(UWM_MDICHILDSAVEMODIFIED_MSG);
		UWM_MDICHILDISMODIFIED =         ::RegisterWindowMessage(UWM_MDICHILDISMODIFIED_MSG);
		UWM_MDICHILDCLOSEWITHNOPROMPT =  ::RegisterWindowMessage(UWM_MDICHILDCLOSEWITHNOPROMPT_MSG);
	}
};

#if (_MSC_VER >= 1300)
	// VC 7.0 and later (the message variables are initialized with RegisterWindowMessage in declaration)
	#pragma deprecated(RegisterTabbedMDIMessages)
#else
	// VC 6.0 and earlier (need to initialize message variables with RegisterWindowMessage outside of declaration)
	#if defined(_ATL_MIN_CRT) || defined(_TABBEDMDI_MESSAGES_EXTERN_REGISTER)
		// With _ATL_MIN_CRT, we don't get global constructors and destructors,
		// which is the out-of-the-box way we register the tabbed MDI window messages,
		// so the client needs to register these messages.
		// _TABBEDMDI_MESSAGES_EXTERN_REGISTER also skips declaring a global
		// right here - see the note below.

		#if defined(_ATL_MIN_CRT) && !defined(_TABBEDMDI_MESSAGES_NO_WARN_ATL_MIN_CRT)
			#pragma message("By defining _ATL_MIN_CRT, you are responsible for registering the custom TabbedMDI window messages")
			#pragma message(" (Define _TABBEDMDI_MESSAGES_NO_WARN_ATL_MIN_CRT to not see this message again)")
		#endif
	#else
		// Global struct, whose constructor will get called when the executable image gets loaded
		// (the CRT makes sure global objects get constructed and destructed)
		// See the note above the struct for a discussion on alternatives to this struct
		// if you're targetting VC 7.0 at a minimum.

		// If you are getting "already defined" errors because of including TabbedMDI.h
		// in multiple translation units, you can either change it so that you
		// reference this file only from stdafx.h, or you can declare
		// "_TABBEDMDI_MESSAGES_EXTERN_REGISTER" before including TabbedMDI.h
		// and then have an instance of the "RegisterTabbedMDIMessages"
		// structure in a translation unit that has reference to this file.
		RegisterTabbedMDIMessages g_RegisterTabbedMDIMessages;
	#endif // defined(_ATL_MIN_CRT) || defined(_TABBEDMDI_MESSAGES_EXTERN_REGISTER)
#endif //(_MSC_VER >= 1300)


#endif //__WTL_TABBED_MDI_MESSAGES_H__
