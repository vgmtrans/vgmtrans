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

#ifndef __WTL_DW__DOCKINGBOX_H__
#define __WTL_DW__DOCKINGBOX_H__

#pragma once

#include <sstream>
#include "ExtDockingWindow.h"
#include "DockMisc.h"

namespace dockwins{

class CDockingBoxMessage
{
public:
	CDockingBoxMessage()
	{
		std::basic_stringstream<TCHAR> smessage;
		smessage<<_T("DockingBoxMessage-")<<GetCurrentProcessId();
		m_message=RegisterWindowMessage(smessage.str().c_str());
		assert(m_message);
		ATLTRACE(_T("%s = %x \n"),smessage.str().c_str(), m_message);
//        if(m_message==0)
//                throw exception();
	}
	operator unsigned long () const
	{
		return m_message;
	}
protected:
	unsigned long m_message;
};

class CDockingBox : public CDocker
{
	typedef CDocker baseClass;
public:
	CDockingBox(HWND hWnd):CDocker(hWnd)
	{
		assert(IsWindowBox(hWnd));
	}
	bool AcceptDock(DFDOCKRECT* pHdr) const
	{
		return (::SendMessage(m_hWnd,m_message,NULL,reinterpret_cast<LPARAM>(pHdr))!=FALSE);
	}

	bool Replace(DFDOCKREPLACE* pHdr) const
	{
		pHdr->hdr.hBar=m_hWnd;
		pHdr->hdr.code=DC_REPLACE;
		return (::SendMessage(m_hWnd,WMDF_DOCK,NULL,reinterpret_cast<LPARAM>(pHdr))!=FALSE);
	}

	bool IsBox() const
	{
		return IsWindowBox(m_hWnd);
	}
	static bool IsWindowBox(HWND hWnd)
	{
		DFMHDR dockHdr;
		dockHdr.code=DC_ISBOX;
		dockHdr.hWnd=hWnd;
//		dockHdr.hBar=HNONDOCKBAR;
		return (::SendMessage(hWnd,m_message,NULL,reinterpret_cast<LPARAM>(&dockHdr))!=FALSE);
	}
	static unsigned long DockingBoxMessage()
	{
		return	m_message;
	}
	bool Activate(HWND hWnd)
	{
		DFMHDR dockHdr;
		dockHdr.code=DC_ACTIVATE;
		dockHdr.hWnd=hWnd;
		dockHdr.hBar=m_hWnd;
		return (::SendMessage(dockHdr.hBar,WMDF_DOCK,NULL,reinterpret_cast<LPARAM>(&dockHdr))!=FALSE);
	}

	BOOL PlaceAs(HWND hWnd)
	{
		RECT rc;
		BOOL bRes=::GetWindowRect(hWnd,&rc);
		if(bRes)
			bRes=SetWindowPos(HWND_TOP,&rc,SWP_SHOWWINDOW);
		return bRes;
	}
protected:
	static CDockingBoxMessage m_message;
};

class CBoxDocker : public CDocker
{
public:
	CBoxDocker(HWND hWnd=NULL) : CDocker(hWnd)
	{
	}
	bool AcceptDock(DFDOCKRECT* pHdr) const
	{
		pHdr->hdr.code=DC_ACCEPT;
		bool bRes=false;
		HWND hWnd=::WindowFromPoint(*reinterpret_cast<POINT*>(&(pHdr->rect)));
		while( (hWnd!=NULL) && (hWnd!=pHdr->hdr.hWnd) )
		{
			if(CDockingBox::IsWindowBox(hWnd))
			{
				CDockingBox box(hWnd);
				bRes=box.AcceptDock(pHdr);
//				bRes=(::SendMessage(hWnd,m_message,NULL,reinterpret_cast<LPARAM>(pHdr))!=FALSE);
				break;
			}
			hWnd=::GetParent(hWnd);
		}
		if(!bRes)
			bRes=CDocker::AcceptDock(pHdr);
		return bRes;
	}

	bool GetDockingPosition(DFDOCKPOS* pHdr) const
	{
		pHdr->hdr.code=DC_GETDOCKPOSITION;
		HWND hWnd=CDockingBox::IsWindowBox(pHdr->hdr.hBar) ? pHdr->hdr.hBar : m_hWnd;
		assert(::IsWindow(hWnd));
		return (::SendMessage(hWnd,WMDF_DOCK,NULL,reinterpret_cast<LPARAM>(pHdr))!=FALSE);
	}

