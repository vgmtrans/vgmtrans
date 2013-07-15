// Copyright (c) 2002
// Sergey Klimov (kidd@ukr.net)
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

#ifndef __WTL_DW__DOCKINGFRAME_H__
#define __WTL_DW__DOCKINGFRAME_H__

#pragma once

#include <atlframe.h>
#include "DockMisc.h"
#include "PackageWindow.h"
#include "DockingFocus.h"

namespace dockwins{
/////////////////CDockingFrameImplBase

#ifdef DF_AUTO_HIDE_FEATURES
template <	class T,
			class TBase,
			class TWinTraits = CDockingFrameTraits,
			class TAutoHidePaneTraits = COutlookLikeAutoHidePaneTraits >
class ATL_NO_VTABLE CDockingFrameImplBase : public TBase
{
	typedef CDockingFrameImplBase<T,TBase,TWinTraits,TAutoHidePaneTraits>	thisClass;
#else
template <	class T,
			class TBase,
			class TWinTraits = CDockingFrameTraits >
class ATL_NO_VTABLE CDockingFrameImplBase : public TBase
{
	typedef CDockingFrameImplBase<T,TBase,TWinTraits>	thisClass;
#endif
	typedef typename TBase								baseClass;
	typedef typename TWinTraits							CTraits; 
	typedef typename CTraits::CSplitterBar				CSplitterBar;
	typedef CPackageWindowFrame<CTraits>				CPackageFrame; 
	typedef CSubWndFramesPackage<CPackageFrame,CTraits>	CWndPackage;
	typedef typename CDWSettings::CStyle				CStyle; 
	struct  CDockOrientationFlag  
	{
		enum{hor=0x80000000,ver=0};
		static void SetVertical(DWORD& flag)
		{
			flag&=~hor;
		}
		static void SetHorizontal(DWORD& flag)
		{
			flag|=hor;
		}
		static bool ResetAndCheckIsHorizontal(DWORD& flag)
		{
			bool bRes=(flag&hor)!=0;
			flag&=~hor;
			return bRes;
		}
	};
public:
	CDockingFrameImplBase()
		:m_vPackage(false),m_hPackage(true)
	{
	}
	void ApplySystemSettings()
	{
		m_settings.Update();
#ifdef DF_AUTO_HIDE_FEATURES
		m_ahManager.ApplySystemSettings(m_hWnd);
#endif
	}
	bool InitializeDockingFrame(CStyle style=CStyle::sUseSysSettings)
	{
		m_settings.SetStyle(style);
		bool bRes=true;
#ifdef DF_AUTO_HIDE_FEATURES
		assert(m_hWnd);
		bRes=m_ahManager.Initialize(m_hWnd);
		assert(bRes);
#endif
		T* pThis = static_cast<T*>(this);
		pThis->ApplySystemSettings();
		pThis->UpdateLayout(FALSE);
		m_vPackage.Insert(&m_hPackage,m_vPackage);
		m_hPackage.Insert(&m_hWndClient,m_vPackage);
#ifdef DF_FOCUS_FEATURES
		m_focusHandler.InstallHook(pThis->m_hWnd);
#endif
		return bRes;
	}

	void UpdateLayout(BOOL bResizeBars = TRUE)
	{
		CRect rc;
		GetClientRect(&rc);
		UpdateBarsPosition(rc, bResizeBars);
		CClientDC dc(m_hWnd);
#ifdef DF_AUTO_HIDE_FEATURES
		m_ahManager.UpdateLayout(dc,rc);
#endif
		m_vPackage.UpdateLayout(rc);
		Draw(dc);
	}

	void GetMinMaxInfo(LPMINMAXINFO pMinMaxInfo) const
	{
		CRect rc;
		GetWindowRect(&rc);
		pMinMaxInfo->ptMinTrackSize.x=0;
		pMinMaxInfo->ptMinTrackSize.y=0;
		m_vPackage.GetMinMaxInfo(pMinMaxInfo);
		pMinMaxInfo->ptMinTrackSize.x+=rc.Width()-m_vPackage.Width();
		pMinMaxInfo->ptMinTrackSize.y+=rc.Height()-m_vPackage.Height();
#ifdef DF_AUTO_HIDE_FEATURES
		pMinMaxInfo->ptMinTrackSize.x+=m_ahManager.Width();
		pMinMaxInfo->ptMinTrackSize.y+=m_ahManager.Height();
#endif
	}

