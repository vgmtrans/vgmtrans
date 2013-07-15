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

#ifndef __WTL_DW__PACKAGEWINDOW_H__
#define __WTL_DW__PACKAGEWINDOW_H__

#pragma once

#include "DockMisc.h"
#include "WndFrmPkg.h"

namespace dockwins{

template <class T,
		  class TWndPackage,
          class TBase = CWindow,
          class TWinTraits = CControlWinTraits>
class ATL_NO_VTABLE CPackageWindowImpl : public CWindowImpl< T, TBase, TWinTraits >
{
	typedef TWndPackage			CWndPackage;
	typedef	CPackageWindowImpl<T,TWndPackage,TBase,TWinTraits>	thisClass;
public:
	CPackageWindowImpl()
		:m_package(false)
	{
	}
	void SetOrientation( bool bHorizontal )
	{
		m_package.SetOrientation(bHorizontal);
	}
	bool IsHorizontal() const
	{
		return m_package.IsHorizontal();
	}

	void UpdateLayout()
	{
		CRect rcClient;
		GetClientRect(&rcClient);
		m_package.UpdateLayout(rcClient);
	}

	void Draw(CDC& dc)
	{
		CRect rcClient;
		GetClientRect(&rcClient);
		m_package.Draw(dc,rcClient);
	}

	HCURSOR GetCursor(const CPoint& pt)
	{
		CRect rcClient;
		GetClientRect(&rcClient);
		HCURSOR hCursor=NULL;
		if(rcClient.PtInRect(pt))
			hCursor=m_package.GetCursor(pt,rcClient);
		return hCursor;
	}

	bool StartSliding(const CPoint& pt)
	{
		CRect rcClient;
		GetClientRect(&rcClient);
		CDWSettings settings;
		return m_package.StartSliding(m_hWnd,pt,rcClient,settings.GhostDrag());
	}

	LRESULT AcceptDock(DFDOCKRECT* pHdr)
	{
		assert(::IsWindow(pHdr->hdr.hWnd));
		assert(pHdr->hdr.hBar==m_hWnd);

		LRESULT lRes=FALSE;
		CRect rcClient;
		GetClientRect(&rcClient);

		ScreenToClient(&(pHdr->rect));
		lRes=m_package.AcceptDock(pHdr,rcClient);
		if(!lRes)
			pHdr->hdr.hBar=HNONDOCKBAR;
		ClientToScreen(&(pHdr->rect));
		return lRes;
	}
	LRESULT GetDockingPosition(DFDOCKPOS* pHdr) const
	{
		LRESULT lRes=FALSE;
		CRect rcClient;
		GetClientRect(&rcClient);
		lRes=m_package.GetDockingPosition(pHdr,rcClient);
		return lRes;
	}
	LRESULT SetDockingPosition(DFDOCKPOS* pHdr)
	{
		LRESULT lRes=FALSE;
		CRect rcClient;
		GetClientRect(&rcClient);
		lRes=m_package.SetDockingPosition(pHdr,rcClient);
		return lRes;
	}
	LRESULT Dock(DFDOCKRECT* pHdr)
	{
		LRESULT lRes=FALSE;
		CRect rcClient;
		GetClientRect(&rcClient);
		ScreenToClient(&(pHdr->rect));
		lRes=m_package.Dock(pHdr,rcClient);
		ClientToScreen(&(pHdr->rect));
		return lRes;
	}
	LRESULT Undock(DFMHDR* pHdr)
	{
		CRect rcClient;
		GetClientRect(&rcClient);
		return m_package.Undock(pHdr,rcClient);
	}
	LRESULT Replace(DFDOCKREPLACE* pHdr)
	{
		CRect rcClient;
		GetClientRect(&rcClient);
		return m_package.Replace(pHdr,rcClient);
	}

	LRESULT GetMinDist(const DFMHDR* pHdr) const
	{
		return m_package.GetMinFrameDist(pHdr);
	}
	void GetMinMaxInfo(LPMINMAXINFO pMinMaxInfo) const
	{
		m_package.GetMinMaxInfo(pMinMaxInfo);
		pMinMaxInfo->ptMaxTrackSize.x=0;
		pMinMaxInfo->ptMaxTrackSize.y=0;
	}
protected:
////////////////messages handlers//////////////////////
    BEGIN_MSG_MAP(thisClass)
		MESSAGE_HANDLER(WM_PAINT, OnPaint)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBackground)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_SETCURSOR,OnSetCursor)
		MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)
		MESSAGE_HANDLER(WM_GETMINMAXINFO,OnGetMinMaxInfo)
