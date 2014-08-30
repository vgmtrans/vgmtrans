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

#ifndef __WTL_DW__WNDFRMPKG_H__
#define __WTL_DW__WNDFRMPKG_H__

#pragma once

#ifndef __ATLMISC_H__
	#error WndFrmPkg.h requires atlmisc.h to be included first
#endif

#include <memory>
#include "ssec.h"
#include "DDTracker.h"
#ifdef USE_BOOST
#include<boost/smart_ptr.hpp>
#endif

namespace dockwins{

class CWndFrame
{
public:
	typedef long position;
	typedef long distance;

	class CCmp
	{
	public:
		CCmp(HWND hWnd)	:m_hWnd(hWnd)
		{
		}
		bool operator ()(const CWndFrame& frame) const
		{
			return (frame.hwnd() == m_hWnd);
		}
	protected:
		HWND m_hWnd;
	};

	CWndFrame(position pos=0,HWND hWnd=NULL)
		:m_hWnd(hWnd),m_pos(pos)
	{
	}

	HWND hwnd() const
	{
		return m_hWnd;
	}

	operator position() const
	{
		return (position)m_pos;
	}

	CWndFrame& operator += (position val)
	{
		m_pos+=val;
		return *this;
	}
	CWndFrame& operator -= (position val)
	{
		m_pos-=val;
		return *this;
	}

	CWndFrame& operator = (position pos)
	{
		m_pos=pos;
		return *this;
	}

	CWndFrame& operator = (double pos)
	{
		m_pos=pos;
		return *this;
	}

	double get_real()
	{
		return m_pos;
	}

	HDWP DeferFramePos(HDWP hdwp,long x1,long y1,long x2,long y2) const
	{
		return ::DeferWindowPos(hdwp,hwnd(),
								NULL,
								x1,y1,
								x2-x1,y2-y1,
								SWP_NOZORDER | SWP_NOACTIVATE);
	}
	void GetMinMaxInfo(LPMINMAXINFO pMinMaxInfo) const
	{
		::SendMessage(m_hWnd,WM_GETMINMAXINFO,NULL,reinterpret_cast<LPARAM>(pMinMaxInfo));
	}
	distance MinDistance() const
	{
		DFMHDR dockHdr;
		dockHdr.code=DC_GETMINDIST;
		dockHdr.hWnd=m_hWnd;
		dockHdr.hBar=::GetParent(m_hWnd);
		assert(::IsWindow(dockHdr.hBar));
		return (distance)::SendMessage(dockHdr.hBar,WMDF_DOCK,NULL,reinterpret_cast<LPARAM>(&dockHdr));
	}
protected:
	double m_pos;
	mutable HWND m_hWnd;
};

//template<class T,const T::distance TMinDist=0>
//struct CWndFrameTraits : ssec::spraits<T, T::position, T::distance/*,TMinDist*/>
//{
//	typedef ssec::spraits<T,position,position/*,TMinDist*/> baseClass;
//	static distance min_distance(const T& x)
//	{
//		const distance dist=TMinDist;
//		return dist+x.MinDistance();
//	}
//
//};

template< class TFrame = CWndFrame , class TTraits = CDockingFrameTraits>
class CWndFramesPackageBase
{
	typedef typename TFrame CFrame;
	typedef typename TTraits CTraits;
	typedef typename CTraits::CSplitterBar	CSplitterBar;
	typedef CWndFramesPackageBase<CFrame,TTraits> thisClass;
protected:
	enum {splitterSize=CSplitterBar::sbThickness};
	typedef typename CFrame::position	position;

#if _MSC_VER >= 1310
	template<class T,const typename T::distance TMinDist=0>
#else
	template<class T,const T::distance TMinDist=0>
#endif
	struct CWndFrameTraits : ssec::spraits<T, typename T::position, typename T::distance/*,TMinDist*/>
	{
		typedef ssec::spraits<T,position,position/*,TMinDist*/> baseClass;
		static distance min_distance(const T& x)
		{
			const distance dist=TMinDist;
			return dist+x.MinDistance();
		}

	};
	typedef CWndFrameTraits<CFrame,splitterSize> CFrameTraits;
	typedef ssec::ssection<CFrame,CFrameTraits> CFrames;

	typedef typename CFrames::iterator				iterator;
	typedef typename CFrames::reverse_iterator		reverse_iterator;
	typedef typename CFrames::const_iterator			const_iterator;
	typedef typename CFrames::const_reverse_iterator	const_reverse_iterator;

	typedef ssec::bounds_type<position> CBounds;
protected:
	struct CEmbeddedSplitterBar : public CSplitterBar
	{
		CEmbeddedSplitterBar(bool bHorizontal,position vertex,const CRect& rcClient)
				:CSplitterBar(bHorizontal)
		{
			if(IsHorizontal())
			{
					top=vertex;
					bottom=top+GetThickness();
					left=rcClient.left;
					right=rcClient.right;
			}
			else
			{
					left=vertex;
					right=left+GetThickness();
					top=rcClient.top;
					bottom=rcClient.bottom;
			}

		}
	};
	class CEmbeddedSplitterBarPainter
	{
	public:
		CEmbeddedSplitterBarPainter(CDC& dc,bool bHorizontal,const CRect& rc)
			:m_dc(dc),m_rc(rc),m_bHorizontal(bHorizontal)
		{
		}
		void operator()(const CFrame& ref) const
		{
			CEmbeddedSplitterBar splitter(m_bHorizontal,ref,m_rc);
			splitter.Draw(m_dc);
		}
	protected:
		const	CRect& m_rc;
		bool	m_bHorizontal;
		CDC&	m_dc;
	};