	void Draw(CDC& dc)
	{
		m_vPackage.Draw(dc);
		m_hPackage.Draw(dc);
#ifdef DF_AUTO_HIDE_FEATURES
		m_ahManager.Draw(dc);
#endif
	}

	HCURSOR GetCursor(const CPoint& pt)
	{
		HCURSOR hCursor=NULL;
		if(m_hPackage.PtInRect(pt))
			hCursor=m_hPackage.GetCursor(pt);
		else
		{
			if(m_vPackage.PtInRect(pt))
				hCursor=m_vPackage.GetCursor(pt);
		}
		return hCursor;
	}

	bool StartSliding(const CPoint& pt)
	{
		bool bRes=!m_vPackage.PtInRect(pt);
		if(!bRes)
		{
			if(m_hPackage.PtInRect(pt))
				bRes=m_hPackage.StartSliding(m_hWnd,pt,m_settings.GhostDrag());
			else
				bRes=m_vPackage.StartSliding(m_hWnd,pt,m_settings.GhostDrag());
			if(bRes)
				RedrawWindow(NULL,NULL,RDW_INVALIDATE | RDW_UPDATENOW |
										((m_hWndClient==NULL)?RDW_ERASE:0));
		}
		return bRes;
	}

	bool AdjustDragRect(DFDOCKRECT* pHdr)
	{
		CRect rc;
		GetClientRect(&rc);
		int limit=rc.Width()/3;
		if(pHdr->rect.right-pHdr->rect.left>limit)
			pHdr->rect.right=pHdr->rect.left+limit;

		limit=GetSystemMetrics(SM_CXMIN);
		if(pHdr->rect.right-pHdr->rect.left<limit)
			pHdr->rect.right=pHdr->rect.left+limit;

		limit=rc.Height()/3;
		if(pHdr->rect.bottom-pHdr->rect.top>limit)
			pHdr->rect.bottom=pHdr->rect.top+limit;

		limit=GetSystemMetrics(SM_CYMIN);
		if(pHdr->rect.bottom-pHdr->rect.top<limit)
			pHdr->rect.bottom=pHdr->rect.top+limit;

		return true;
	}

	LRESULT AcceptDock(DFDOCKRECT* pHdr)
	{
		ScreenToClient(&(pHdr->rect));
		pHdr->hdr.hBar=m_hWnd;
		pHdr->flag=0;
		LRESULT lRes=m_hPackage.AcceptDock(pHdr);
		if(lRes!=FALSE)
			CDockOrientationFlag::SetHorizontal(pHdr->flag);
		else
		{
			lRes=m_vPackage.AcceptDock(pHdr);
			if(lRes!=FALSE)
				CDockOrientationFlag::SetVertical(pHdr->flag);
			else
				pHdr->hdr.hBar=HNONDOCKBAR;
		}
		ClientToScreen(&(pHdr->rect));
		return lRes;
	}

	LRESULT Dock(DFDOCKRECT* pHdr)
	{
		LRESULT lRes=FALSE;
		ScreenToClient(&(pHdr->rect));
		if(CDockOrientationFlag::ResetAndCheckIsHorizontal(pHdr->flag))
			lRes=m_hPackage.Dock(pHdr);
		else
			lRes=m_vPackage.Dock(pHdr);
		ClientToScreen(&(pHdr->rect));
		RedrawWindow(NULL,NULL,RDW_INVALIDATE | RDW_UPDATENOW |
									((m_hWndClient==NULL)?RDW_ERASE:0));
		return lRes;
	}

