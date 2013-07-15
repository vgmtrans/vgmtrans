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

#ifndef __WTL_DW__DOCKINGWINDOW_H__
#define __WTL_DW__DOCKINGWINDOW_H__

#pragma once

#include "DDTracker.h"

namespace dockwins{

class CDocker : protected CWindow
{
public:
	explicit CDocker(HWND hWnd=NULL) : CWindow(hWnd)
	{
	}
	bool AdjustDragRect(DFDOCKRECT* pHdr) const
	{
		pHdr->hdr.code=DC_ADJUSTDRAGRECT;
		return (::SendMessage(m_hWnd,WMDF_DOCK,NULL,reinterpret_cast<LPARAM>(pHdr))!=FALSE);
	}
	bool AcceptDock(DFDOCKRECT* pHdr) const
	{
		pHdr->hdr.code=DC_ACCEPT;
		return (::SendMessage(m_hWnd,WMDF_DOCK,NULL,reinterpret_cast<LPARAM>(pHdr))!=FALSE);
	}
	bool Dock(DFDOCKRECT* pHdr) const
	{
		pHdr->hdr.code=DC_DOCK;
//		return (::SendMessage(m_hWnd,WMDF_DOCK,NULL,reinterpret_cast<LPARAM>(pHdr))!=FALSE);
		return (::SendMessage(pHdr->hdr.hBar,WMDF_DOCK,NULL,reinterpret_cast<LPARAM>(pHdr))!=FALSE);
	}

	bool Undock(DFMHDR* pHdr) const
	{
		pHdr->code=DC_UNDOCK;
		return (::SendMessage(pHdr->hBar,WMDF_DOCK,NULL,reinterpret_cast<LPARAM>(pHdr))!=FALSE);
	}

    bool Replace(DFDOCKREPLACE* pHdr) const
    {
        pHdr->hdr.code=DC_REPLACE;
        return (::SendMessage(pHdr->hdr.hBar,WMDF_DOCK,NULL,reinterpret_cast<LPARAM>(pHdr))!=FALSE);
    }

	bool GetDockingPosition(DFDOCKPOS* pHdr) const
	{
		pHdr->hdr.code=DC_GETDOCKPOSITION;
		return (::SendMessage(m_hWnd,WMDF_DOCK,NULL,reinterpret_cast<LPARAM>(pHdr))!=FALSE);
	}

	bool SetDockingPosition(DFDOCKPOS* pHdr) const
	{
		pHdr->hdr.hBar=m_hWnd;
		pHdr->hdr.code=DC_SETDOCKPOSITION;
		return (::SendMessage(m_hWnd,WMDF_DOCK,NULL,reinterpret_cast<LPARAM>(pHdr))!=FALSE);
	}
//#ifdef DF_AUTO_HIDE_FEATURES
	bool PinUp(DFPINUP* pHdr) const
	{
		pHdr->hdr.code=DC_PINUP;
		return (::SendMessage(m_hWnd,WMDF_DOCK,NULL,reinterpret_cast<LPARAM>(pHdr))!=FALSE);
	}