	class CSplitterMoveTrackerBase : public IDDTracker//CDDTrackerBaseT<CSizeTrackerBase>
	{
	protected:
		void SetPosition()
		{
			assert(m_bounds.bind(m_pos)==m_pos);
			m_frames.set_position(m_i,m_pos);
			m_owner.Arrange(m_rc);
			m_wnd.RedrawWindow(&m_rc,NULL,RDW_INVALIDATE | RDW_UPDATENOW);
		}
	public:
		CSplitterMoveTrackerBase(HWND hWnd,thisClass& owner,const CPoint& pt,const CRect& rc)
			:m_wnd(hWnd),m_owner(owner),m_frames(owner.m_frames),m_rc(rc)
		{
			position pos=owner.IsHorizontal() ? pt.x : pt.y;
			m_i=m_frames.locate(pos);
			if(m_i!=m_frames.end())
			{
				m_pos=(*m_i);
				m_offset=pos-m_pos;
				m_frames.get_effective_bounds(m_i,m_bounds);
			}
		}
		operator iterator() const
		{
			return m_i;
		}
		void OnMove(long x, long y)
		{
			position pos = m_owner.IsHorizontal() ? x : y;
			pos=m_bounds.bind(pos-m_offset);
			if(pos!=m_pos)
			{
				m_pos=pos;
				Move();
			}
		}
		virtual void Move()=0;

	protected:
		thisClass&		m_owner;
		CFrames&		m_frames;
		iterator		m_i;
		CBounds			m_bounds;
		position		m_pos;
		position		m_offset;
		const CRect&	m_rc;
		CWindow			m_wnd;
	};

	friend class CSplitterMoveTrackerBase;

	class CSplitterMoveTrackerFull : public CSplitterMoveTrackerBase
	{
	public:
		CSplitterMoveTrackerFull(HWND hWnd,thisClass& owner,const CPoint& pt,const CRect& rc)
			:CSplitterMoveTrackerBase(hWnd,owner,pt,rc)
		{
		}
		void Move()
		{
			SetPosition();
		}
	};

	class CSplitterMoveTrackerGhost : public CSplitterMoveTrackerBase
	{
		typedef CEmbeddedSplitterBar CSplitterBar;
		typedef CSimpleSplitterBarSlider<CSplitterBar> CSlider;
	public:
		CSplitterMoveTrackerGhost(HWND hWnd,thisClass& owner,
									const CPoint& pt,const CRect& rc)
			:CSplitterMoveTrackerBase(hWnd,owner,pt,rc),
			  m_dc(NULL),m_splitter(!owner.IsHorizontal(),m_pos,rc),m_slider(m_splitter)
		{
			CPoint point(rc.TopLeft ());
			::ClientToScreen(hWnd,&point);
			CSize offset(point.x-rc.left,point.y-rc.top);
			m_splitter+=offset;
			m_ghOffset=owner.IsHorizontal()
									?offset.cx
									:offset.cy;
		}
		void BeginDrag()
		{
			m_splitter.DrawGhostBar(m_dc);
		}
		void EndDrag(bool bCanceled)
		{
			m_splitter.CleanGhostBar(m_dc);
			if(!bCanceled)
				SetPosition();
		}
		void Move()
		{
			m_splitter.CleanGhostBar(m_dc);
			m_slider=m_pos+m_ghOffset;
			m_splitter.DrawGhostBar(m_dc);
		}

	protected:
		position		m_ghOffset;
		CSplitterBar	m_splitter;
		CSlider			m_slider;
		CWindowDC		m_dc;
	};
	template<long add=0>
	class CMinMaxInfoAccumulator
	{
		typedef LPMINMAXINFO (*CFunPtr)(MINMAXINFO& mmInfo,LPMINMAXINFO pMinMaxInfo);
	public:
		CMinMaxInfoAccumulator(bool bHorizontal)
		{
			m_pFun=bHorizontal ? &MinMaxInfoH : &MinMaxInfoV;
		}
		LPMINMAXINFO operator() (LPMINMAXINFO pMinMaxInfo,const CFrame& x) const
		{
			MINMAXINFO mmInfo;
			ZeroMemory(&mmInfo,sizeof(MINMAXINFO));
			x.GetMinMaxInfo(&mmInfo);
			return (*m_pFun)(mmInfo,pMinMaxInfo);
		}
	protected:
		static LPMINMAXINFO MinMaxInfoH(MINMAXINFO& mmInfo,LPMINMAXINFO pMinMaxInfo)
		{
			if(pMinMaxInfo->ptMinTrackSize.y<mmInfo.ptMinTrackSize.y)
				pMinMaxInfo->ptMinTrackSize.y=mmInfo.ptMinTrackSize.y;
			pMinMaxInfo->ptMinTrackSize.x+=mmInfo.ptMinTrackSize.x+add;
			return pMinMaxInfo;
		}
		static LPMINMAXINFO MinMaxInfoV(MINMAXINFO& mmInfo,LPMINMAXINFO pMinMaxInfo)
		{
			if(pMinMaxInfo->ptMinTrackSize.x<mmInfo.ptMinTrackSize.x)
				pMinMaxInfo->ptMinTrackSize.x=mmInfo.ptMinTrackSize.x;
			pMinMaxInfo->ptMinTrackSize.y+=mmInfo.ptMinTrackSize.y+add;
			return pMinMaxInfo;
		}
	protected:
		CFunPtr	m_pFun;
	};
protected:
	bool ArrangeH(const CRect& rc)
	{
		HDWP hdwp=reinterpret_cast<HDWP>(TRUE);
		const_iterator begin=m_frames.begin();
		const_iterator end=m_frames.end();
		if(begin!=end)
		{
			hdwp=BeginDeferWindowPos((int)m_frames.size());
			const_iterator next=begin;
			while((hdwp!=NULL)&&(++next!=end))
			{
				long x=(*begin)+splitterSize;
				hdwp=begin->DeferFramePos(hdwp,
											x,
											rc.top,
											(*next),
											rc.bottom);
				begin=next;
			}
			if(hdwp!=NULL)
			{
				long x=(*begin)+splitterSize;
				hdwp=begin->DeferFramePos(hdwp,
											x,
											rc.top,
											rc.right,
											rc.bottom);
				if(hdwp)
					::EndDeferWindowPos(hdwp);
			}
		}
		return hdwp!=NULL;
	}