	LRESULT Undock(DFMHDR* pHdr)
	{
		bool bRes;
		assert(::IsWindow(pHdr->hWnd));
		bRes=m_vPackage.Undock(pHdr);
		if(!bRes)
			bRes=m_hPackage.Undock(pHdr);
		assert(bRes);
		RedrawWindow(NULL,NULL,RDW_INVALIDATE | RDW_UPDATENOW |
									((m_hWndClient==NULL)?RDW_ERASE:0));
		return bRes;
	}
	template<class T>
	bool DockWindow(T& dockWnd,CDockingSide side,unsigned long nBar,float fPctPos,unsigned long	nWidth, unsigned long nHeight)
	{
		if(dockWnd.IsDocking())
			dockWnd.Undock();

		DFDOCKPOS dockHdr={0};
		dockHdr.hdr.code=DC_SETDOCKPOSITION;
		dockHdr.hdr.hWnd=dockWnd.m_hWnd;
		dockHdr.hdr.hBar=m_hWnd;

		dockHdr.dwDockSide=side;
		dockHdr.nBar=nBar;
		dockHdr.fPctPos=fPctPos;
		dockHdr.nWidth=nWidth;
		dockHdr.nHeight=nHeight;
		return SetDockingPosition(&dockHdr);
	}

	bool SetDockingPosition(DFDOCKPOS* pHdr)
	{
		assert(::IsWindow(pHdr->hdr.hWnd));
		bool bRes;
		CDockingSide side(pHdr->dwDockSide);
		if(side.IsHorizontal())
			bRes=m_vPackage.SetDockingPosition(pHdr);
		else
			bRes=m_hPackage.SetDockingPosition(pHdr);
		RedrawWindow(NULL,NULL,RDW_INVALIDATE | RDW_UPDATENOW |
									((m_hWndClient==NULL)?RDW_ERASE:0));
		return bRes;
	}

	bool GetDockingPosition(DFDOCKPOS* pHdr) const
	{
		assert(::IsWindow(pHdr->hdr.hWnd));
		bool bRes;
#ifdef DF_AUTO_HIDE_FEATURES
		bRes=m_ahManager.GetDockingPosition(pHdr);
		if(!bRes)
		{
#endif
			pHdr->dwDockSide=CDockingSide::sBottom;
			bRes=m_vPackage.GetDockingPosition(pHdr);
			if(!bRes)
			{
				pHdr->dwDockSide=CDockingSide::sRight;
				bRes=m_hPackage.GetDockingPosition(pHdr);
			}
#ifdef DF_AUTO_HIDE_FEATURES
		}
#endif
		return bRes;
	}
#ifdef DF_AUTO_HIDE_FEATURES
	bool OnMouseMove(WPARAM key,const CPoint& pt)
	{
		bool bRes=( (key==0) && GetActiveWindow()!=0 );
		if(bRes)
			bRes=m_ahManager.MouseEnter(m_hWnd,pt);
		return bRes;
	}
	bool OnMouseHover(WPARAM key,const CPoint& pt)
	{
		if(key==0)
			m_ahManager.MouseHover(m_hWnd,pt);
		return true;
	}
	bool PinUp(DFPINUP* pHdr)
	{
		bool bUpdate;
		bool bRes=m_ahManager.PinUp(m_hWnd,pHdr,bUpdate);
		if(bUpdate)
		{
			T* pThis = static_cast<T*>(this);
			pThis->UpdateLayout(FALSE);
			pThis->RedrawWindow(NULL,NULL,RDW_INVALIDATE | RDW_UPDATENOW |
											((m_hWndClient==NULL)?RDW_ERASE:0));
		}
		return bRes;
	}
	LRESULT IsPinned(DFMHDR* pHdr) const
	{
		return pHdr->hBar==m_ahManager.m_hWnd;
	}
#endif
protected:
////////////////messages handlers//////////////////////
    BEGIN_MSG_MAP(thisClass)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_SETTINGCHANGE, OnSettingChange)
		MESSAGE_HANDLER(WM_SYSCOLORCHANGE, OnSysColorChange)
		MESSAGE_HANDLER(WM_PAINT, OnPaint)
		MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)
		MESSAGE_HANDLER(WM_SETCURSOR,OnSetCursor)
		MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)
		MESSAGE_HANDLER(WM_GETMINMAXINFO,OnGetMinMaxInfo)
#ifdef DF_AUTO_HIDE_FEATURES
		MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
		MESSAGE_HANDLER(WM_MOUSEHOVER, OnMouseHover)
#endif
/////////////////////
		MESSAGE_HANDLER(WMDF_DOCK,OnDock)
#ifdef DF_FOCUS_FEATURES
		CHAIN_MSG_MAP_MEMBER(m_focusHandler)