	bool SetDockingPosition(DFDOCKPOS* pHdr) const
	{
		if((pHdr->hdr.hBar==0)
				|| !::IsWindow(pHdr->hdr.hBar)
					|| !CDockingBox::IsWindowBox(pHdr->hdr.hBar))
		{
			CDockingSide side (pHdr->dwDockSide);
			if(side.IsValid())
				pHdr->hdr.hBar=m_hWnd;
			else
			{
				::SetWindowPos(pHdr->hdr.hWnd ,HWND_TOP ,
								pHdr->rcFloat.left, pHdr->rcFloat.top,
								pHdr->rcFloat.right - pHdr->rcFloat.left,
								pHdr->rcFloat.bottom - pHdr->rcFloat.top,
								SWP_SHOWWINDOW);
				return true;
			}
		}
		pHdr->hdr.code=DC_SETDOCKPOSITION;
		return (::SendMessage(pHdr->hdr.hBar,WMDF_DOCK,NULL,reinterpret_cast<LPARAM>(pHdr))!=FALSE);
	}

	bool Activate(DFMHDR* pHdr)
	{
		pHdr->code=DC_ACTIVATE;
		return (::SendMessage(pHdr->hBar,WMDF_DOCK,NULL,reinterpret_cast<LPARAM>(pHdr))!=FALSE);
	}

};


template <class TCaption,DWORD t_dwStyle = 0, DWORD t_dwExStyle = 0>
struct CDockingBoxTraits : CDockingWindowTraits<TCaption,t_dwStyle,t_dwExStyle>
{
	typedef CBoxDocker	CDocker;
};

typedef CDockingBoxTraits<COutlookLikeCaption,
								WS_OVERLAPPEDWINDOW | WS_POPUP/* WS_CHILD*/ |
								/*WS_VISIBLE |*/ WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
								WS_EX_TOOLWINDOW/* WS_EX_CLIENTEDGE*/> COutlookLikeDockingBoxTraits;
typedef CDockingBoxTraits<COutlookLikeExCaption,
								WS_OVERLAPPEDWINDOW | WS_POPUP/* WS_CHILD*/ |
								/*WS_VISIBLE |*/ WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
								WS_EX_TOOLWINDOW/* WS_EX_CLIENTEDGE*/> COutlookLikeExDockingBoxTraits;
typedef CDockingBoxTraits<CVC6LikeCaption,
								WS_OVERLAPPEDWINDOW | WS_POPUP/* WS_CHILD*/ |
								/*WS_VISIBLE |*/ WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
								WS_EX_TOOLWINDOW/* WS_EX_CLIENTEDGE*/> CVC6LikeDockingBoxTraits;
template <class T,
          class TBase = CWindow,
          class TDockingWinTraits = COutlookLikeDockingBoxTraits>
class ATL_NO_VTABLE CBoxedDockingWindowBaseImpl :
			public CTitleDockingWindowImpl<T,TBase,TDockingWinTraits>
{
    typedef CTitleDockingWindowImpl< T, TBase, TDockingWinTraits >		baseClass;
    typedef CBoxedDockingWindowBaseImpl< T, TBase, TDockingWinTraits >	thisClass;
public:
	bool DockTo(HWND hWnd,int index=0)
	{
		bool bRet=CDockingBox::IsWindowBox(hWnd);
		if(bRet)
		{
			if(IsDocking())
						Undock();
			DFDOCKPOS dockHdr={0};
//			dockHdr.hdr.code=DC_SETDOCKPOSITION;
			dockHdr.hdr.hWnd=m_hWnd;
			dockHdr.hdr.hBar=hWnd;
			dockHdr.nIndex=index;
			CDockingSide::Invalidate(dockHdr.dwDockSide);
			GetWindowRect(&dockHdr.rcFloat);
			bRet=m_docker.SetDockingPosition(&dockHdr);
		}
		return bRet;
	}
	LRESULT OnIsBox(DFMHDR* /*pHdr*/)
	{
		return TRUE;
	}
	LRESULT OnAcceptDock(DFDOCKRECT* /*pHdr*/)
	{
		return FALSE;
	}
	LRESULT OnDock(DFDOCKRECT* /*pHdr*/)
	{
		assert(false);
		return FALSE;
	}
    LRESULT OnUndock(DFMHDR* /*pHdr*/)
    {
		assert(false);
		return FALSE;
    }
	LRESULT OnActivate(DFMHDR* /*pHdr*/)
	{
		assert(false);
		return FALSE;
	}

#ifdef DF_AUTO_HIDE_FEATURES
	LRESULT OnPinBtnPress(DFPINBTNPRESS* pHdr)
	{
		T* pThis=static_cast<T*>(this);
		return pThis->PinBtnPress(pHdr->bVisualize ? true : false);
	}
#endif
	bool OnSetDockingPosition(DFDOCKPOS* /*pHdr*/)
	{
		assert(false);
		return false;
	}

	bool OnGetDockingPosition(DFDOCKPOS* pHdr) const
	{
		HWND hWnd=pHdr->hdr.hWnd;
		bool bRes;
		if(IsDocking())
		{
			pHdr->hdr.hWnd=m_hWnd;
			bRes=GetDockingPosition(pHdr);
		}
		else
		{
			if(IsWindowVisible() || m_pos.hdr.hBar==HNONDOCKBAR)
			{
				CDockingSide::Invalidate(pHdr->dwDockSide);
				GetWindowRect(&pHdr->rcFloat);
			}
			else
				CopyMemory(pHdr,&m_pos,sizeof(DFDOCKPOS));
			bRes=true;
		}
		pHdr->hdr.hWnd=hWnd;
		pHdr->hdr.hBar=m_hWnd;
		return bRes;
	}

////////////////messages handlers/////////////////////////////////
	BEGIN_MSG_MAP(thisClass)
		MESSAGE_HANDLER(WMDF_DOCK,OnDockingBoxMessage)
		MESSAGE_HANDLER(CDockingBox::DockingBoxMessage(),OnDockingBoxMessage)
		CHAIN_MSG_MAP(baseClass)
	END_MSG_MAP()
protected:
	LRESULT OnDockingBoxMessage(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/)
	{
		LRESULT lRes=FALSE;
		T* pThis=static_cast<T*>(this);
		DFMHDR* pHdr=reinterpret_cast<DFMHDR*>(lParam);
		switch(pHdr->code)
		{
			case DC_ISBOX:
				lRes=pThis->OnIsBox(pHdr);
				break;
			case DC_ACCEPT:
				lRes=pThis->OnAcceptDock(reinterpret_cast<DFDOCKRECT*>(pHdr));
				break;
			case DC_DOCK:
				lRes=pThis->OnDock(reinterpret_cast<DFDOCKRECT*>(pHdr));
				break;
			case DC_UNDOCK:
				lRes=pThis->OnUndock(pHdr);
				break;
			case DC_SETDOCKPOSITION:
				lRes=pThis->OnSetDockingPosition(reinterpret_cast<DFDOCKPOS*>(pHdr));
				break;
			case DC_GETDOCKPOSITION:
				lRes=pThis->OnGetDockingPosition(reinterpret_cast<DFDOCKPOS*>(pHdr));
				break;
			case DC_ACTIVATE:
				lRes=pThis->OnActivate(pHdr);
				break;
#ifdef DF_AUTO_HIDE_FEATURES
			case DC_PINBTNPRESS:	
				lRes=pThis->OnPinBtnPress(reinterpret_cast<DFPINBTNPRESS*>(pHdr));
				break;
#endif

		}
		return lRes;
	}
protected:
};

template <class T,
          class TBase = CWindow,
          class TDockingWinTraits = COutlookLikeDockingBoxTraits>
class ATL_NO_VTABLE CDockingBoxBaseImpl :
			public CBoxedDockingWindowBaseImpl<T,TBase,TDockingWinTraits>
{
    typedef CBoxedDockingWindowBaseImpl< T, TBase, TDockingWinTraits >	baseClass;
    typedef CDockingBoxBaseImpl< T, TBase, TDockingWinTraits >			thisClass;
protected:
	// use CreateInstance instead
	CDockingBoxBaseImpl()
	{
	}
	virtual ~CDockingBoxBaseImpl()
	{
	}
    virtual void OnFinalMessage(HWND /*hWnd*/)
    {
        delete this;
    }
public:
	HWND Create(HWND hWnd)
	{
		CRect rcPos(0,0,100,100);
        return baseClass::Create(hWnd, rcPos, NULL);
	}
	static HWND CreateInstance(HWND hWnd)
	{
		assert(false);
		return NULL;
	}
};


template <class T,
          class TBase ,
          class TDockingWinTraits >
class ATL_NO_VTABLE CBoxedDockingWindowImpl :
			public CBoxedDockingWindowBaseImpl<T,TBase,TDockingWinTraits>
{
    typedef CBoxedDockingWindowBaseImpl< T, TBase, TDockingWinTraits >	baseClass;
    typedef CBoxedDockingWindowImpl< T, TBase, TDockingWinTraits >		thisClass;
protected:
	typedef	typename TDockingWinTraits::CBox	CBox;
public:
	BOOL IsWindowVisible() const
	{
		BOOL bRes=baseClass::IsWindowVisible();
		if(!bRes)
		{
			bRes=IsDocking();
			if(bRes)
				bRes=::IsWindowVisible(GetOwnerDockingBar());
		}
		return bRes;
	}

	bool Show()
	{
		bool bRes;
		if(IsDocking())
		{
			DFMHDR dockHdr;
			dockHdr.hBar=GetOwnerDockingBar();
			if(CDockingBox::IsWindowBox(dockHdr.hBar))
			{
//				dockHdr.code=DC_ACTIVATE;
				dockHdr.hWnd=m_hWnd;
				bRes=m_docker.Activate(&dockHdr);
			}
			else
				bRes=baseClass::Show();
		}
		else
			bRes=baseClass::Show();
		return bRes;
	}

	bool Activate()
	{
		bool bRes=IsDocking();
		if(bRes)
		{
			DFMHDR dockHdr;
			DFMHDR dockHdr;
			dockHdr.hBar=GetOwnerDockingBar();
			bRes=CDockingBox::IsWindowBox(dockHdr.hBar);
			if(bRes)
			{
//				dockHdr.code=DC_ACTIVATE;
				dockHdr.hWnd=m_hWnd;
				bRes=m_docker.Activate(&dockHdr);
			}
		}
		return bRes;
	}

	LRESULT OnAcceptDock(DFDOCKRECT* pHdr)
	{
		bool bRes=SendMessage(WM_NCHITTEST,NULL,MAKELPARAM(pHdr->rect.left, pHdr->rect.top))==HTCAPTION;
		if(bRes)
		{
			//GetWindowRect(&(pHdr->rect));
			pHdr->hdr.hBar=m_hWnd;
			GetClientRect(&(pHdr->rect));
			ClientToScreen(&(pHdr->rect));
		}
		return bRes;
	}
	HDOCKBAR CreateDockingBox()
	{
		HDOCKBAR hBar=CBox::CreateInstance(m_docker);
		assert(hBar!=HNONDOCKBAR);
		if(hBar!=HNONDOCKBAR)
		{
			CDockingBox	box(hBar);
			if(IsDocking())
			{
				DFDOCKREPLACE dockHdr;
				dockHdr.hdr.hBar=GetOwnerDockingBar();
				dockHdr.hdr.hWnd=m_hWnd;
				dockHdr.hWnd=hBar;
				if(!m_docker.Replace(&dockHdr))
				{
					::PostMessage(hBar,WM_CLOSE,0,0);
					hBar=HNONDOCKBAR;
				}
			}
			else
				box.PlaceAs(m_hWnd);
		}
		return hBar;
	}
	LRESULT OnDock(DFDOCKRECT* pHdr)
	{
		pHdr->hdr.hBar=GetOwnerDockingBar();
		bool bRes = !IsDocking() || !CDockingBox::IsWindowBox(pHdr->hdr.hBar);
		if(bRes)
		{
			pHdr->hdr.hBar=CreateDockingBox();
			bRes=(pHdr->hdr.hBar!=HNONDOCKBAR);
			if(bRes)
			{
				HWND hPrevDockWnd=pHdr->hdr.hWnd;
				pHdr->hdr.hWnd=m_hWnd;
				CDockingBox	box(pHdr->hdr.hBar);
				bRes=box.Dock(pHdr);
	//			bRes=m_docker.Dock(pHdr);
				pHdr->hdr.hWnd=hPrevDockWnd;
			}
			else
				return bRes;
		}
		CDockingBox	box(pHdr->hdr.hBar);
		bRes=box.Dock(pHdr);
//		bRes=m_docker.Dock(pHdr);
		return bRes;
	}

	bool OnSetDockingPosition(DFDOCKPOS* pHdr)
	{
		pHdr->hdr.hBar=GetOwnerDockingBar();
		bool bRes=!IsDocking() || !CDockingBox::IsWindowBox(pHdr->hdr.hBar);
		if(bRes )
		{
			pHdr->hdr.hBar=CreateDockingBox();
			bRes=(pHdr->hdr.hBar!=HNONDOCKBAR);
			if(bRes)
			{

				HWND hPrevDockWnd=pHdr->hdr.hWnd;
				pHdr->hdr.hWnd=m_hWnd;

	//			CDockingBox	box(pHdr->hdr.hBar);
	//			bRes=box.SetDockingPosition(pHdr);
				bRes=m_docker.SetDockingPosition(pHdr);
				pHdr->hdr.hWnd=hPrevDockWnd;
			}
			else
				return bRes;

		}
//		CDockingBox	box(pHdr->hdr.hBar);
//		bRes=box.SetDockingPosition(pHdr);
		bRes=m_docker.SetDockingPosition(pHdr);
		return bRes;
	}
	LRESULT OnActivate(DFMHDR* pHdr)
	{
		bool bRes=IsDocking();
		if(bRes)
		{
			pHdr->hBar=GetOwnerDockingBar();
			assert(CDockingBox::IsWindowBox(pHdr->hBar));
			bRes=m_docker.Activate(pHdr);
		}
		return bRes;
	}


};

}//namespace dockwins
#endif // __WTL_DW__DOCKINGBOX_H__