	bool ArrangeV(const CRect& rc)
	{
		HDWP hdwp=reinterpret_cast<HDWP>(TRUE);
		const_iterator begin=m_frames.begin();
		const_iterator end=m_frames.end();
		if(begin!=end)
		{
			hdwp=BeginDeferWindowPos((int)m_frames.size());
			const_iterator next=begin;
			while((hdwp!=NULL)&&(++next!=end))
			{
				long y=(*begin)+splitterSize;
				hdwp=begin->DeferFramePos(hdwp,
											rc.left,
											y,
											rc.right,
											(*next));
				begin=next;
			}
			if(hdwp!=NULL)
			{
				long y=(*begin)+splitterSize;
				hdwp=begin->DeferFramePos(hdwp,
											rc.left,
											y,
											rc.right,
											rc.bottom);
				if(hdwp)
					::EndDeferWindowPos(hdwp);
			}
		}
		return hdwp!=NULL;
	}
	bool Arrange(const CRect& rcClient)
	{
		bool bRes;
		if(IsHorizontal())
			bRes=ArrangeH(rcClient);
		else
			bRes=ArrangeV(rcClient);
		return bRes;
	}

	CWndFramesPackageBase(bool bHorizontal)
		:m_bHorizontal(bHorizontal),m_frames(0,0)
	{
	}
public:
	bool IsHorizontal() const
	{
		return m_bHorizontal;
	}

	void SetOrientation(bool bHorizontal)
	{
		m_bHorizontal=bHorizontal;
	}

	HCURSOR GetCursor(const CPoint& pt,const CRect& rc) const
	{
		HCURSOR hCursor=NULL;
		position pos=IsHorizontal() ?pt.x :pt.y;
		const_iterator i=m_frames.locate(pos);
		if(i!=m_frames.end())
		{
			CEmbeddedSplitterBar splitter(!IsHorizontal(),(*i),rc);
			hCursor=splitter.GetCursor(pt);
		}
		return hCursor;
	}
/*
	bool StartSliding(HWND hWnd,const CPoint& pt,const CRect& rc,bool bGhostMove)
	{
		std::auto_ptr<CSplitterMoveTrackerBase> pTracker;

		if(bGhostMove)
			pTracker.reset(new CSplitterMoveTrackerGhost(hWnd,*this,pt,rc));
		else
			pTracker.reset(new CSplitterMoveTrackerFull(hWnd,*this,pt,rc));

		bool bRes=false;
		if(const_iterator(*pTracker)!=m_frames.end())
			 bRes=TrackDragAndDrop(*pTracker,hWnd);
		return bRes;
	}
*/
	bool StartSliding(HWND hWnd,const CPoint& pt,const CRect& rc,bool bGhostMove)
	{
		bool bRes=false;
		position pos=IsHorizontal() ?pt.x :pt.y;
		const_iterator i=m_frames.locate(pos);
		if(i!=m_frames.end())
		{
			CEmbeddedSplitterBar splitter(!IsHorizontal(),(*i),rc);
			if(splitter.IsPtIn(pt))
			{
				std::auto_ptr<CSplitterMoveTrackerBase> pTracker;

#if (_MSC_VER >= 1310)
				if(bGhostMove)
					pTracker.reset(new CSplitterMoveTrackerGhost(hWnd,*this,pt,rc));
				else
					pTracker.reset(new CSplitterMoveTrackerFull(hWnd,*this,pt,rc));
#else // (_MSC_VER < 1310)
				if(bGhostMove)
					pTracker=std::auto_ptr<CSplitterMoveTrackerBase>(
										new CSplitterMoveTrackerGhost(hWnd,*this,pt,rc));
				else
					pTracker=std::auto_ptr<CSplitterMoveTrackerBase>(
										new CSplitterMoveTrackerFull(hWnd,*this,pt,rc));
#endif

				if(const_iterator(*pTracker)!=m_frames.end())
					 bRes=TrackDragAndDrop(*pTracker,hWnd);
			}
		}
		return bRes;
	}

	bool UpdateLayout(const CRect& rc)
	{
		CBounds bounds;
		if(IsHorizontal())
		{
				bounds.low=rc.left;
				bounds.hi=rc.right;
		}
		else
		{
				bounds.low=rc.top;
				bounds.hi=rc.bottom;
		}
		bounds.low-=splitterSize;

		CBounds::distance_t limit=m_frames.distance_limit();
		if(bounds.distance()<limit)
						bounds.hi=bounds.low+limit;
		m_frames.set_bounds(bounds);
		return Arrange(rc);
	}

	void GetMinFrameSize(const DFMHDR* pHdr,CSize& sz) const
	{
		MINMAXINFO mmInfo;
		ZeroMemory(&mmInfo,sizeof(MINMAXINFO));
		::SendMessage(pHdr->hWnd,WM_GETMINMAXINFO,NULL,reinterpret_cast<LPARAM>(&mmInfo));
		sz.cx=mmInfo.ptMinTrackSize.x;
		sz.cy=mmInfo.ptMinTrackSize.y;
	}
	LRESULT GetMinFrameDist(const DFMHDR* pHdr) const
	{
		CSize sz;
		GetMinFrameSize(pHdr,sz);
		return (IsHorizontal()) ? sz.cx : sz.cy;
	}

	void GetMinMaxInfo(LPMINMAXINFO pMinMaxInfo) const
	{
		if(m_frames.size()!=0)
		{
			pMinMaxInfo=std::accumulate(m_frames.begin(),m_frames.end(),
											pMinMaxInfo,CMinMaxInfoAccumulator<splitterSize>(IsHorizontal()));
			if(IsHorizontal())
				pMinMaxInfo->ptMinTrackSize.x-=splitterSize;
			else
				pMinMaxInfo->ptMinTrackSize.y-=splitterSize;
		}
	}
	void Draw(CDC& dc,const CRect& rc) const
	{
		if(m_frames.begin()!=m_frames.end())
			std::for_each(++m_frames.begin(),m_frames.end(),CEmbeddedSplitterBarPainter(dc,!IsHorizontal(),rc));
	}

