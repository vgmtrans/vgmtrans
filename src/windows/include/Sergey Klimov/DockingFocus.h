// Copyright (c) 2002
// Peter Krnjevic(peter@dynalinktech.com)
// WTL Docking windows
//
// This code is provided "as is", with absolutely no warranty expressed
// or implied. Any use is at your own risk.
//
// This code may be used in compiled form in any way you desire. This
// file may be redistributed unmodified by any means PROVIDING it is
// not sold for profit without the authors written consent, and
// providing that this notice and the authors name is included. If
// the source code in  this file is used in any commercial application
// then a simple email woulod be nice.

#pragma once
#ifndef __WTL_DW__DOCKINGFOCUS_H__
#define __WTL_DW__DOCKINGFOCUS_H__

#define DF_FOCUS_FEATURES

#include <map>
#include <queue>

namespace dockwins {

typedef std::map<HWND,HWND> HWNDMAP;

class CDockingFocusHandler : public CMessageMap
{
public:
	CDockingFocusHandler() { This(this); m_hWnd=0; m_hHook=0; }
	HRESULT	InstallHook(HWND hWnd)
	{
		ATLASSERT(0 != hWnd);
		ATLASSERT(0 == m_hWnd);
		m_hWnd = hWnd;
		// make	sure the hook is installed
		if (m_hHook == NULL)
		{
			m_hHook = ::SetWindowsHookEx(WH_CBT, CBTProcStub, _Module.m_hInst, GetCurrentThreadId());
			// is the hook set?
			if (m_hHook == NULL)
			{
				ATLASSERT(false);
				return E_UNEXPECTED;
			}
		}
		return S_OK;
	}

	HRESULT	RemoveHook(HWND /*hWnd*/)
	{
		HRESULT	hr = S_OK;
		if (!::UnhookWindowsHookEx(m_hHook))
			hr = HRESULT_FROM_WIN32(::GetLastError());
		m_hHook = NULL;
		return hr;
	}

	void AddWindow(HWND hwnd)
	{
		ATLASSERT(hwnd);
		if (m_mapHwnd.end() == m_mapHwnd.find(hwnd))
			m_mapHwnd.insert(HWNDMAP::value_type(hwnd,::IsChild(hwnd,GetFocus())?GetFocus():NULL));
	}

    BEGIN_MSG_MAP(thisClass)
		m_hwndMSg = hWnd;
		MESSAGE_HANDLER(WM_MOUSEACTIVATE, OnMouseActivate)
		MESSAGE_HANDLER(WM_ACTIVATE, OnActivate)
    END_MSG_MAP()

	LRESULT OnMouseActivate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	{
//		bHandled = FALSE;
		SetFocusIfWindowFound(m_hwndMSg);
		return 0;
	}