	bool IsPinned(DFMHDR* pHdr) const
	{
		pHdr->code=DC_ISPINNED;
		return (::SendMessage(m_hWnd,WMDF_DOCK,NULL,reinterpret_cast<LPARAM>(pHdr))!=FALSE);
	}
//#endif
	operator HWND ()
	{
		return m_hWnd;
	}
};

template < DWORD t_dwStyle = 0, DWORD t_dwExStyle = 0>
struct CDockingBarWinTraits : CWinTraits<t_dwStyle,t_dwExStyle>
{
	typedef dockwins::CDocker CDocker;
};

typedef CDockingBarWinTraits<WS_OVERLAPPEDWINDOW| WS_POPUP/* WS_CHILD*/ | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,WS_EX_TOOLWINDOW/* WS_EX_CLIENTEDGE*/>    CSimpleDockingBarWinTraits;

template <class T,
          class TBase = CWindow,
          class TWinTraits = CSimpleDockingBarWinTraits>
class ATL_NO_VTABLE CDockingWindowBaseImpl : public CWindowImpl< T, TBase, TWinTraits >
{
    typedef CWindowImpl< T, TBase, TWinTraits >   baseClass;
    typedef CDockingWindowBaseImpl< T, TBase, TWinTraits >       thisClass;
	typedef typename TWinTraits::CDocker	CDocker;
protected:
	class CGhostMoveTracker : public CDDTrackerBaseT<CGhostMoveTracker>
	{
//probably better use GetSystemMetrics
        enum{GhostRectSideSize=3};
	public:
		CGhostMoveTracker(const CDocker& docker,const POINT& pt,DFDOCKRECT& dockHdr)
			:m_docker(docker),m_dockHdr(dockHdr),m_dc(NULL)
		{
			m_offset.cx=m_dockHdr.rect.left-pt.x;
			m_offset.cy=m_dockHdr.rect.top-pt.y;
			m_size.cx=m_dockHdr.rect.right-m_dockHdr.rect.left;
			m_size.cy=m_dockHdr.rect.bottom-m_dockHdr.rect.top;
		}
		void DrawGhostRect(CDC& dc,RECT* pRect)
		{
			CBrush brush((HBRUSH)CDCHandle::GetHalftoneBrush());
			if(!brush.IsNull())
			{
				HBRUSH hBrushOld = dc.SelectBrush(brush);

				dc.PatBlt(pRect->left, pRect->top,
						  pRect->right-pRect->left,GhostRectSideSize, PATINVERT);
				dc.PatBlt(pRect->left, pRect->bottom-GhostRectSideSize,
						  pRect->right-pRect->left,GhostRectSideSize, PATINVERT);

				dc.PatBlt(pRect->left, pRect->top+GhostRectSideSize,
						  GhostRectSideSize,pRect->bottom-pRect->top-2*GhostRectSideSize, PATINVERT);
				dc.PatBlt(pRect->right-GhostRectSideSize, pRect->top+GhostRectSideSize,
						  GhostRectSideSize,pRect->bottom-pRect->top-2*GhostRectSideSize, PATINVERT);


				dc.SelectBrush(hBrushOld);
			}

		}
		void CleanGhostRect(CDC& dc,RECT* pRect)
		{
			DrawGhostRect(dc,pRect);
		}
		void BeginDrag()
		{
			DrawGhostRect(m_dc,&m_dockHdr.rect);
		}
		void EndDrag(bool /*bCanceled*/)
		{
			CleanGhostRect(m_dc,&m_dockHdr.rect);
		}
		void OnMove(long x, long y)
		{
			CleanGhostRect(m_dc,&m_dockHdr.rect);
			m_dockHdr.rect.left=x;
			m_dockHdr.rect.top=y;
			::ClientToScreen(m_dockHdr.hdr.hWnd,reinterpret_cast<POINT*>(&m_dockHdr.rect));
			m_dockHdr.rect.right=m_dockHdr.rect.left+m_size.cx;
			m_dockHdr.rect.bottom=m_dockHdr.rect.top+m_size.cy;
			m_docker.AdjustDragRect(&m_dockHdr);
			if((GetKeyState(VK_CONTROL) & 0x8000) || !m_docker.AcceptDock(&m_dockHdr))
			{
				m_dockHdr.hdr.hBar=HNONDOCKBAR;
				m_dockHdr.rect.left=x+m_offset.cx;
				m_dockHdr.rect.top=y+m_offset.cy;
				m_dockHdr.rect.right=m_dockHdr.rect.left+m_size.cx;
				m_dockHdr.rect.bottom=m_dockHdr.rect.top+m_size.cy;
			}
			DrawGhostRect(m_dc,&m_dockHdr.rect);
		}
		bool ProcessWindowMessage(MSG* pMsg)
		{
			bool bHandled=false;
			switch(pMsg->message)
			{
				case WM_KEYDOWN:
				case WM_KEYUP:
					if(pMsg->wParam==VK_CONTROL)
					{
						CPoint point(pMsg->pt.x,pMsg->pt.y);
						::ScreenToClient(m_dockHdr.hdr.hWnd,&point);
						OnMove(point.x,point.y);
						bHandled=true;
					}
					break;
			}
		   return bHandled;
		}
	protected:
		const CDocker&	m_docker;
		CWindowDC		m_dc;
		DFDOCKRECT&		m_dockHdr;
		SIZE			m_size;
		SIZE			m_offset;
	};
public:
	CDockingWindowBaseImpl()
		:m_hBarOwner(HNONDOCKBAR)
	{
		m_rcUndock.SetRectEmpty();
	}

	HWND Create(HWND hDockingFrameWnd, RECT& rcPos, LPCTSTR szWindowName = NULL,
		DWORD dwStyle = 0, DWORD dwExStyle = 0,
		UINT nID = 0, LPVOID lpCreateParam = NULL)
	{
		m_docker=CDocker(hDockingFrameWnd);
		return baseClass::Create(hDockingFrameWnd, rcPos, szWindowName ,
									dwStyle , dwExStyle , nID , lpCreateParam);
	}
#ifdef DF_AUTO_HIDE_FEATURES
	BOOL IsWindowVisible() const
	{
		BOOL bRes=IsPinned();
		if(!bRes)
			bRes=baseClass::IsWindowVisible();
		return bRes;
	}
	bool IsPinned() const
	{
		bool bRes=IsDocking();
		if(bRes)
		{
			DFMHDR dockHdr;
//			dockHdr.code=DC_ISPINNED;
			dockHdr.hWnd=m_hWnd;
			dockHdr.hBar=GetOwnerDockingBar();
			bRes=m_docker.IsPinned(&dockHdr);
		}
		return bRes;
	}
#endif
	HDOCKBAR GetOwnerDockingBar() const
	{
		return m_hBarOwner;
	}

	bool GetDockingPosition(DFDOCKPOS* pHdr) const
	{
		assert(::IsWindow(m_hWnd));
		bool bRes=true;
		pHdr->hdr.hBar=GetOwnerDockingBar();
		if(IsDocking())
		{
			pHdr->hdr.hWnd=m_hWnd;
//		    pHdr->hdr.code=DC_GETDOCKPOSITION;
			bRes=m_docker.GetDockingPosition(pHdr);
		}
		return bRes;
	}