	bool AcceptDock(DFDOCKRECT* pHdr,const CRect& rc)
	{
		bool bRes=false;
		CSize sz;
		GetMinFrameSize(&(pHdr->hdr),sz);
		if(rc.PtInRect(CPoint(pHdr->rect.left,pHdr->rect.top))
			&& (sz.cx<=rc.Width())
				&& (sz.cy<=rc.Height())
					&& ((( (IsHorizontal()) ? sz.cx : sz.cy)+m_frames.distance_limit())<(m_frames.hi()-m_frames.low())))
		{
			if(IsHorizontal())
			{
				pHdr->rect.top=rc.top;
				pHdr->rect.bottom=rc.bottom;
			}
			else
			{
				pHdr->rect.left=rc.left;
				pHdr->rect.right=rc.right;
			}
			bRes=true;
		}
		return bRes;
	}
	bool Dock(CFrame& frame,const DFDOCKRECT* pHdr,const CRect& rc)
	{
		position		  pos;
		CFrames::distance len;

		if(IsHorizontal())
		{
			pos=pHdr->rect.left;
			len=pHdr->rect.right-pHdr->rect.left;
		}
		else
		{
			pos=pHdr->rect.top;
			len=pHdr->rect.bottom-pHdr->rect.top;
		}

		iterator i=m_frames.locate(pos);
		if(i!=m_frames.end())
		{
			iterator inext=i;
			++inext;
			CBounds fbounds((*i),(inext!=m_frames.end()) ? position(*inext) : m_frames.hi());
			if((fbounds.low+(fbounds.hi-fbounds.low)/3)<pos)
				i=inext;
		}
		frame=pos;
		m_frames.insert(i,frame,len);

		return Arrange(rc);
	}
	bool Undock(const DFMHDR* pHdr,const CRect& rc)
	{
		iterator i=std::find_if(m_frames.begin(),m_frames.end(),CFrame::CCmp(pHdr->hWnd));
		assert(i!=m_frames.end());
		m_frames.erase(i);
		return Arrange(rc);
	}
	bool Replace(const DFDOCKREPLACE* pHdr,const CRect& rc)
	{
		iterator i=std::find_if(m_frames.begin(),m_frames.end(),CFrame::CCmp(pHdr->hdr.hWnd));
		assert(i!=m_frames.end());
		m_frames.replace(i,CFrame(0,pHdr->hWnd));
		return Arrange(rc);
	}

protected:
	bool	m_bHorizontal;
	CFrames	m_frames;
};

template< class TTraits = CDockingFrameTraits >
class CWndFramesPackage : public CWndFramesPackageBase<CWndFrame,TTraits >
{
	typedef typename CWndFrame CFrame;
	typedef typename TTraits CTraits;
	typedef typename CTraits::CSplitterBar	CSplitterBar;
	typedef CWndFramesPackage<TTraits> thisClass;
	typedef CWndFramesPackageBase<CFrame,TTraits > baseClass;
public:
	CWndFramesPackage(bool bHorizontal):baseClass(bHorizontal)
	{
	}
	void PrepareForDocking(CWindow wnd,HDOCKBAR bar)
	{
		wnd.ShowWindow(SW_HIDE);
		DWORD style = wnd.GetWindowLong(GWL_STYLE);
		DWORD newStyle = style&(~WS_POPUP)|WS_CHILD;
		wnd.SetWindowLong( GWL_STYLE, newStyle);
		wnd.SetParent(bar);
		wnd.SendMessage(WM_NCACTIVATE,TRUE);
		wnd.SendMessage(WMDF_NDOCKSTATECHANGED,
					MAKEWPARAM(TRUE,IsHorizontal()),
					reinterpret_cast<LPARAM>(bar));
	}
	void PrepareForUndocking(CWindow wnd,HDOCKBAR bar)
	{
		wnd.ShowWindow(SW_HIDE);
		DWORD style = wnd.GetWindowLong(GWL_STYLE);
		DWORD newStyle = style&(~WS_CHILD)|WS_POPUP;
		wnd.SetWindowLong( GWL_STYLE, newStyle);
		wnd.SetParent(NULL);

		wnd.SendMessage(WMDF_NDOCKSTATECHANGED,
			FALSE,
			reinterpret_cast<LPARAM>(bar));
	}