#endif
		CHAIN_MSG_MAP(baseClass)
    END_MSG_MAP()

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
	{
		T* pThis=static_cast<T*>(this);
		pThis->InitializeDockingFrame();
		bHandled=FALSE;
		return 0;
	}

	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
	{
		bHandled = FALSE;
#ifdef DF_FOCUS_FEATURES
		m_focusHandler.RemoveHook(m_hWnd);
#endif
		return 0;
	}

	LRESULT OnSettingChange(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	{
		T* pThis=static_cast<T*>(this);
		pThis->ApplySystemSettings();
//		pThis->UpdateLayout();
		return 0;
	}

	LRESULT OnSysColorChange(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	{
		T* pThis=static_cast<T*>(this);
		pThis->ApplySystemSettings();
//		pThis->UpdateLayout();
		return 0;
	}

	LRESULT OnPaint(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	{
		T* pThis=static_cast<T*>(this);
		CPaintDC dc(pThis->m_hWnd);
		pThis->Draw(dc);
		return 0;
	}
	LRESULT OnSetFocus(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
	{
		return DefWindowProc(uMsg,wParam,lParam);
	}

	LRESULT OnSetCursor(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	{
		T* pThis = static_cast<T*>(this);
		if((HWND)wParam == pThis->m_hWnd && LOWORD(lParam) == HTCLIENT)
		{
			DWORD dwPos = ::GetMessagePos();
            CPoint pt(GET_X_LPARAM(dwPos), GET_Y_LPARAM(dwPos));
			pThis->ScreenToClient(&pt);
			HCURSOR hCursor=pThis->GetCursor(pt);
			bHandled=(hCursor!=NULL);
			if(bHandled)
				SetCursor(hCursor);
			return bHandled;
		}

		bHandled = FALSE;
		return 0;
	}

	LRESULT OnLButtonDown(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
	{
		CPoint pt( GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		T* pThis = static_cast<T*>(this);
		bHandled=pThis->StartSliding(pt);
		return !bHandled;
	}

	LRESULT OnGetMinMaxInfo(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
	{
		T* pThis=static_cast<T*>(this);
		LRESULT lRes=pThis->DefWindowProc(uMsg,wParam,lParam);
		pThis->GetMinMaxInfo(reinterpret_cast<LPMINMAXINFO>(lParam));
		return lRes;
	}
#ifdef DF_AUTO_HIDE_FEATURES
	LRESULT OnMouseMove(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	{
		CPoint pt( GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		T* pThis = static_cast<T*>(this);
		bHandled=pThis->OnMouseMove(wParam,pt);
		return 0;
	}
	LRESULT OnMouseHover(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	{
		CPoint pt( GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		T* pThis = static_cast<T*>(this);
		bHandled=pThis->OnMouseHover(wParam,pt);
		return 0;
	}
#endif
    LRESULT OnDock(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/)
    {
		LRESULT lRes=FALSE;
		T* pThis=static_cast<T*>(this);
		DFMHDR* pHdr=reinterpret_cast<DFMHDR*>(lParam);
		switch(pHdr->code)
		{
			case DC_ADJUSTDRAGRECT:
				lRes=pThis->AdjustDragRect(reinterpret_cast<DFDOCKRECT*>(pHdr));
				break;
			case DC_ACCEPT:
				lRes=pThis->AcceptDock(reinterpret_cast<DFDOCKRECT*>(pHdr));
				break;
			case DC_DOCK:
				lRes=pThis->Dock(reinterpret_cast<DFDOCKRECT*>(pHdr));
				break;
			case DC_UNDOCK:
				lRes=pThis->Undock(pHdr);
				break;
			case DC_SETDOCKPOSITION:
				lRes=pThis->SetDockingPosition(reinterpret_cast<DFDOCKPOS*>(pHdr));
				break;
			case DC_GETDOCKPOSITION:
				lRes=pThis->GetDockingPosition(reinterpret_cast<DFDOCKPOS*>(pHdr));
				break;
#ifdef DF_AUTO_HIDE_FEATURES
			case DC_PINUP:
				lRes=pThis->PinUp(reinterpret_cast<DFPINUP*>(pHdr));
				break;
			case DC_ISPINNED:
				lRes=pThis->IsPinned(pHdr);
				break;
#endif
		}
		return lRes;
	}
protected:
	CDWSettings	m_settings;
	CWndPackage	m_vPackage;
	CWndPackage	m_hPackage;
	CRect		m_rcClient;
#ifdef DF_AUTO_HIDE_FEATURES
	CAutoHideManager<TAutoHidePaneTraits> m_ahManager;
#endif
#ifdef DF_FOCUS_FEATURES
	CDockingFocusHandler m_focusHandler;
#endif
};

/////////////////CDockingSiteBasement
template <class T, class TBase = CWindow, class TWinTraits = CDockingSiteTraits>
class ATL_NO_VTABLE CDockingSiteBasement : public CWindowImpl<T, TBase, TWinTraits >
{
    typedef CDockingSiteBasement<T, TBase, TWinTraits >  thisClass;
public:
    DECLARE_WND_CLASS(NULL)
    CDockingSiteBasement():m_hWndClient(NULL)
    {
    }
protected:
    BEGIN_MSG_MAP(thisClass)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBackground)
	END_MSG_MAP()
	LRESULT OnSize(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
	{
		if(wParam != SIZE_MINIMIZED)
		{
				T* pT = static_cast<T*>(this);
				pT->UpdateLayout();
		}
		bHandled = FALSE;
		return 1;
	}
    LRESULT OnEraseBackground(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
    {
            if(m_hWndClient != NULL)
                    return 1;

            bHandled = FALSE;
            return 0;
    }
protected:
    HWND m_hWndClient;
};
/////////////////CDockingSiteImpl
template <  class T,
			class TBase = CWindow,
            class TWinTraits = CDockingSiteTraits,
            class TBaseImpl = CDockingSiteBasement<T,TBase,TWinTraits> >
class CDockingSiteImpl:
    public CDockingFrameImplBase< T, TBaseImpl ,TWinTraits >
{
    typedef CDockingSiteImpl<T,TBase,TWinTraits,TBaseImpl>  thisClass;
    typedef CDockingFrameImplBase< T, TBaseImpl ,TWinTraits > baseClass;
public:
    DECLARE_WND_CLASS(_T("CDockingSiteImpl"))
    void UpdateLayout(BOOL bResizeBars = TRUE)
    {
		bResizeBars;// avoid level 4 warning
        CRect rc;
		T* pT = static_cast<T*>(this);
		pT->GetClientRect(&rc);
		CClientDC dc(m_hWnd);
#ifdef DF_AUTO_HIDE_FEATURES
		m_ahManager.UpdateLayout(dc,rc);
		m_ahManager.Draw(dc);
#endif
		m_vPackage.UpdateLayout(rc);
		m_vPackage.Draw(dc);
    }
};

/////////////////CDockingFrameImpl

template <	class T,
            class TBase = CWindow,
            class TWinTraits = CDockingFrameTraits >
class ATL_NO_VTABLE CDockingFrameImpl:
	public CDockingFrameImplBase< T, CFrameWindowImpl< T ,TBase, TWinTraits> ,TWinTraits >
{
public:
	DECLARE_WND_CLASS(_T("CDockingFrameImpl"))
};

/////////////////CMDIDockingFrameImpl
template <class T,
		  class TBase = CMDIWindow,
		  class TWinTraits = CDockingFrameTraits >
class ATL_NO_VTABLE CMDIDockingFrameImpl :
	public CDockingFrameImplBase< T, CMDIFrameWindowImpl< T ,TBase, TWinTraits> ,TWinTraits >
{
public:
	DECLARE_WND_CLASS(_T("CMDIDockingFrameImpl"))
};

#ifdef DF_AUTO_HIDE_FEATURES
/////////////////CAutoHideMDIDockingFrameImpl
template <class T,
		  class TBase = CMDIWindow,
		  class TWinTraits = CDockingFrameTraits,
		  class TAutoHidePaneTraits = COutlookLikeAutoHidePaneTraits >
class ATL_NO_VTABLE CAutoHideMDIDockingFrameImpl :
	public CDockingFrameImplBase< T, CMDIFrameWindowImpl< T ,TBase, TWinTraits> ,TWinTraits, TAutoHidePaneTraits >
{
public:
	DECLARE_WND_CLASS(_T("CAutoHideMDIDockingFrameImpl"))
};
#endif


}//namespace dockwins
#endif // __WTL_DW__DOCKINGFRAME_H__