	bool GetDockingWindowPlacement(DFDOCKPOSEX* pHdr) const
	{
		bool bRes=true;
		pHdr->bDocking=IsDocking();
		if(pHdr->bDocking)
		{
			::CopyRect(&pHdr->rect,&m_rcUndock);
			bRes=GetDockingPosition(&(pHdr->dockPos));
		}
		else
			GetWindowRect(&pHdr->rect);
		return bRes;
	}
	bool SetDockingPosition(DFDOCKPOS* pHdr)
	{
		assert(::IsWindow(m_hWnd));
		if(IsDocking())
					Undock();
		pHdr->hdr.hWnd=m_hWnd;
//	    pHdr->hdr.code=DC_SETDOCKPOSITION;
		return m_docker.SetDockingPosition(pHdr);
	}

	bool SetDockingWindowPlacement(DFDOCKPOSEX* pHdr)
	{
		bool bRes=true;
		if(pHdr->bDocking)
		{
			bRes=SetDockingPosition(&(pHdr->dockPos));
			::CopyRect(&m_rcUndock,&pHdr->rect);
		}
		else
		{
			if(IsDocking())
						Undock();
			bRes=(SetWindowPos(HWND_TOP,&(pHdr->rect),SWP_SHOWWINDOW | SWP_NOACTIVATE)!=FALSE);
		}
		return bRes;
	}

	bool IsDocking() const
	{
		return GetOwnerDockingBar()!=HNONDOCKBAR;
	}

	bool Float(LPCRECT pRc,UINT flags=SWP_SHOWWINDOW | SWP_NOACTIVATE | SWP_FRAMECHANGED,HWND hWndInsertAfter=HWND_TOP)
	{
		bool bRes=IsDocking();
		if(bRes)
		{
			if(Undock())
				bRes=(SetWindowPos(hWndInsertAfter,pRc,flags)!=FALSE);
		}
		return bRes;
	}

	bool Float()
	{
		bool bRes=!m_rcUndock.IsRectEmpty();
		if(bRes)
			bRes=Float(&m_rcUndock);
		return bRes;
	}

	virtual bool Undock()
	{
		assert(IsDocking());
		DFMHDR dockHdr;
//		dockHdr.code=DC_UNDOCK;
		dockHdr.hWnd=m_hWnd;
		dockHdr.hBar=GetOwnerDockingBar();
		return m_docker.Undock(&dockHdr);
	}

	bool OnClosing()
	{
		bool bRes=true;
		if(IsDocking())
			bRes=Undock();
		return bRes;
	}

	virtual bool DockMe(DFDOCKRECT* pHdr)
	{
		return m_docker.Dock(pHdr);
	}

    bool BeginMoving(const POINT& point)
    {
		DFDOCKRECT dockHdr;
//		dockHdr.hdr.code=DC_ACCEPT;
		dockHdr.hdr.hWnd=m_hWnd;
		dockHdr.hdr.hBar=HNONDOCKBAR;//GetOwnerDockingBar();

		if(m_rcUndock.IsRectEmpty())
		{
			GetWindowRect(&dockHdr.rect);
//			dockHdr.hdr.code=DC_ADJUSTDRAGRECT;
			m_docker.AdjustDragRect(&dockHdr);
			m_rcUndock.CopyRect(&dockHdr.rect);
		}
		GetWindowRect(&dockHdr.rect);
		CPoint pt(point);
		ClientToScreen(&pt);

		float ratio=float(pt.x-dockHdr.rect.left)/(dockHdr.rect.right-dockHdr.rect.left);
		dockHdr.rect.left=pt.x-long(ratio*m_rcUndock.Width());
		ratio=float(pt.y-dockHdr.rect.top)/(dockHdr.rect.bottom-dockHdr.rect.top);
		dockHdr.rect.top=pt.y-long(ratio*m_rcUndock.Height());

		dockHdr.rect.right=dockHdr.rect.left+m_rcUndock.Width();
		dockHdr.rect.bottom=dockHdr.rect.top+m_rcUndock.Height();

		CGhostMoveTracker tracker(m_docker,point,dockHdr);
		if(TrackDragAndDrop(tracker,m_hWnd))
		{

			CPoint ptCur;
			::GetCursorPos(&ptCur);
			if((dockHdr.hdr.hBar!=HNONDOCKBAR)
				|| (ptCur.x!=pt.x) || (ptCur.y!=pt.y))
			{
				if(IsDocking())
							Undock();
				if(dockHdr.hdr.hBar!=HNONDOCKBAR)
//						m_docker.Dock(&dockHdr);
					DockMe(&dockHdr);
				else
					SetWindowPos(HWND_TOP,&(dockHdr.rect),SWP_SHOWWINDOW | SWP_FRAMECHANGED);
			}
		}
		return true;
	}
	void OnDocked(HDOCKBAR hBar,bool /*bHorizontal*/)
	{
		assert(!IsDocking());
		m_hBarOwner=hBar;
	}
	void OnUndocked(HDOCKBAR /*hBar*/)
	{
		assert(IsDocking());
		m_hBarOwner=HNONDOCKBAR;
	}
////////////////messages handlers/////////////////////////////////
	BEGIN_MSG_MAP(thisClass)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBackground)
		MESSAGE_HANDLER(WM_CLOSE, OnClose)
        MESSAGE_HANDLER(WM_NCLBUTTONDOWN,OnNcLButtonDown)
		MESSAGE_HANDLER(WM_WINDOWPOSCHANGED, OnWindowPosChanged)
		MESSAGE_HANDLER(WMDF_NDOCKSTATECHANGED,OnDockStateChanged)
		MESSAGE_HANDLER(WM_MENUCHAR,OnMenuChar)
	END_MSG_MAP()