	bool SetDockingPosition(const DFDOCKPOS* pHdr,const CRect& rc)
	{
		assert(::IsWindow(pHdr->hdr.hWnd));
		CWindow wnd(pHdr->hdr.hWnd);
		PrepareForDocking(wnd,pHdr->hdr.hBar);
		position pos=m_frames.low()+position((m_frames.hi()-m_frames.low())*pHdr->fPctPos);
		m_frames.insert(CFrame(pos,pHdr->hdr.hWnd),pHdr->nHeight);
//		wnd.ShowWindow(SW_SHOWNA);
		bool bRes=Arrange(rc);
		wnd.SetWindowPos(NULL,0,0,0,0,SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
		return bRes;
	}

	bool GetDockingPosition(DFDOCKPOS* pHdr,const CRect& /*rc*/) const
	{
		assert(::IsWindow(pHdr->hdr.hWnd));
		const_iterator i=std::find_if(m_frames.begin(),m_frames.end(),CFrame::CCmp(pHdr->hdr.hWnd));
		bool bRes=(i!=m_frames.end());
		if(bRes)
		{
			position pos=*i-m_frames.low();
			pHdr->fPctPos=float(pos)/(m_frames.hi()-m_frames.low());
			pHdr->nHeight=m_frames.get_frame_size(i)-CSplitterBar::GetThickness();
//			pHdr->nWidth=IsHorizontal() ? rc.Height() : rc.Width();
//			pHdr->nWidth-=CSplitterBar::GetThickness();
			if(m_frames.size()==1)
				pHdr->dwDockSide|=CDockingSide::sSingle;
		}
		return bRes;
	}

	bool AcceptDock(DFDOCKRECT* pHdr,const CRect& rc)
	{
		return (!((m_frames.size()==1)
					&&(m_frames.begin()->hwnd()==pHdr->hdr.hWnd)))
						  &&baseClass::AcceptDock(pHdr,rc);
	}
	bool Dock(DFDOCKRECT* pHdr,const CRect& rc)
	{
		assert(::IsWindow(pHdr->hdr.hWnd));
		CWindow wnd(pHdr->hdr.hWnd);
		PrepareForDocking(wnd,pHdr->hdr.hBar);
		CFrame frame(0,pHdr->hdr.hWnd);
		bool bRes=baseClass::Dock(frame,pHdr,rc);
		wnd.SetWindowPos(NULL,0,0,0,0,SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
//		wnd.ShowWindow(SW_SHOWNA);
		return bRes;

	}
	bool Undock(DFMHDR* pHdr,const CRect& rc)
	{
		bool bRes=baseClass::Undock(pHdr,rc);
		assert(bRes);
		PrepareForUndocking(pHdr->hWnd,pHdr->hBar);
		if(m_frames.size()==0)
		{
			DFMHDR dockHdr;
			dockHdr.hWnd = pHdr->hBar;
			dockHdr.hBar = ::GetParent(pHdr->hBar);
			assert(::IsWindow(dockHdr.hBar));
			dockHdr.code=DC_UNDOCK;
			::SendMessage(dockHdr.hBar,WMDF_DOCK,NULL,reinterpret_cast<LPARAM>(&dockHdr));
			::PostMessage(dockHdr.hWnd,WM_CLOSE,NULL,NULL);
		}
		return bRes;
	}

	bool Replace(const DFDOCKREPLACE* pHdr,const CRect& rc)
	{
		PrepareForUndocking(pHdr->hdr.hWnd,pHdr->hdr.hBar);
		PrepareForDocking(pHdr->hWnd,pHdr->hdr.hBar);
		bool bRes=baseClass::Replace(pHdr,rc);
		assert(bRes);
		::SetWindowPos(pHdr->hWnd,NULL,0,0,0,0,SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
//		::ShowWindow(pHdr->hWnd,SW_SHOWNA);
		return bRes;
	}

};

template<class T>
class CPtrFrame
{
	typedef CPtrFrame<T> thisClass;
public:
	typedef typename T::position position;
	typedef typename T::distance distance;

	class CCmp
	{
	public:
		CCmp(HWND hWnd)	:m_hWnd(hWnd)
		{
		}
		bool operator ()(const thisClass& frame) const
		{
			return (frame.hwnd() == m_hWnd);
		}
	protected:
		HWND m_hWnd;
	};

	CPtrFrame(const CPtrFrame& x):m_pos(x.m_pos),m_ptr(x.m_ptr)
	{
	}
	CPtrFrame(position pos, T* ptr)
		:m_pos(pos),m_ptr(ptr)
	{
	}
	HWND hwnd() const
	{
		ATLASSERT(m_ptr.get() != NULL);
		return m_ptr->operator HWND();
	}

	operator position() const
	{
		return (position)m_pos;
	}

	thisClass& operator += (position val)
	{
		m_pos+=val;
		return *this;
	}
	thisClass& operator -= (position val)
	{
		m_pos-=val;
		return *this;
	}

	thisClass& operator = (position pos)
	{
		m_pos=pos;
		return *this;
	}

	thisClass& operator = (double pos)
	{
		m_pos=pos;
		return *this;
	}

	double get_real()
	{
		return m_pos;
	}

	HDWP DeferFramePos(HDWP hdwp,long x1,long y1,long x2,long y2) const
	{
		ATLASSERT(m_ptr.get() != NULL);
		return m_ptr->DeferFramePos(hdwp,x1,y1,x2,y2);
	}
	T* operator ->() const
	{
		return m_ptr.get();
	}
	void GetMinMaxInfo(LPMINMAXINFO pMinMaxInfo) const
	{
		ATLASSERT(m_ptr.get() != NULL);
		m_ptr->GetMinMaxInfo(pMinMaxInfo);
	}
	distance MinDistance() const
	{
		ATLASSERT(m_ptr.get() != NULL);
		return m_ptr->MinDistance();
	}
protected:
	double m_pos;
#ifdef USE_BOOST
	mutable boost::shared_ptr<T> m_ptr;
#else
	mutable std::auto_ptr<T> m_ptr;
#endif
};


struct IFrame
{
	typedef long position;
	typedef long distance;

	virtual  ~IFrame(){};
	virtual  HWND hwnd() const=0;
	operator HWND() const
	{
		return hwnd();
	}
	virtual bool AcceptDock(DFDOCKRECT* pHdr) const=0;
	virtual distance MinDistance() const=0;
	virtual void GetMinMaxInfo(LPMINMAXINFO pMinMaxInfo) const=0;

	virtual HDWP DeferFramePos(HDWP hdwp,long x1,long y1,long x2,long y2) const
	{
		return ::DeferWindowPos(hdwp,hwnd(),
								NULL,
								x1,y1,
								x2-x1,y2-y1,
								SWP_NOZORDER | SWP_NOACTIVATE);
	}
};


class CWindowPtrWrapper : public IFrame
{
public:
	CWindowPtrWrapper(HWND* phWnd):m_phWnd(phWnd)
	{
	}
	virtual HWND hwnd() const
	{
		return (*m_phWnd);
	}
	virtual bool AcceptDock(DFDOCKRECT* /*pHdr*/) const
	{
		return false;
	}
	virtual HDWP DeferFramePos(HDWP hdwp,long x1,long y1,long x2,long y2) const
	{
		if(*m_phWnd!=NULL)
			hdwp=::DeferWindowPos(hdwp,hwnd(),
									NULL,
									x1,y1,
									x2-x1,y2-y1,
									SWP_NOZORDER | SWP_NOACTIVATE);
		return hdwp;
	}
	void GetMinMaxInfo(LPMINMAXINFO pMinMaxInfo) const
	{
		if(*m_phWnd!=NULL)
		 ::SendMessage(hwnd(),WM_GETMINMAXINFO,NULL,reinterpret_cast<LPARAM>(pMinMaxInfo));
	}
	virtual distance MinDistance() const
	{
		MINMAXINFO mmInfo;
		ZeroMemory(&mmInfo,sizeof(MINMAXINFO));
		GetMinMaxInfo(&mmInfo);
		return mmInfo.ptMinTrackSize.x;;
	}
protected:
	HWND* m_phWnd;
};

//TImbeddedPackegeWnd
template<class TPackageFrame,class TTraits = CDockingWindowTraits >
class CSubWndFramesPackage :
		public CRect,
		public CWndFramesPackageBase<CPtrFrame<IFrame>,TTraits >
{
	typedef CPtrFrame<IFrame> CFrame;
	typedef TTraits CTraits;
	typedef typename CTraits::CSplitterBar	CSplitterBar;
	typedef CSubWndFramesPackage<TPackageFrame,TTraits> thisClass;
	typedef CWndFramesPackageBase<CFrame,TTraits >		baseClass;
	typedef typename TPackageFrame	CPackageFrame;
	enum {controlledLen=(15+CSplitterBar::sbThickness)};
	struct  CDockOrientationFlag
	{
		enum{low=0,high=1};
		static void SetHigh(DWORD& flag)
		{
			flag=high;
		}
		static void SetLow(DWORD& flag)
		{
			flag=low;
		}
		static bool IsLow(DWORD flag)
		{
			return (flag&high)==0;
		}
	};
protected:
	class CFrameWrapper : public IFrame
	{
	public:
		CFrameWrapper(thisClass* ptr):m_ptr(ptr)
		{
		}
		virtual ~CFrameWrapper()
		{
		}
		virtual  HWND hwnd() const
		{
			return NULL;
		}
/*
		virtual bool AcceptDock(DFDOCKRECT* pHdr) const
		{
			return m_ptr->AcceptDock(pHdr);
		}
*/
		virtual bool AcceptDock(DFDOCKRECT* /*pHdr*/) const
		{
			assert(false);
			return true;
		}

		virtual HDWP DeferFramePos(HDWP hdwp,long x1,long y1,long x2,long y2) const
		{
			m_ptr->UpdateLayout(CRect(x1,y1,x2,y2));
			return hdwp;
		}
		virtual void GetMinMaxInfo(LPMINMAXINFO pMinMaxInfo) const
		{
			m_ptr->GetMinMaxInfo(pMinMaxInfo);
		}
		virtual distance MinDistance() const
		{
			MINMAXINFO mmInfo;
			ZeroMemory(&mmInfo,sizeof(MINMAXINFO));
			GetMinMaxInfo(&mmInfo);
			return m_ptr->IsHorizontal() ? mmInfo.ptMinTrackSize.y : mmInfo.ptMinTrackSize.x;
		}
	protected:
		thisClass* m_ptr;
	};
public:
	CSubWndFramesPackage(bool bHorizontal)
		:baseClass(bHorizontal),m_pDecl(0)
	{
		SetRectEmpty();
	}
	HCURSOR GetCursor(const CPoint& pt) const
	{
		return baseClass::GetCursor(pt,*this);
	}
	bool StartSliding(HWND hWnd,const CPoint& pt,bool bGhostMove)
	{
		return baseClass::StartSliding(hWnd,pt,*this,bGhostMove);
	}

#ifndef DW_PROPORTIONAL_RESIZE
	bool UpdateLayout(const CRect& rc)
	{
		bool bRes=EqualRect(&rc)!=FALSE;
		if(!bRes)
		{
			CopyRect(rc);
			CBounds bounds;
			if(IsHorizontal())
			{
					bounds.low=rc.left;
					bounds.hi=rc.right;
			}
			else
			{
					bounds.low=rc.top;
					bounds.hi=rc.bottom;
			}
			bounds.low-=splitterSize;

			CBounds::distance_t limit=m_frames.distance_limit();
			if(bounds.distance()<limit)
							bounds.hi=bounds.low+limit;
			if(m_pDecl!=0)
				m_frames.set_bounds(bounds,CFrame::CCmp(m_pDecl->hwnd()));
			else
				m_frames.set_bounds(bounds);
			bRes=Arrange(rc);
		}
		return bRes;
	}
#else
	bool UpdateLayout(const CRect& rc)
	{
		bool bRes=EqualRect(&rc)!=FALSE;
		if(!bRes)
		{
			CopyRect(rc);
			bRes=baseClass::UpdateLayout(*this);
		}
		return bRes;
	}
#endif //DW_PROPORTIONAL_RESIZE
	void Draw(CDC& dc)
	{
		baseClass::Draw(dc,*this);
	}

	bool AcceptDock(DFDOCKRECT* pHdr)
	{
		CPoint pt(pHdr->rect.left,pHdr->rect.top);
		CSize sz(0,0);
		position pos;
		if(IsHorizontal())
		{
			pos=pt.x;
			sz.cx=controlledLen;
		}
		else
		{
			pos=pt.y;
			sz.cy=controlledLen;
		}
		bool bRes=PtInRect(pt)==TRUE;
		if(bRes)
		{
//			position pos=IsHorizontal() ? pHdr->rect.left :pHdr->rect.top;
			const_iterator i=m_frames.locate(pos);
			if( m_frames.get_frame_low(i)+controlledLen>pos)
				bRes=AcceptDock(i,pHdr,*this);
			else
			{
				if( m_frames.get_frame_hi(i)-controlledLen<pos)
					bRes=AcceptDock(++i,pHdr,*this);
				else
				{
					bRes=i->hwnd()!=m_pDecl->hwnd();
					if(bRes)
						bRes=(*i)->AcceptDock(pHdr);
				}
			}
		}
		else
		{
			CRect rc(this);
			rc.InflateRect(sz);
			bRes=rc.PtInRect(pt)==TRUE;
			if(bRes)
			{
				if(pos<m_frames.hi())
					bRes=AcceptDock(m_frames.begin(),pHdr,rc);
				else
					bRes=AcceptDock(m_frames.end(),pHdr,rc);
			}
		}
		return bRes;
	}

	bool AcceptDock(const_iterator i,DFDOCKRECT* pHdr,const CRect& rc)
	{
		assert(std::find_if(m_frames.begin(),m_frames.end(),CFrame::CCmp(m_pDecl->hwnd()))!=m_frames.end());
		const_iterator begin=m_frames.begin();
		if(std::find_if(begin,i,CFrame::CCmp(m_pDecl->hwnd()))==i)
			CDockOrientationFlag::SetLow(pHdr->flag);
		else
			CDockOrientationFlag::SetHigh(pHdr->flag);

		bool bRes=baseClass::AcceptDock(pHdr,rc);
		if(bRes)
		{
			if(IsHorizontal())
			{
				long len=(pHdr->rect.right-pHdr->rect.left);
				if(CDockOrientationFlag::IsLow(pHdr->flag))
				{
					assert(i!=m_frames.end());
					pHdr->rect.left=(*i)+CSplitterBar::GetThickness();
					pHdr->rect.right=pHdr->rect.left+len;
				}
				else
				{
					pHdr->rect.right=(i==m_frames.end()) ? m_frames.hi() : (*i);
					pHdr->rect.left=pHdr->rect.right-len;

				}
			}
			else
			{
				long len=(pHdr->rect.bottom-pHdr->rect.top);
				if(CDockOrientationFlag::IsLow(pHdr->flag))
				{
					assert(i!=m_frames.end());
					pHdr->rect.top=(*i)+CSplitterBar::GetThickness();
					pHdr->rect.bottom=pHdr->rect.top+len;
				}
				else
				{
					pHdr->rect.bottom=(i==m_frames.end()) ? m_frames.hi() : (*i);
					pHdr->rect.top=pHdr->rect.bottom-len;
				}
			}
		}
		return bRes;
	}
	bool Dock(CFrame& frame,const DFDOCKRECT* pHdr,const CRect& rc)
	{
		position		  pos;
		CFrames::distance len;

		if(IsHorizontal())
		{
			pos=pHdr->rect.left;
			len=pHdr->rect.right-pHdr->rect.left;
		}
		else
		{
			pos=pHdr->rect.top;
			len=pHdr->rect.bottom-pHdr->rect.top;
		}
		frame=pos;
		if(!CDockOrientationFlag::IsLow(pHdr->flag))
			pos+=len;
		iterator i;
		if(pos!=m_frames.hi())
			i=m_frames.locate(pos);
		else
			i=m_frames.end();
		m_frames.insert(i,CFrame::CCmp(m_pDecl->hwnd()),frame,len);
		return Arrange(rc);
	}

	bool Dock(DFDOCKRECT* pHdr)
	{
		CPackageFrame* ptr=CPackageFrame::CreateInstance(pHdr->hdr.hBar,!IsHorizontal());
		bool bRes=(ptr!=0);
		if(bRes)
		{
			HWND hDockWnd=pHdr->hdr.hWnd;
			pHdr->hdr.hWnd=ptr->hwnd();
			CFrame frame(0,ptr);
			bRes=Dock(frame,pHdr,*this);
			assert(bRes);
			if(bRes)
			{
				pHdr->hdr.hWnd=hDockWnd;
				pHdr->hdr.hBar=ptr->hwnd();
				CWindow bar(pHdr->hdr.hBar);
				bar.GetWindowRect(&(pHdr->rect));
				bRes=::SendMessage(ptr->hwnd(),WMDF_DOCK,NULL,reinterpret_cast<LPARAM>(pHdr))!=FALSE;
			}
		}
		return bRes;
	}

	bool Undock(const DFMHDR* pHdr)
	{
		iterator i=std::find_if(m_frames.begin(),m_frames.end(),CFrame::CCmp(pHdr->hWnd));
		bool bRes=(i!=m_frames.end());
		if(bRes)
		{
			m_frames.erase(i,CFrame::CCmp(m_pDecl->hwnd()));
			bRes=Arrange(*this);
		}
		return bRes;
	}

	bool GetDockingPosition(DFDOCKPOS* pHdr) const
	{
		const_iterator i=std::find_if(m_frames.begin(),m_frames.end(),CFrame::CCmp(pHdr->hdr.hBar));
		bool bRes=(i!=m_frames.end());
		if(bRes)
		{
			pHdr->nWidth=m_frames.get_frame_size(i)-CSplitterBar::GetThickness();
			CFrames::size_type dWnd=std::distance(m_frames.begin(),i);
			i=std::find_if(m_frames.begin(),m_frames.end(),CFrame::CCmp(m_pDecl->hwnd()));
			assert(i!=m_frames.end());
			CFrames::size_type dBWnd=std::distance(m_frames.begin(),i);
			if(dBWnd>dWnd)
			{
				pHdr->nBar=(unsigned long)dWnd;
				pHdr->dwDockSide|=CDockingSide::sTop;
			}
			else
			{
				pHdr->nBar=(unsigned long)(m_frames.size()-dWnd-1);
//				pHdr->dwDockSide|=0;
			}
			bRes=(::SendMessage(pHdr->hdr.hBar,WMDF_DOCK,NULL,reinterpret_cast<LPARAM>(pHdr))!=FALSE);
		}
		return bRes;
	}

//	bool SetDockingPosition(DFDOCKPOS* pHdr)
//	{
//		bool bRes=true;
//		unsigned long limit=GetMinFrameDist(&(pHdr->hdr));
//		if(pHdr->nWidth<limit)
//						pHdr->nWidth=limit;
//		CDockingSide side(pHdr->dwDockSide);
//		if(side.IsTop())
//		{
//			iterator i=ssec::search_n(m_frames.begin(),m_frames.end(),CFrame::CCmp(m_pDecl->hwnd()),pHdr->nBar);
//			assert(i!=m_frames.end());
//			if(i->hwnd()==m_pDecl->hwnd() || side.IsSingle())
//			{
//				CPackageFrame* ptr=CPackageFrame::CreateInstance(pHdr->hdr.hBar,!IsHorizontal());
//				bRes=(ptr!=0);
//				if(bRes)
//				{
////					i=m_frames.insert(i,CFrame(*i,ptr),pHdr->nWidth);
//					i=m_frames.insert(i,CFrame::CCmp(m_pDecl->hwnd()),CFrame(*i,ptr),pHdr->nWidth+splitterSize);
//				}
//			}
//			else
//				m_frames.set_frame_size(i,pHdr->nWidth+splitterSize);
//			pHdr->hdr.hBar=i->hwnd();
//		}
//		else
//		{
//			reverse_iterator ri=ssec::search_n(m_frames.rbegin(),m_frames.rend(),CFrame::CCmp(m_pDecl->hwnd()),pHdr->nBar);
//			assert(ri!=m_frames.rend());
//			iterator i=ri.base();
//			if(/*ri->hwnd()*/(*ri).hwnd()==m_pDecl->hwnd() || side.IsSingle())
//			{
//				CPackageFrame* ptr=CPackageFrame::CreateInstance(pHdr->hdr.hBar,!IsHorizontal());
//				bRes=(ptr!=0);
//				if(bRes)
//				{
//					position pos=(i==m_frames.end())? m_frames.hi():*i;
//					pos-=pHdr->nWidth+CSplitterBar::GetThickness();
//					if(pos<m_frames.low())
//						pos=m_frames.low();
////					i=m_frames.insert(i,CFrame(pos,ptr),pHdr->nWidth);
//					i=m_frames.insert(i,CFrame::CCmp(m_pDecl->hwnd()),CFrame(pos,ptr),pHdr->nWidth+splitterSize);
//				}
//			}
//			else
//			{
//				assert(i!=m_frames.begin());
//				m_frames.set_frame_size(--i,pHdr->nWidth+splitterSize);
//			}
//			pHdr->hdr.hBar=i->hwnd();
//		}
//		bRes=Arrange(*this);
//		assert(bRes);
//		if(bRes)
//		{
//			bRes=(::SendMessage(pHdr->hdr.hBar,WMDF_DOCK,NULL,reinterpret_cast<LPARAM>(pHdr))!=FALSE);
//			assert(bRes);
//		}
//		return bRes;
//	}

	bool SetDockingPosition(DFDOCKPOS* pHdr)
	{
		bool bRes=true;
		unsigned long limit=(unsigned long)GetMinFrameDist(&(pHdr->hdr));
		if(pHdr->nWidth<limit)
						pHdr->nWidth=limit;
		CDockingSide side(pHdr->dwDockSide);
		if(side.IsTop())
		{
			iterator i=ssec::search_n(m_frames.begin(),m_frames.end(),CFrame::CCmp(m_pDecl->hwnd()),pHdr->nBar);
			assert(i!=m_frames.end());
			if(i->hwnd()==m_pDecl->hwnd() || side.IsSingle())
			{
				CPackageFrame* ptr=CPackageFrame::CreateInstance(pHdr->hdr.hBar,!IsHorizontal());
				bRes=(ptr!=0);
				if(bRes)
				{
//					i=m_frames.insert(i,CFrame(*i,ptr),pHdr->nWidth);
					i=m_frames.insert(i,CFrame::CCmp(m_pDecl->hwnd()),CFrame(*i,ptr),pHdr->nWidth+splitterSize);
					bRes=Arrange(*this);
					assert(bRes);
				}
			}
			pHdr->hdr.hBar=i->hwnd();
		}
		else
		{
			reverse_iterator ri=ssec::search_n(m_frames.rbegin(),m_frames.rend(),CFrame::CCmp(m_pDecl->hwnd()),pHdr->nBar);
			assert(ri!=m_frames.rend());
			iterator i=ri.base();
			if(/*ri->hwnd()*/(*ri).hwnd()==m_pDecl->hwnd() || side.IsSingle())
			{
				CPackageFrame* ptr=CPackageFrame::CreateInstance(pHdr->hdr.hBar,!IsHorizontal());
				bRes=(ptr!=0);
				if(bRes)
				{
					position pos=(i==m_frames.end())? m_frames.hi():*i;
					pos-=pHdr->nWidth+CSplitterBar::GetThickness();
					if(pos<m_frames.low())
						pos=m_frames.low();
//					i=m_frames.insert(i,CFrame(pos,ptr),pHdr->nWidth);
					i=m_frames.insert(i,CFrame::CCmp(m_pDecl->hwnd()),CFrame(pos,ptr),pHdr->nWidth+splitterSize);
					bRes=Arrange(*this);
					assert(bRes);
					pHdr->hdr.hBar=i->hwnd();
				}
			}
			else
				pHdr->hdr.hBar=/*ri->hwnd()*/(*ri).hwnd();
		}
		if(bRes)
		{
			bRes=(::SendMessage(pHdr->hdr.hBar,WMDF_DOCK,NULL,reinterpret_cast<LPARAM>(pHdr))!=FALSE);
			assert(bRes);
		}
		return bRes;
	}

	bool Insert(HWND* ptr, const CRect& rc)
	{
		DFDOCKRECT dockHdr;
		::CopyRect(&dockHdr.rect,&rc);
		assert(m_pDecl==0);
		m_pDecl=new CWindowPtrWrapper(ptr);
		CFrame frame(0,m_pDecl);
		return baseClass::Dock(frame,&dockHdr,*this);
	}
	bool Insert(thisClass* ptr, const CRect& rc)
	{
		DFDOCKRECT dockHdr;
		::CopyRect(&dockHdr.rect,&rc);
		assert(m_pDecl==0);
		m_pDecl=new CFrameWrapper(ptr);
		CFrame frame(0,m_pDecl);
		return baseClass::Dock(frame,&dockHdr,*this);
	}
protected:
	IFrame* m_pDecl;
};

}//namespace dockwins

#endif // __WTL_DW__WNDFRMPKG_H__