	LRESULT OnActivate(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
	{
		bHandled = false;
		static HWND hWnd;
		static HWND hWndActive;
		if (::GetActiveWindow() == m_hWnd)
		{
			if (WA_INACTIVE == wParam)
			{
				hWnd = GetFocus();
			}
			else
			{
				if (::IsChild(m_hWnd,hWnd))
				{
					bHandled = true;
					SetFocusEx(hWnd); // ::PostMessage(hWnd,WM_SETFOCUS,0,0);
				}
			}
			InvalidateCaption(hWnd);
		}
		return 0;
	}

	// ugly, but effective.
	static CDockingFocusHandler* This(CDockingFocusHandler* t=0)
	{
		static CDockingFocusHandler* pThis;
		if (t)
		{
			ATLASSERT(0 == pThis);
			pThis=t;
		}
		ATLASSERT(0 != pThis);
		return pThis;
	}

	static HWND SetFocusEx(HWND hwnd)
	{
		CWindow dad(GetParent(hwnd));
		if (dad.IsWindow())
		{
			TCHAR sz[100];
			int n = GetClassName(dad,sz,sizeof(sz)/sizeof(sz[0]));
			if (n && 0 == _tcscmp(_T("#32770"),sz))
			{
				HWND hwndFocus = GetFocus();
				dad.GotoDlgCtrl(hwnd);
				return hwndFocus;
			}
		}
		return ::SetFocus(hwnd);
	}

private:
	static LRESULT CALLBACK CBTProcStub(int nCode, WPARAM wParam, LPARAM lParam)
	{
		ATLASSERT(This());
		return This()->CBTProc(nCode,wParam,lParam);
	}

	LRESULT CALLBACK CBTProc(int nCode, WPARAM wParam, LPARAM lParam)
	{
		LPMSG lpMsg	= (LPMSG) lParam;
		lpMsg;  // avoid warning on level 4
		if (nCode == HCBT_SETFOCUS)
		{
			InvalidateCaption((HWND)wParam);
			InvalidateCaption((HWND)lParam);
			UpdateFocus(GetDockingParent((HWND)wParam),(HWND)wParam);
		}
		else if (nCode == HCBT_DESTROYWND)
		{
			HWNDMAP::iterator i =  m_mapHwnd.find((HWND)wParam);
			if (m_mapHwnd.end() != i)
				m_mapHwnd.erase(i);
		}
		// Pass to the next hook in the chain.
		return ::CallNextHookEx(m_hHook, nCode, wParam, lParam);
	}

	void InvalidateCaption(HWND hWnd)
	{
		CWindow wnd(GetDockingParent(hWnd));
		if (!wnd.IsWindow())
			return;
		m_qHwndCaption.push(wnd);
		// since the focus has yet to be set, we need to wait a bit before redrawing the caption.
		SetTimer(NULL,0,1,TimerProc);
	}

	static VOID CALLBACK TimerProc(HWND hWnd, UINT /*uMsg*/, UINT_PTR idEvent, DWORD /*dwTime*/)
	{
		KillTimer(hWnd,idEvent);
		ATLASSERT(This());
		ATLASSERT(!This()->m_qHwndCaption.empty());
		CWindow w(This()->m_qHwndCaption.front());
		if (w.IsWindow())
			w.RedrawWindow(NULL,NULL,RDW_FRAME|RDW_INVALIDATE);
		This()->m_qHwndCaption.pop();
	}

	static HWND GetDockingParent(HWND hWnd)
	{
		static ATOM aClass;

		if (!aClass)
		{
			WNDCLASSEX wc;
			wc.cbSize = sizeof(wc);
			aClass = (ATOM)::GetClassInfoEx(_Module.GetModuleInstance(),_T("CPackageWindowFrame::CPackageWindow"),&wc);
		}
		// looks for parent window 2 levels down from m_hWnd, which
		// was initialized in InitializeDockingFrame.
		//	ATLASSERT(aClass);
		if (0 == aClass)
			return NULL;
		ATLASSERT(This());
		while (NULL != hWnd)
		{
			HWND h = ::GetParent(hWnd);
			if (!h)
				return NULL;
			if (::GetClassLongPtr(h,GCW_ATOM) == aClass)
				return hWnd;
			hWnd = h;
		}
		return hWnd;
	}

	bool SetFocusIfWindowFound(HWND hwnd)
	{
		HWNDMAP::iterator i =  m_mapHwnd.find(hwnd);
		if (m_mapHwnd.end() == i)
			return false;
		if (i->second)
		{
			// ensure setting focus won't change activation.
			if (::IsChild(hwnd,i->second))
				SetFocusEx(i->second);
		}
		else
			SetFocusEx(i->first);
		return true;
	}

	void UpdateFocus(HWND hwndDocking, HWND hwndFocus)
	{
		if (!hwndDocking)
			return;
		HWNDMAP::iterator i = m_mapHwnd.find(hwndDocking);
		if (m_mapHwnd.end() != i && ::IsChild(hwndDocking,hwndFocus))
			i->second = hwndFocus;
	}
private:
	HHOOK m_hHook;
	HWND m_hWnd;
	HWND m_hwndMSg;
	std::queue<HWND> m_qHwndCaption;	// Invalid caption queue
	HWNDMAP m_mapHwnd;			// Windows that have received focus
};


class CCaptionFocus
{
public:
	CCaptionFocus(HDC hdc) : m_hWnd(::WindowFromDC(hdc)), m_DockingFocusHandler(*CDockingFocusHandler::This()) { }
	HBRUSH GetCaptionBgBrush()
	{
		m_DockingFocusHandler.AddWindow(m_hWnd);
		return GetSysColorBrush(HasFocus() ? COLOR_ACTIVECAPTION : COLOR_3DFACE);
	}
	DWORD GetCaptionTextColor() const { return GetSysColor(HasFocus() ? COLOR_CAPTIONTEXT : COLOR_BTNTEXT); }
//private:
	bool HasFocus() const { return 0!= CWindow(m_hWnd).IsChild(GetFocus()); }
private:
	HWND m_hWnd;
	CDockingFocusHandler& m_DockingFocusHandler;
};



} // namespace dockwins
#endif // __WTL_DW__DOCKINGFOCUS_H__