	LRESULT OnEraseBackground(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	{
		return 1;
	}

	LRESULT OnWindowPosChanged(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
	{
		LPWINDOWPOS pWPos=reinterpret_cast<LPWINDOWPOS>(lParam);

		if(
			(pWPos->flags&SWP_SHOWWINDOW
			|| ((pWPos->flags&(SWP_NOSIZE | SWP_NOMOVE))!=(SWP_NOSIZE | SWP_NOMOVE)))
				&& !IsDocking()
					&& IsWindowVisible()
						/*&& !(pWPos->flags&SWP_HIDEWINDOW)*/)
		{
			m_rcUndock.left=pWPos->x;
			m_rcUndock.top=pWPos->y;
			m_rcUndock.right=m_rcUndock.left+pWPos->cx;
			m_rcUndock.bottom=m_rcUndock.top+pWPos->cy;
		}
		bHandled = FALSE;
		return 0;
	}

	LRESULT OnClose(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM/* lParam*/, BOOL& bHandled)
	{
		T* pThis=reinterpret_cast<T*>(this);
		bHandled=!pThis->OnClosing();
		return !bHandled;
	}

    LRESULT OnNcLButtonDown(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
		bHandled=(wParam==HTCAPTION);
        if(bHandled)
        {
			SetWindowPos(HWND_TOP,0,0,0,0,SWP_NOMOVE | SWP_NOSIZE );
			POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
			if(::DragDetect(m_hWnd,pt))
			{
				T* pThis=static_cast<T*>(this);
				pThis->ScreenToClient(&pt);
				pThis->BeginMoving(pt);
			}
        }
        return 0;
    }

	LRESULT OnDockStateChanged(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
	{
         T* pThis=static_cast<T*>(this);
		if(wParam!=FALSE)
			pThis->OnDocked(reinterpret_cast<HDOCKBAR>(lParam),(DOCKED2HORIZONTAL(wParam)==TRUE));
		else
			pThis->OnUndocked(reinterpret_cast<HDOCKBAR>(lParam));
		return TRUE;
	}

	LRESULT OnMenuChar(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
	{
		LRESULT lRes = DefWindowProc(uMsg, wParam, lParam);
		if( HIWORD(lRes) == MNC_IGNORE ) {
			return ::SendMessage(this->GetTopLevelParent(), uMsg, wParam, lParam);
		}
		return lRes;
	}
protected:
	CDocker		m_docker;
	HDOCKBAR	m_hBarOwner;
	CRect		m_rcUndock;
};

template<class T>
class CDockingWindowPlacement : public DFDOCKPOS
{
public:
	CDockingWindowPlacement(T& dw)
		:DFDOCKPOS(0),m_dw(dw)
	{
		hdr.hBar=HNONDOCKBAR;
	}
	bool IsVisible() const
	{
		assert(::IsWindow(dw.m_hWnd));
		return (m_dw.IsWindowVisible()!=FALSE);
	}
	bool IsDocking() const
	{
		assert(::IsWindow(dw.m_hWnd));
		return m_dw.IsDocking();
	}
	bool Hide()
	{
		assert(::IsWindow(dw.m_hWnd));
		bool bRes=true;
		if(m_dw.IsDocking())
		{
			bRes=m_dw.GetDockingPosition(this);
			assert(bRes);
			if(bRes)
				bRes=m_dw.Undock();
			assert(bRes);
		}
		else
			hdr.hBar=HNONDOCKBAR;
		return (bRes && m_dw.ShowWindow(SW_HIDE));
	}
	bool Show()
	{
		assert(::IsWindow(dw.m_hWnd));
		bool bRes=true;
		if(hdr.hBar!=HNONDOCKBAR)
			bRes=m_dw.SetDockingPosition(this);
		else
			m_dw.ShowWindow(SW_SHOW);
		assert(bRes);
		return bRes;
	}
	CDockingWindowPlacement& operator = (const DFDOCKPOS& pos)
	{
		dwDockSide=pos.dwDockSide;
		nBar=pos.nBar;
		fPctPos=pos.fPctPos;
		nWidth=pos.nWidth;
		nHeight=pos.nHeight;
		return *this;
	}
protected:
	T&	m_dw;
};

class CCaptionBase : public COrientedRect
{
	typedef COrientedRect baseClass;
public:
	struct CButton: CRect
	{
        virtual void CalculateRect(CRect& rc,bool bHorizontal)=0;
		virtual void Draw (CDC& dc)=0;
		virtual void Press(HWND /*hWnd*/){};
		virtual void Release(HWND /*hWnd*/){};
		virtual void Hot(HWND /*hWnd*/){}
	};

	class CBtnClickTracker : public CDDTrackerBaseT<CBtnClickTracker>
	{
	public:
		CBtnClickTracker(HWND hWnd,CButton& btn)
			:m_hWnd(hWnd),m_btn(btn)
		{
		}
		void BeginDrag()
		{
			m_btn.Press(m_hWnd);
		}
		void EndDrag(bool /*bCanceled*/)
		{
			m_btn.Release(m_hWnd);
		}
	protected:
		HWND		m_hWnd;
		CButton&	m_btn;
	};
protected:
	template<class T>
	class CHotBtnTracker : public CDDTrackerBaseT<CHotBtnTracker<T> >
	{
	public:
		CHotBtnTracker(CButton& btn,T& owner,HWND hWnd,int nHotHitTest)
			:m_btn(btn),m_owner(owner),m_hWnd(hWnd),m_nHotHitTest(nHotHitTest),m_bDoAction(false)
		{
			CRect rc;
			::GetClientRect(m_hWnd,&rc);
			m_offset.x=rc.left;
			m_offset.y=rc.top;
			::ClientToScreen(m_hWnd,&m_offset);
			::GetWindowRect(hWnd,&rc);
			m_offset.x-=rc.left;
			m_offset.y-=rc.top;
		}
		void BeginDrag()
		{
			m_btn.Hot(m_hWnd);
		}
		void EndDrag(bool /*bCanceled*/)
		{
			m_btn.Release(m_hWnd);
		}

		void OnDropLeftButton(long x, long y)
		{
			CPoint pt(x+m_offset.x,y+m_offset.y);
			m_bDoAction=(m_owner.HitTest(pt)==m_nHotHitTest);

		}
		void OnMove(long x, long y)
		{
			CPoint pt(x+m_offset.x,y+m_offset.y);
			if(m_owner.HitTest(pt)!=m_nHotHitTest)
				::ReleaseCapture();
		}
		bool ProcessWindowMessage(MSG* pMsg)
		{
			bool bRes=false;
			bRes=(pMsg->message==WM_LBUTTONDOWN);
			if(bRes)
				m_btn.Press(m_hWnd);
			return bRes;
		}

		operator bool () const
		{
			return m_bDoAction;
		}
	protected:
		bool		m_bDoAction;
		CPoint		m_offset;
		T&			m_owner;
		CButton&	m_btn;
		HWND		m_hWnd;
		int			m_nHotHitTest;
	};
public:
	CCaptionBase(bool bHorizontal=true)
		:COrientedRect(bHorizontal,::GetSystemMetrics(SM_CYSMCAPTION))
	{
	}
	CCaptionBase(unsigned long thickness,bool bHorizontal=true)
		:COrientedRect(bHorizontal,thickness)
	{
	}

	bool CalculateRect(CRect& rc,bool bTop)
	{
		return baseClass::CalculateRect(rc,bTop);
	}
	LRESULT HitTest(const CPoint& /*pt*/) const
	{
		return HTNOWHERE;
	}
	void Draw(HWND /*hWnd*/,CDC& dc)
	{
		dc.FillRect(this,(HBRUSH)LongToPtr(COLOR_3DFACE + 1));
	}
	bool OnAction(HWND hWnd,unsigned int nHitTest)
	{
		return HotTrack(hWnd,nHitTest);
	}
	bool HotTrack(HWND /*hWnd*/,unsigned int /*nHitTest*/)
	{
		return false;
	}
	void UpdateMetrics()
	{
		// Override in derived class if it depends on system metrics
	}
	virtual void ThemeChanged()
	{
		// Override to support changed themes message
	}
};


template <class TCaption,DWORD t_dwStyle = 0, DWORD t_dwExStyle = 0>
struct CDockingWindowTraits : CDockingBarWinTraits<t_dwStyle,t_dwExStyle>
{
	typedef TCaption CCaption;
};

typedef CDockingWindowTraits<CCaptionBase,WS_OVERLAPPEDWINDOW | WS_POPUP/* WS_CHILD*/ | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,WS_EX_TOOLWINDOW/* WS_EX_CLIENTEDGE*/> CEmptyTitleDockingWindowTraits;

template <class T,
          class TBase = CWindow,
          class TDockingWinTraits = CEmptyTitleDockingWindowTraits>
class ATL_NO_VTABLE CTitleDockingWindowBaseImpl :
	public CDockingWindowBaseImpl< T, TBase, TDockingWinTraits >
{
    typedef CDockingWindowBaseImpl< T, TBase, TDockingWinTraits >		baseClass;
    typedef CTitleDockingWindowBaseImpl< T, TBase, TDockingWinTraits >	thisClass;
protected:
	typedef typename TDockingWinTraits::CCaption	CCaption;
public:
	LRESULT NcHitTest(const CPoint& pt)
	{
		LRESULT lRes=m_caption.HitTest(pt);
		if(lRes==HTNOWHERE)
				lRes=HTCLIENT;
		else
		{
			if(GetCapture()==NULL)
				m_caption.HotTrack(m_hWnd,(unsigned int)lRes);
		}
		return lRes;
	}

	LRESULT ThemeChanged()
	{
		m_caption.ThemeChanged();
		return 0;
	}

	void GetMinMaxInfo(LPMINMAXINFO pMinMaxInfo) const
	{
		long width=m_caption.GetThickness();
		if(pMinMaxInfo->ptMinTrackSize.y<width)
			pMinMaxInfo->ptMinTrackSize.y=width;
		if(pMinMaxInfo->ptMinTrackSize.x<width)
			pMinMaxInfo->ptMinTrackSize.x=width;
	}

	void NcCalcSize(CRect* pRc)
	{
		DWORD style = GetWindowLong(GWL_STYLE);
		if((style&WS_CAPTION)==0)
			m_caption.SetRectEmpty();
		else
			m_caption.CalculateRect(*pRc,true);
	}
	void NcDraw(CDC& dc)
	{
		DWORD style = GetWindowLong(GWL_STYLE);
		if((style&WS_CAPTION)!=0)
			m_caption.Draw(m_hWnd,dc);
	}
	void OnDocked(HDOCKBAR hBar,bool bHorizontal)
	{
		m_caption.SetOrientation(!bHorizontal);
		baseClass::OnDocked(hBar,bHorizontal);
	}
	void OnUndocked(HDOCKBAR hBar)
	{
		m_caption.SetOrientation(true);
		baseClass::OnUndocked(hBar);
	}
	bool CloseBtnPress()
	{
		PostMessage(WM_CLOSE);
		return false;
	}
#ifdef DF_AUTO_HIDE_FEATURES
	bool PinUp(const CDockingSide& side)
	{
		CRect rc;
		GetWindowRect(&rc);
		T* pThis=static_cast<T*>(this);
		assert(rc.Width()>0);
		assert(rc.Height()>0);
		return pThis->PinUp(side,(side.IsHorizontal() ? rc.Width() : rc.Height()));
	}

	bool PinUp(const CDockingSide& side,unsigned long width,bool bVisualize=false)
	{
		if(IsDocking())
					Undock();
		DFPINUP pinHdr;
		pinHdr.hdr.hWnd=m_hWnd;
		pinHdr.hdr.hBar=GetOwnerDockingBar();
//		pinHdr.hdr.code=DC_PINUP;
		pinHdr.dwDockSide=side;
		pinHdr.nWidth=width;
		pinHdr.dwFlags= bVisualize ? DFPU_VISUALIZE : 0 ;
		pinHdr.n=0;
		return m_docker.PinUp(&pinHdr);
	}

	bool PinBtnPress(bool bVisualize=true)
	{
		assert(IsDocking());
		DFDOCKPOS dockHdr={0};
//		dockHdr.hdr.code=DC_GETDOCKPOSITION;
		dockHdr.hdr.hWnd=m_hWnd;
		dockHdr.hdr.hBar=GetOwnerDockingBar();
		bool bRes=GetDockingPosition(&dockHdr);
		if(bRes)
		{
			bRes=Undock();
			if(bRes)
			{
				T* pThis=static_cast<T*>(this);
				bRes=pThis->PinUp(CDockingSide(dockHdr.dwDockSide),dockHdr.nWidth,bVisualize);
			}
		}
		return bRes;
	}
#endif
protected:
	BEGIN_MSG_MAP(thisClass)
		if(IsDocking())
		{
			MESSAGE_HANDLER(WM_WINDOWPOSCHANGING,OnWindowPosChanging)
			MESSAGE_HANDLER(WM_NCCALCSIZE, OnNcCalcSize)
			MESSAGE_HANDLER(WM_NCACTIVATE, OnNcActivate)
			MESSAGE_HANDLER(WM_NCHITTEST,OnNcHitTest)
			MESSAGE_HANDLER(WM_NCPAINT,OnNcPaint)
			MESSAGE_HANDLER(WM_SETTEXT,OnCaptionChange)
			MESSAGE_HANDLER(WM_SETICON,OnCaptionChange)
			MESSAGE_HANDLER(WM_NCLBUTTONDOWN, OnNcLButtonDown)
			MESSAGE_HANDLER(WM_NCLBUTTONUP,OnNcLButtonUp)
			MESSAGE_HANDLER(WM_NCLBUTTONDBLCLK,OnNcLButtonDblClk)
#ifdef WM_THEMECHANGED
			MESSAGE_HANDLER(WM_THEMECHANGED,OnThemeChanged)
#endif
#ifdef DF_FOCUS_FEATURES
			ATLASSERT(CDockingFocusHandler::This());
			CHAIN_MSG_MAP_ALT_MEMBER((*CDockingFocusHandler::This()),0)
#endif
		}
		MESSAGE_HANDLER(WM_GETMINMAXINFO,OnGetMinMaxInfo)
		MESSAGE_HANDLER(WM_SETTINGCHANGE,OnSettingChange)
		MESSAGE_HANDLER(WM_SYSCOLORCHANGE,OnSysColorChange)
		CHAIN_MSG_MAP(baseClass)
	END_MSG_MAP()

	LRESULT OnGetMinMaxInfo(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
	{
		T* pThis=static_cast<T*>(this);
		LRESULT lRes=pThis->DefWindowProc(uMsg,wParam,lParam);
		pThis->GetMinMaxInfo(reinterpret_cast<LPMINMAXINFO>(lParam));
		return lRes;
	}

	LRESULT OnSettingChange(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	{
		if(!IsDocking())
		{
			// If we're floating, we're a top level window.
			// We might be getting this message before the main frame
			// (which is also a top-level window).
			// The main frame handles this message, and refreshes
			// system settings cached in CDWSettings.  In case we
			// are getting this message before the main frame,
			// update these cached settings (so that when we update
			// our caption's settings that depend on them,
			// its using the latest).
			CDWSettings settings;
			settings.Update();

			// In addition, because we are a top-level window,
			// we should be sure to send this message to all our descendants
			// in case there are common controls and other windows that
			// depend on cached system metrics.
			this->SendMessageToDescendants(uMsg, wParam, lParam, TRUE);
		}

		m_caption.UpdateMetrics();

		T* pThis=static_cast<T*>(this);
		pThis->SetWindowPos(NULL,0,0,0,0,SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);

		bHandled = FALSE;
		return 1;
	}

	LRESULT OnSysColorChange(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	{
		if(!IsDocking())
		{
			// If we're floating, we're a top level window.
			// We might be getting this message before the main frame
			// (which is also a top-level window).
			// The main frame handles this message, and refreshes
			// system settings cached in CDWSettings.  In case we
			// are getting this message before the main frame,
			// update these cached settings (so that when we update
			// our caption's settings that depend on them,
			// its using the latest).
			CDWSettings settings;
			settings.Update();

			// In addition, because we are a top-level window,
			// we should be sure to send this message to all our descendants
			// in case there are common controls and other windows that
			// depend on cached system metrics.
			this->SendMessageToDescendants(uMsg, wParam, lParam, TRUE);
		}

		m_caption.UpdateMetrics();

		T* pThis=static_cast<T*>(this);
		pThis->SetWindowPos(NULL,0,0,0,0,SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);

		bHandled = FALSE;
		return 1;
	}

	LRESULT OnWindowPosChanging(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	{
		return NULL;
	}

	LRESULT OnNcActivate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
	{
		bHandled=IsWindowEnabled();
		return TRUE;
	}

	LRESULT OnNcCalcSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/)
	{
		T* pThis=static_cast<T*>(this);
		CRect* pRc=reinterpret_cast<CRect*>(lParam);
		CPoint ptTop(pRc->TopLeft());
		(*pRc)-=ptTop;
		pThis->NcCalcSize(pRc);
		(*pRc)+=ptTop;
		return NULL;
	}

	LRESULT OnNcHitTest(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/)
	{
		CPoint pt(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		T* pThis=static_cast<T*>(this);
		CRect rcWnd;
		GetWindowRect(&rcWnd);
		pt.x-=rcWnd.TopLeft().x;
		pt.y-=rcWnd.TopLeft().y;
		return pThis->NcHitTest(pt);
	}

	LRESULT OnThemeChanged(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	{
		T* pThis=static_cast<T*>(this);
		return pThis->ThemeChanged();
	}
//OnSetIcon
//OnSetText
	LRESULT OnCaptionChange(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
	{
//		LockWindowUpdate();
		DWORD style = ::GetWindowLong(m_hWnd,GWL_STYLE);
		::SetWindowLong(m_hWnd, GWL_STYLE, style&(~WS_CAPTION));
		LRESULT lRes=DefWindowProc(uMsg,wParam,lParam);
		::SetWindowLong(m_hWnd, GWL_STYLE, style);
		T* pThis=static_cast<T*>(this);
		pThis->SetWindowPos(NULL,0,0,0,0,SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
//		CWindowDC dc(m_hWnd);
//		pThis->NcDraw(dc);
//		LockWindowUpdate(FALSE);
		return lRes;
	}
	LRESULT OnNcPaint(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	{
		CWindowDC dc(m_hWnd);
		T* pThis=static_cast<T*>(this);
		pThis->NcDraw(dc);
		return NULL;
	}

	LRESULT OnNcLButtonDown(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
	{
		bHandled=m_caption.OnAction(m_hWnd,(unsigned int)wParam);
		if(!bHandled && wParam == HTCAPTION)
			this->SetFocus();

		return !bHandled;
	}

	LRESULT OnNcLButtonUp(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
	{
		T* pThis=static_cast<T*>(this);
		HWND hWndFocus = ::GetFocus();
		switch(wParam)
		{
			case HTCLOSE:
				bHandled=pThis->CloseBtnPress();
				break;
#ifdef DF_AUTO_HIDE_FEATURES
			case HTPIN:
				bHandled=pThis->PinBtnPress();
				break;
#endif
			default:
				bHandled=FALSE;
		}

		if(hWndFocus != ::GetFocus())
		{
			if(::IsWindow(hWndFocus) && ::IsWindowVisible(hWndFocus))
			{
				::SetFocus(hWndFocus);
			}
			else
			{
				::SetFocus(this->GetTopLevelParent());
			}
		}
		return 0;
	}
	LRESULT OnNcLButtonDblClk(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	{
		return 0;
	}
protected:
	CCaption	m_caption;
};

template <class T,
          class TBase = CWindow,
          class TDockingWinTraits = CEmptyTitleDockingWindowTraits>
class ATL_NO_VTABLE CTitleDockingWindowImpl
			: public CTitleDockingWindowBaseImpl< T, TBase, TDockingWinTraits >
{
    typedef CTitleDockingWindowBaseImpl< T, TBase, TDockingWinTraits >	baseClass;
    typedef CTitleDockingWindowImpl< T, TBase, TDockingWinTraits >		thisClass;
public:
	CTitleDockingWindowImpl()
	{
		::ZeroMemory(&m_pos, sizeof(DFDOCKPOS));
		m_pos.hdr.hBar=HNONDOCKBAR;
	}
	virtual bool Undock()
	{
		assert(IsDocking());
		GetDockingPosition(&m_pos);
		return baseClass::Undock();
	}
	virtual bool Hide()
	{
		bool bRes=true;
		if(IsDocking())
		{
			bRes=GetDockingPosition(&m_pos);
			assert(bRes);
			if(bRes)
			//	bRes=Undock();
				bRes=Float(&m_rcUndock,SWP_HIDEWINDOW);
			assert(bRes);
		}
		else
			m_pos.hdr.hBar=HNONDOCKBAR;
		return (bRes && ShowWindow(SW_HIDE));
	}
	virtual bool Show()
	{
		bool bRes=true;
		if(m_pos.hdr.hBar!=HNONDOCKBAR)
			bRes=SetDockingPosition(&m_pos);
		else
			ShowWindow(SW_SHOW);
		assert(bRes);
		return bRes;
	}

	bool Toggle()
	{
		bool bRes=(static_cast<T*>(this)->IsWindowVisible()!=FALSE);
		if(bRes)
			Hide();
		else
			Show();
		return bRes;
	}
	bool GetDockingWindowPlacement(DFDOCKPOSEX* pHdr) const
	{
		bool bRes=baseClass::GetDockingWindowPlacement(pHdr);
		pHdr->bVisible=static_cast<const T*>(this)->IsWindowVisible();
		if((!pHdr->bDocking)
			&& (!pHdr->bVisible)
				&& (m_pos.hdr.hBar!=HNONDOCKBAR))
			::CopyMemory(&pHdr->dockPos,&m_pos,sizeof(DFDOCKPOS));
		return bRes;
	}
	bool SetDockingWindowPlacement(DFDOCKPOSEX* pHdr)
	{
		bool bRes=true;
		pHdr->dockPos.hdr.hWnd=m_hWnd;
		::CopyMemory(&m_pos,&(pHdr->dockPos),sizeof(DFDOCKPOS));
		if(pHdr->bVisible)
			bRes=baseClass::SetDockingWindowPlacement(pHdr);
		else
		{
			if(IsDocking())
				Undock();
			::CopyRect(&m_rcUndock,&pHdr->rect);
			bRes=(SetWindowPos(NULL,&m_rcUndock,
					SWP_NOZORDER | SWP_HIDEWINDOW |	SWP_NOACTIVATE )!=FALSE);
		}

#ifdef DF_AUTO_HIDE_FEATURES
		// Update from Peter Carlson.
		//  A fix for the pin restore problem.
		CDockingSide side(pHdr->dockPos.dwDockSide);
		if (side.IsPinned())
			PinUp(side, (side.IsHorizontal() ? pHdr->dockPos.nHeight : pHdr->dockPos.nWidth));
		//
#endif

		return bRes;
	}

	bool CanBeClosed(unsigned long /*param*/)
	{
		HWND hWndFocus = ::GetFocus();

		this->Hide();

		if(hWndFocus != ::GetFocus())
		{
			if(::IsWindow(hWndFocus) && ::IsWindowVisible(hWndFocus))
			{
				::SetFocus(hWndFocus);
			}
			else
			{
				::SetFocus(this->GetTopLevelParent());
			}
		}
		return false;
	}

	BEGIN_MSG_MAP(thisClass)
		MESSAGE_HANDLER(WM_CLOSE, OnClose)
		MESSAGE_HANDLER(WM_NCLBUTTONDBLCLK, OnNcLButtonDblClk)
		CHAIN_MSG_MAP(baseClass)
	END_MSG_MAP()

	LRESULT OnClose(UINT /*uMsg*/, WPARAM wParam, LPARAM/* lParam*/, BOOL& bHandled)
	{
		bHandled=!(static_cast<T*>(this)->CanBeClosed((unsigned long)wParam));
		return 0;
	}
	LRESULT OnNcLButtonDblClk(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
	{
		if(IsIconic() || IsZoomed())
		{
			// Docking a minimised window is a bad idea!
			// Let Windows restore.
			bHandled = FALSE;
			return 0;
		}

		if(wParam==HTCAPTION)
		{
			if(IsDocking())
				Float();
			else
			{
				if(m_pos.hdr.hBar!=HNONDOCKBAR)
					SetDockingPosition(&m_pos);
			}
		}
		return 0;
	}
protected:
	DFDOCKPOS m_pos;
};

#define COMMAND_TOGGLE_MEMBER_HANDLER(id, member) \
	if(uMsg == WM_COMMAND && id == LOWORD(wParam)) \
	{ \
		member.Toggle(); \
	}

//please don't use CStateKeeper class anymore!
//this class is obsolete and provided only for compatibility with previous versions.
//the CTitleDockingWindowImpl class provide all functionality of CStateKeeper
template<class T>
struct CStateKeeper : public T
{
};

//please don't use CTitleExDockingWindowImpl class anymore!
//this class is obsolete and provided only for compatibility with previous versions.
//the CTitleDockingWindowImpl class provide all functionality of CTitleExDockingWindowImpl
template <class T,
          class TBase = CWindow,
          class TDockingWinTraits = CEmptyTitleDockingWindowTraits>
struct ATL_NO_VTABLE CTitleExDockingWindowImpl : CTitleDockingWindowImpl< T, TBase, TDockingWinTraits >
{
};

}//namespace dockwins
#endif // __WTL_DW__DOCKINGWINDOW_H__