/////////////////////
		MESSAGE_HANDLER(WMDF_DOCK,OnDock)
    END_MSG_MAP()


	LRESULT OnPaint(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	{
		T* pThis=static_cast<T*>(this);
		CPaintDC dc(pThis->m_hWnd);
		pThis->Draw(dc);
		return 0;
	}

	LRESULT OnEraseBackground(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	{
		return 1;
	}

	LRESULT OnSize(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
	{
		if(wParam != SIZE_MINIMIZED)
		{
			T* pThis = static_cast<T*>(this);
			pThis->UpdateLayout();
		}
		bHandled = FALSE;
		return 1;
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

    LRESULT OnDock(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/)
    {
		LRESULT lRes=FALSE;
		T* pThis=static_cast<T*>(this);
		DFMHDR* pHdr=reinterpret_cast<DFMHDR*>(lParam);
		switch(pHdr->code)
		{
			case DC_ACCEPT:
				lRes=pThis->AcceptDock(reinterpret_cast<DFDOCKRECT*>(pHdr));
				break;
			case DC_DOCK:
				lRes=pThis->Dock(reinterpret_cast<DFDOCKRECT*>(pHdr));
				break;
			case DC_UNDOCK:
				lRes=pThis->Undock(pHdr);
				break;
			case DC_REPLACE:
				lRes=pThis->Replace(reinterpret_cast<DFDOCKREPLACE*>(pHdr));
				break;
			case DC_SETDOCKPOSITION:
				lRes=pThis->SetDockingPosition(reinterpret_cast<DFDOCKPOS*>(pHdr));
				break;
			case DC_GETDOCKPOSITION:
				lRes=pThis->GetDockingPosition(reinterpret_cast<DFDOCKPOS*>(pHdr));
				break;
			case DC_GETMINDIST:
				lRes=pThis->GetMinDist(pHdr);
				break;
		}
		return lRes;
	}
protected:
	CWndPackage		m_package;
};

template<class TTraits = CDockingFrameTraits >
class CPackageWindowFrame : public IFrame
{
	typedef CPackageWindowFrame<TTraits> thisClass;
	typedef TTraits	CTraits;
protected:
	template<class TTraits >
	struct CPackageWindowT : CPackageWindowImpl<CPackageWindowT<TTraits>, CWndFramesPackage<TTraits> >
	{
	public:
		DECLARE_WND_CLASS_EX(_T("CPackageWindowFrame::CPackageWindow"), CS_DBLCLKS, COLOR_WINDOW)
        virtual void OnFinalMessage(HWND /*hWnd*/)
        {
            delete this;
        }
	};
	typedef CPackageWindowT<CTraits> CPackageWindow;
public:
	CPackageWindowFrame()
	{
		m_pImpl=new CPackageWindow;
	}
	virtual  HWND hwnd() const
	{
		return m_pImpl->m_hWnd;
	}
	virtual bool AcceptDock(DFDOCKRECT* pHdr) const
	{
		CWindow from(pHdr->hdr.hBar);
		pHdr->hdr.hBar=hwnd();
		from.ClientToScreen(&(pHdr->rect));
		bool bRes=(::SendMessage(pHdr->hdr.hBar,WMDF_DOCK,NULL,reinterpret_cast<LPARAM>(pHdr))!=FALSE);
		from.ScreenToClient(&(pHdr->rect));
		return bRes;
	}
	virtual void GetMinMaxInfo(LPMINMAXINFO pMinMaxInfo) const
	{
		m_pImpl->GetMinMaxInfo(pMinMaxInfo);
	}
	virtual distance MinDistance() const
	{
		MINMAXINFO mmInfo;
		ZeroMemory(&mmInfo,sizeof(MINMAXINFO));
		GetMinMaxInfo(&mmInfo);
		return m_pImpl->IsHorizontal() ? mmInfo.ptMinTrackSize.y : mmInfo.ptMinTrackSize.x;
	}

	HWND Create(HWND hParent,RECT& rc,bool bHorizontal)
	{
		m_pImpl->SetOrientation(bHorizontal);
		return m_pImpl->Create(hParent,rc);
	}
	static thisClass* CreateInstance(HDOCKBAR hBar,bool bHorizontal)
	{
		thisClass* ptr=new thisClass;
		CRect rc(0,0,0,0);
		if(ptr->Create(hBar,rc,bHorizontal)==NULL)
		{
			delete ptr;
			ptr=0;
			assert(ptr);
		}
		return ptr;
	}
protected:
	CPackageWindow* m_pImpl;
};

}//namespace dockwins
#endif // __WTL_DW__PACKAGEWINDOW_H__
