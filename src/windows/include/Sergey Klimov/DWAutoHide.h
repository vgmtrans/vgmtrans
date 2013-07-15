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

#ifndef __WTL_DW__DWAUTOHIDE_H__
#define __WTL_DW__DWAUTOHIDE_H__

#pragma once

#define DF_AUTO_HIDE_FEATURES

#include <queue>
#include <deque>
#include "ssec.h"
#include "DockMisc.h"
#include "ExtDockingWindow.h"


namespace dockwins{

typedef CDockingWindowTraits<COutlookLikeCaption,
								WS_CAPTION | WS_CHILD |
								WS_CLIPCHILDREN | WS_CLIPSIBLINGS,WS_EX_TOOLWINDOW>
								COutlookLikeAutoHidePaneTraits;


template <class TAutoHidePaneTraits,class TSplitterBar,/* DWORD TDockFrameStyle=0,*/
			DWORD t_dwStyle = 0, DWORD t_dwExStyle = 0>
struct CDockingFrameTraitsT : CWinTraits <t_dwStyle,t_dwExStyle>
{
	typedef TSplitterBar		CSplitterBar;
	typedef TAutoHidePaneTraits CAutoHidePaneTraits;
};

typedef CDockingFrameTraitsT< COutlookLikeAutoHidePaneTraits,CSimpleSplitterBar<5>,
		WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
		WS_EX_APPWINDOW | WS_EX_WINDOWEDGE> CDockingFrameTraits;

typedef CDockingFrameTraitsT<COutlookLikeAutoHidePaneTraits, CSimpleSplitterBarEx<6>,
		WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,0> CDockingSiteTraits;

struct IPinnedLabel
{
	typedef CDockingSide	CSide;
	enum
	{
		leftBorder=3,
		rightBorder=3,
		labelEdge=2,
		labelPadding=5,			// padding between the labels
		captionPadding=3		// padding between border of the label and the lable caption
	};

	class CPinnedWindow
	{
	public:
		CPinnedWindow()
			:m_hWnd(NULL),m_icon(0),m_txt(0),m_width(0)
		{
		}
		CPinnedWindow(const CPinnedWindow& ref)
		{
			this->operator=(ref);
		}
		~CPinnedWindow()
		{
			delete [] m_txt;
		}
		CPinnedWindow& operator = (const CPinnedWindow& ref)
		{
			m_hWnd=ref.m_hWnd;
			m_icon=ref.m_icon;
			m_width=ref.m_width;
			m_txt=ref.m_txt;
			ref.m_txt=0;
			return *this;
		}
		int Assign(HWND hWnd,unsigned long width)
		{
			assert(::IsWindow(hWnd));
			m_hWnd=hWnd;
			m_width=width;
			m_icon=reinterpret_cast<HICON>(::SendMessage(hWnd, WM_GETICON, FALSE, 0));
// need conditional code because types don't match in winuser.h
#ifdef _WIN64
			if(m_icon == NULL)
				m_icon = (HICON)::GetClassLongPtr(hWnd, GCLP_HICONSM);
#else
			if(m_icon == NULL)
				m_icon = (HICON)LongToHandle(::GetClassLongPtr(hWnd, GCLP_HICONSM));
#endif
			delete [] m_txt;
			int len=0;
			try
			{
				len=::GetWindowTextLength(hWnd)+1;
				m_txt=new TCHAR[len];
				::GetWindowText(hWnd,m_txt,len);
			} catch(std::bad_alloc& /*e*/)
			{
				m_txt=0;
			}
			return len;

		}
		HWND Wnd() const
		{
			return m_hWnd;
		}
		HICON Icon() const
		{
			return m_icon;
		}
		LPCTSTR Text() const
		{
			return m_txt;
		}
		unsigned long Width() const
		{
			return m_width;
		}
		void Width(unsigned long width)
		{
			m_width=width;
		}
		void PrepareForDock(HDOCKBAR hBar,bool bHorizontal)
		{
			::ShowWindow(m_hWnd,SW_HIDE);
			DWORD style = ::GetWindowLong(m_hWnd,GWL_STYLE);
			DWORD newStyle = style&(~(WS_POPUP | WS_CAPTION))|WS_CHILD;
			::SetWindowLong(m_hWnd, GWL_STYLE, newStyle);
			::SetParent(m_hWnd,hBar);
			::SendMessage(m_hWnd,WM_NCACTIVATE,TRUE,NULL);
			::SendMessage(m_hWnd,WMDF_NDOCKSTATECHANGED,
									MAKEWPARAM(TRUE,bHorizontal),
									reinterpret_cast<LPARAM>(hBar));
		}
		void PrepareForUndock(HDOCKBAR	hBar)
		{
			::ShowWindow(m_hWnd,SW_HIDE);
			DWORD style = ::GetWindowLong(m_hWnd,GWL_STYLE);
			DWORD newStyle = style&(~WS_CHILD) | WS_POPUP | WS_CAPTION;
			::SetWindowLong(m_hWnd, GWL_STYLE, newStyle);
			::SetParent(m_hWnd,NULL);
			::SendMessage(m_hWnd,WMDF_NDOCKSTATECHANGED,
					FALSE,
					reinterpret_cast<LPARAM>(hBar));
		}
		void DrawLabel(CDC& dc,const CRect& rc,const CSide& side) const
		{
			CRect rcOutput(rc);
			rcOutput.DeflateRect(captionPadding,captionPadding);
			if(m_icon!=NULL)
			{
				CDWSettings settings;
				CSize  szIcon(settings.CXMinIcon(),settings.CYMinIcon());
				if(side.IsHorizontal())
				{
					if(rc.Width()>szIcon.cx+2*captionPadding)
					{
						POINT pt={rcOutput.left,rc.top+(rc.Height()-szIcon.cx)/2};
						rcOutput.left+=szIcon.cx+captionPadding;
						dc.DrawIconEx(pt,m_icon,szIcon);
					}
				}
				else
				{
					if(rc.Height()>szIcon.cy+2*captionPadding)
					{
						POINT pt={rc.left+(rc.Width()-szIcon.cy)/2,rcOutput.top};
						rcOutput.top+=szIcon.cy+captionPadding;
						dc.DrawIconEx(pt,m_icon,szIcon);
					}
				}
			}
			DrawEllipsisText(dc,m_txt,(int)_tcslen(m_txt),&rcOutput,side.IsHorizontal());
		}
	protected:
		unsigned long	m_width;
		HWND			m_hWnd;
		HICON			m_icon;
		mutable LPTSTR	m_txt;
	};

	class CCmp
	{
	public:
		CCmp(HWND hWnd):m_hWnd(hWnd)
		{
		}
		bool operator() (const IPinnedLabel* ptr) const
		{
			return ptr->IsOwner(m_hWnd);
		}
	protected:
		HWND m_hWnd;
	};

	virtual ~IPinnedLabel(){}

	virtual IPinnedLabel* Remove(HWND hWnd,HDOCKBAR hBar)=0;
	virtual bool UnPin(HWND hWnd,HDOCKBAR hBar,DFDOCKPOS* pHdr)=0;

	virtual long Width() const=0;
	virtual void Width(long width)=0;
	virtual long DesiredWidth(CDC& dc) const=0;

	virtual bool GetDockingPosition(DFDOCKPOS* pHdr) const=0;

	virtual CPinnedWindow* ActivePinnedWindow()=0;
	virtual CPinnedWindow* FromPoint(long x,bool bActivate)=0;
	virtual bool IsOwner(HWND hWnd) const=0;
	virtual void Draw(CDC& dc,const CRect& rc,const CSide& side) const=0;

};

class CSinglePinnedLabel : public IPinnedLabel
{
public:
	CSinglePinnedLabel(DFPINUP* pHdr,bool bHorizontal)
		:m_width(0)
	{
		assert( (pHdr->n==0) || (pHdr->n==1) );
		m_wnd.Assign(pHdr->hdr.hWnd,pHdr->nWidth);
		m_wnd.PrepareForDock(pHdr->hdr.hBar,bHorizontal);
	}
	CSinglePinnedLabel(const CPinnedWindow& wnd)
		:m_wnd(wnd),m_width(0)
	{
	}

	virtual IPinnedLabel* Remove(HWND hWnd,HDOCKBAR hBar)
	{
		hWnd; // avoid warning on level 4
		assert(IsOwner(hWnd));
		m_wnd.PrepareForUndock(hBar);
		return 0;
	}
	virtual bool UnPin(HWND hWnd,HDOCKBAR hBar,DFDOCKPOS* pHdr)
	{
		GetDockingPosition(pHdr);
		bool bRes=(Remove(hWnd,hBar)==0);
		if(bRes)
		{
			pHdr->hdr.code=DC_SETDOCKPOSITION;
			::SendMessage(pHdr->hdr.hBar,WMDF_DOCK,NULL,reinterpret_cast<LPARAM>(pHdr));
		}
		return bRes;
	}
	virtual long Width() const
	{
		return m_width;
	}

	virtual void Width(long width)
	{
		m_width=width;
	}

	virtual long DesiredWidth(CDC& dc) const
	{
		SIZE sz;
		LPCTSTR text=m_wnd.Text();
		bool bRes=(GetTextExtentPoint32(dc, text,(int)_tcslen(text),&sz)!=FALSE);
		assert(bRes);
		unsigned long width=sz.cx+2*captionPadding;
		if(m_wnd.Icon()!=NULL)
		{
			CDWSettings settings;
			width+=settings.CXMinIcon()+captionPadding;
		}
		bRes;
		return width;
	}

	virtual CPinnedWindow* ActivePinnedWindow()
	{
		return &m_wnd;
	}
	virtual CPinnedWindow* FromPoint(long x,bool /*bActivate*/)
	{
		x; // avoid warning on level 4
		assert(x>=0 && x<Width() );
		return ActivePinnedWindow();
	}

	virtual void Draw(CDC& dc,const CRect& rc,const CSide& side) const
	{
		dc.Rectangle(&rc);
		m_wnd.DrawLabel(dc,rc,side);
	}

	virtual bool IsOwner(HWND hWnd) const
	{
		return m_wnd.Wnd()==hWnd;
	}
	virtual bool GetDockingPosition(DFDOCKPOS* pHdr) const
	{
		assert(pHdr->hdr.hWnd==m_wnd.Wnd());
		pHdr->nBar=0;
		pHdr->nWidth=m_wnd.Width();
		pHdr->nHeight=1;
		pHdr->nIndex=0;
		pHdr->fPctPos=0;
		return true;
	}
protected:
	CPinnedWindow	m_wnd;
	long			m_width;
};

class CMultyPinnedLabel : public IPinnedLabel
{
	enum {npos=ULONG_MAX/*std::numeric_limits<unsigned long>::max()*/};
public:
	CMultyPinnedLabel(DFPINUP* pHdr,bool bHorizontal)
		:m_width(0)
	{
		assert(pHdr->n>1);
		m_n=pHdr->n;
		m_tabs=new CPinnedWindow[m_n];
		int maxLen=0;
		for(unsigned long i=0;i<m_n;i++)
		{
			int len=m_tabs[i].Assign(pHdr->phWnds[i],pHdr->nWidth);
			m_tabs[i].PrepareForDock(pHdr->hdr.hBar,bHorizontal);
			if(len>maxLen)
			{
				maxLen=len;
				m_longestTextTab=i;
			}
			if(pHdr->phWnds[i]==pHdr->hdr.hWnd)
				m_activeTab=i;
		}
	}
	~CMultyPinnedLabel()
	{
		delete [] m_tabs;
	}

	virtual bool UnPin(HWND hWnd,HDOCKBAR hBar,DFDOCKPOS* pHdr)
	{
		hWnd; // avoid warning on level 4
		assert(pHdr->hdr.hWnd==hWnd);
		GetDockingPosition(pHdr);
		pHdr->hdr.hWnd=m_tabs[0].Wnd();
		pHdr->hdr.code=DC_SETDOCKPOSITION;
		m_tabs[0].PrepareForUndock(hBar);
		::SendMessage(pHdr->hdr.hBar,WMDF_DOCK,NULL,reinterpret_cast<LPARAM>(pHdr));

		pHdr->hdr.hBar=pHdr->hdr.hWnd;
		for(unsigned long i=1;i<m_n;i++)
		{
			pHdr->nIndex=i;
			pHdr->hdr.hWnd=m_tabs[i].Wnd();
			m_tabs[i].PrepareForUndock(hBar);
			::SendMessage(pHdr->hdr.hBar,WMDF_DOCK,NULL,reinterpret_cast<LPARAM>(pHdr));
		}
		pHdr->hdr.code=DC_ACTIVATE;
		pHdr->hdr.hWnd=m_tabs[m_activeTab].Wnd();
		::SendMessage(pHdr->hdr.hBar,WMDF_DOCK,NULL,reinterpret_cast<LPARAM>(&(pHdr->hdr)));
		return true;
	}
	virtual IPinnedLabel* Remove(HWND hWnd,HDOCKBAR hBar)
	{
		assert(IsOwner(hWnd));
		IPinnedLabel* ptr=this;
		try
		{
			if(m_n==2)
			{
				unsigned long i=(m_tabs[0].Wnd()!=hWnd);
				ptr=new CSinglePinnedLabel(m_tabs[i]);
				m_tabs[!i].PrepareForUndock(hBar);
			}
			else
			{
				CPinnedWindow* ptr=m_tabs;
				m_tabs=new CPinnedWindow[m_n-1];
				unsigned long j=0;
				unsigned int maxLen=0;
				for(unsigned long i=0;i<m_n;i++)
				{
					if(ptr[i].Wnd()!=hWnd)
					{
						if(maxLen<_tcslen(ptr[i].Text()))
							m_longestTextTab=j;
						m_tabs[j++]=ptr[i];
					}
					else
						ptr[i].PrepareForUndock(hBar);
				}
				if(m_activeTab==--m_n)
							--m_activeTab;
				delete [] ptr;
			}
		}
		catch(std::bad_alloc& /*e*/)
		{
		}
		return ptr;
	}
	unsigned long Locate(HWND hWnd) const
	{
		for(unsigned long i=0;i<m_n;i++)
			if(m_tabs[i].Wnd()==hWnd)
				return i;
			return (unsigned long)npos;
	}
	virtual bool IsOwner(HWND hWnd) const
	{
		return (Locate(hWnd)!=npos);
	}

	virtual long Width() const
	{
		return m_width;
	}
	virtual void Width(long width)
	{
		m_width=width;
		if(m_width<m_passiveTabWidth*long(m_n))
		{
			if(m_width<long(m_n))
				m_passiveTabWidth=0;
			else
				m_passiveTabWidth=m_width/m_n;
		}
	}
	virtual long DesiredWidth(CDC& dc) const
	{
		SIZE sz;
		LPCTSTR text=m_tabs[m_longestTextTab].Text();
		bool bRes=(GetTextExtentPoint32(dc, text,(int)_tcslen(text),&sz)!=FALSE);
		assert(bRes);
		bRes;
		long width=sz.cx+2*captionPadding;
		CDWSettings settings;
		width+=settings.CXMinIcon()+captionPadding;
		m_passiveTabWidth=settings.CXMinIcon()+2*captionPadding;
		width+=m_passiveTabWidth*(m_n-1);
		return width;
	}

	virtual CPinnedWindow* ActivePinnedWindow()
	{
		return m_tabs+m_activeTab;
	}
	virtual CPinnedWindow* FromPoint(long x,bool bActivate)
	{
		assert(x>=0 && x<Width() );
		unsigned long i=m_activeTab;
		if(x<long(m_activeTab)*m_passiveTabWidth)
			i=x/m_passiveTabWidth;
		else
		{
			long width=Width()-(m_n-m_activeTab-1)*m_passiveTabWidth;
			if( width<x )
				i+=(x-width)/m_passiveTabWidth+1;
		}
		assert(m_activeTab<m_n);
		if(bActivate)
			m_activeTab=i;
		return m_tabs+i;
	}

	void DrawPassiveTab(unsigned long i,CDC& dc,const CRect& rc,const CSide& side) const
	{
		CRect rcOutput(rc);
		rcOutput.DeflateRect(captionPadding,captionPadding);
		HICON icon=m_tabs[i].Icon();
		CDWSettings settings;
		CSize  sz(settings.CXMinIcon(),settings.CYMinIcon());
		if( icon!=0
			&& (sz.cx<=(rc.Width()-2*captionPadding))
			&& (sz.cy<=(rc.Height()-2*captionPadding)) )
		{
			POINT pt={rcOutput.left,rcOutput.top};
			dc.DrawIconEx(pt,icon,sz);
		}
		else
		{
			LPCTSTR text=m_tabs[i].Text();
			DrawEllipsisText(dc,text,(int)_tcslen(text),&rcOutput,side.IsHorizontal());
		}
	}
	void DrawActiveTab(unsigned long i,CDC& dc,const CRect& rc,const CSide& side) const
	{
		m_tabs[i].DrawLabel(dc,rc,side.IsHorizontal());
	}
	virtual void Draw(CDC& dc,const CRect& rc,const CSide& side) const
	{
		CRect rcOutput(rc);
		dc.Rectangle(&rcOutput);
		long* pLeft;
		long* pRight;
		long* px;
		long* py;
		if(side.IsHorizontal())
		{
			pLeft=&rcOutput.left;
			pRight=&rcOutput.right;

			px=&rcOutput.left;
			py=&rcOutput.bottom;
		}
		else
		{
			pLeft=&rcOutput.top;
			pRight=&rcOutput.bottom;

			px=&rcOutput.right;
			py=&rcOutput.top;
		}
		for(unsigned long i=0;i<m_n;i++)
		{
			if(i==m_activeTab)
			{
				*pRight=*pLeft+m_width-m_passiveTabWidth*(m_n-1);
				assert(*pRight<=(side.IsHorizontal() ? rcOutput.right : rcOutput.bottom));
				DrawActiveTab(i,dc,rcOutput,side.IsHorizontal());
			}
			else
			{
				*pRight=*pLeft+m_passiveTabWidth;
				assert(*pRight<=(side.IsHorizontal() ? rcOutput.right : rcOutput.bottom));
				DrawPassiveTab(i,dc,rcOutput,side);
			}
			dc.MoveTo(rcOutput.left, rcOutput.top);
			dc.LineTo(*px,*py);
			*pLeft=*pRight;
		}
	}

	virtual bool GetDockingPosition(DFDOCKPOS* pHdr) const
	{
		unsigned long i=Locate(pHdr->hdr.hWnd);
		bool bRes=(i!=npos);
		if(bRes)
		{
			if(m_activeTab==i)
				pHdr->dwDockSide|=CDockingSide::sActive;
			pHdr->nBar=0;
			pHdr->nWidth=m_tabs[i].Width();
			pHdr->nHeight=1;
			pHdr->nIndex=i;
			pHdr->fPctPos=0;
		}
		return bRes;
	}
protected:
	unsigned long	m_n;
	CPinnedWindow*	m_tabs;
	long			m_width;

	mutable  long   m_passiveTabWidth;
	unsigned long	m_activeTab;
	unsigned long	m_longestTextTab;
};

class CAutoHideBar: protected CRect
{
	typedef CRect	baseClass;
protected:
	typedef IPinnedLabel*					CPinnedLabelPtr;
	typedef std::deque<CPinnedLabelPtr>		CBunch;
	typedef CBunch::const_iterator			const_iterator;
public:
	typedef IPinnedLabel::CSide	CSide;

	CAutoHideBar()
	{
		SetRectEmpty();
	}
	static void DestroyPinnedLabel(void *ptr)
	{
		delete static_cast< CPinnedLabelPtr >(ptr);
	}
	~CAutoHideBar()
	{
		void (*pDelete)(void *)=&DestroyPinnedLabel;
		std::for_each(m_bunch.begin(),m_bunch.end(),pDelete);
	}
	operator const CRect& () const
	{
		return *this;
	}
	bool IsPtIn(const CPoint& pt) const
	{
		return (PtInRect(pt)!=FALSE);
	}
	const CSide& Orientation() const
	{
		return m_side;
	}
	bool IsVisible() const
	{
		return !m_bunch.empty();
	}
	bool IsHorizontal() const
	{
		return Orientation().IsHorizontal();
	}
	bool IsTop() const
	{
		return  Orientation().IsTop();
	}
	void Initialize(CSide side)
	{
		m_side=side;
	}
	bool CalculateRect(CDC& dc,CRect& rc,long width,long leftPadding,long rightPadding)
	{
		if(IsVisible())
		{
			CopyRect(rc);
			if(IsHorizontal())
			{
				if(IsTop())
					rc.top=bottom=top+width;
				else
					rc.bottom=top=bottom-width;
				left+=leftPadding;
				right-=rightPadding;
			}
			else
			{
				if(IsTop())
					rc.left=right=left+width;
				else
					rc.right=left=right-width;
				top+=leftPadding;
				bottom-=rightPadding;
			}
			UpdateLayout(dc);
		}
		return true;
	}
	void UpdateLayout(CDC& dc) const
	{
		bool bHorizontal=IsHorizontal();
		HFONT hPrevFont;
		long availableWidth;
//		long availableWidth=bHorizontal ? Width() : Height();
		CDWSettings settings;
		if(bHorizontal)
		{
			availableWidth=Width();
			hPrevFont=dc.SelectFont(settings.HSysFont());
		}
		else
		{
			availableWidth=Height();
			hPrevFont=dc.SelectFont(settings.VSysFont());
		}
		availableWidth+=IPinnedLabel::labelPadding-IPinnedLabel::leftBorder-IPinnedLabel::rightBorder;

		typedef std::priority_queue<long,std::deque<long>,std::greater<long> > CQWidth;
		CQWidth widths;
		long width = 0;
		for(const_iterator i=m_bunch.begin();i!=m_bunch.end();++i)
		{
			int labelWidth=(*i)->DesiredWidth(dc);
			(*i)->Width(labelWidth);
			labelWidth+=IPinnedLabel::labelPadding;
			widths.push(labelWidth);
			width+=labelWidth;
		}
		long averageLableWidth=width;
		long n=(long)m_bunch.size();
		if(n>0 && (width>availableWidth) )
		{
			width=availableWidth;
			long itemsLeft=n;
			averageLableWidth=width/itemsLeft;
			long diffrence=width%itemsLeft;
			while(!widths.empty())
			{
				long itemWidth=widths.top();
				long diff=averageLableWidth-itemWidth;
				if(diff>0)
				{
					diffrence+=diff;
					--itemsLeft;
					widths.pop();
				}
				else
				{
					if(diffrence<itemsLeft)
						break;
					averageLableWidth+=diffrence/itemsLeft;
					diffrence=diffrence%itemsLeft;
				}
			}

			averageLableWidth-=IPinnedLabel::labelPadding;
			if(averageLableWidth<IPinnedLabel::labelPadding)
				averageLableWidth=0;
			for(const_iterator i=m_bunch.begin();i!=m_bunch.end();++i)
			{
				long labelWidth=(*i)->Width();
				if( labelWidth>averageLableWidth )
							labelWidth=averageLableWidth;
				(*i)->Width(labelWidth);
			}
		}
		dc.SelectFont(hPrevFont);
	}

    void Draw(CDC& dc,bool bEraseBackground=true)
    {
		if(IsVisible())
		{
			if(bEraseBackground)
			{
				CDWSettings settings;
				CBrush bgrBrush;
				bgrBrush.CreateSolidBrush(settings.CoolCtrlBackgroundColor());
				HBRUSH hOldBrush=dc.SelectBrush(bgrBrush);
				dc.PatBlt(left, top, Width(), Height(), PATCOPY);
				dc.SelectBrush(hOldBrush);
			}

			CDWSettings settings;
			CPen pen;
			pen.CreatePen(PS_SOLID,1,::GetSysColor(COLOR_BTNSHADOW));
			HPEN hOldPen=dc.SelectPen(pen);
			CPen penEraser;
			penEraser.CreatePen(PS_SOLID,1,::GetSysColor(COLOR_BTNFACE));
			COLORREF oldColor=dc.SetTextColor(settings.AutoHideBarTextColor());
			CBrush brush;
			brush.CreateSolidBrush(::GetSysColor(COLOR_BTNFACE));
			HBRUSH hOldBrush=dc.SelectBrush(brush);
			int oldBkMode=dc.SetBkMode(TRANSPARENT);

			HFONT hOldFont;
			long* pLeft;
			long* pRight;
			CRect rcLabel(this);
			long *xELine,*yELine;
			long tmp;
			if(IsHorizontal())
			{
				xELine=&rcLabel.right;
				if(IsTop())
				{
					rcLabel.bottom-=IPinnedLabel::labelEdge;
					yELine=&rcLabel.top;
				}
				else
				{
					rcLabel.top+=IPinnedLabel::labelEdge;
					tmp=rcLabel.bottom-1;
					yELine=&tmp;
				}
				hOldFont=dc.SelectFont(settings.HSysFont());
				pLeft=&rcLabel.left;
				pRight=&rcLabel.right;
			}
			else
			{
				yELine=&rcLabel.bottom;
				if(IsTop())
				{
					rcLabel.right-=IPinnedLabel::labelEdge;
					xELine=&rcLabel.left;
				}
				else
				{
					rcLabel.left+=IPinnedLabel::labelEdge;
					tmp=rcLabel.right-1;
					xELine=&tmp;
				}

				hOldFont=dc.SelectFont(settings.VSysFont());
				pLeft=&rcLabel.top;
				pRight=&rcLabel.bottom;
			}
			*pLeft+=IPinnedLabel::leftBorder;
			*pRight-=IPinnedLabel::rightBorder;

			long minSize=(long)m_bunch.size()*IPinnedLabel::labelPadding-IPinnedLabel::labelPadding;

			if(minSize<(*pRight-*pLeft))
			{
				*pRight=*pLeft+1;
				CPoint ptELine;
				for(const_iterator i=m_bunch.begin();i!=m_bunch.end();++i)
				{
					ptELine.x=*xELine;
					ptELine.y=*yELine;
					*pRight=*pLeft+(*i)->Width();
					assert( m_side.IsHorizontal() ? *pRight<=right : *pRight<=bottom);
					(*i)->Draw(dc,rcLabel,m_side);

					*pLeft=*pRight+IPinnedLabel::labelPadding;
					--*pRight;
					HPEN hPrevPen=dc.SelectPen(penEraser);
					dc.MoveTo(ptELine);
					dc.LineTo(*xELine,*yELine);
					dc.SelectPen(hPrevPen);

					*pRight=*pLeft+1;
					assert( m_side.IsHorizontal()
								? (*pLeft>=left && (*pLeft<=right+IPinnedLabel::labelPadding) )
								: (*pLeft>=top && (*pLeft<=bottom+IPinnedLabel::labelPadding) ) );
				}
			}
			dc.SelectFont(hOldFont);
			dc.SelectPen(hOldPen);
			dc.SetTextColor(oldColor);
			dc.SelectBrush(hOldBrush);
			dc.SetBkMode(oldBkMode);
		}
	}

	IPinnedLabel::CPinnedWindow* MouseEnter(const CPoint& pt,bool bActivate=false) const
	{
		IPinnedLabel::CPinnedWindow* ptr=0;
		if(IsVisible() && IsPtIn(pt))
		{
			int x;
			int vRight;
			if(IsHorizontal())
			{
				x=pt.x;
				vRight=left;
			}
			else
			{
				x=pt.y;
				vRight=top;
			}
			for(const_iterator i=m_bunch.begin();i!=m_bunch.end();++i)
			{
				unsigned long vLeft=vRight;
				vRight+=(*i)->Width();
				if(vRight>x)
				{
					ptr=(*i)->FromPoint(x-vLeft,bActivate);
					break;
				}
				vRight+=IPinnedLabel::labelPadding;
				if(vRight>x)
						break;
			}
		}
		return ptr;
	}
	CPinnedLabelPtr Insert(DFPINUP* pHdr)
	{
		assert(m_side.Side()==CSide(pHdr->dwDockSide).Side());
		CPinnedLabelPtr ptr=0;
		try{
			if(pHdr->n>1)
				ptr=new CMultyPinnedLabel(pHdr,IsHorizontal());
			else
				ptr=new CSinglePinnedLabel(pHdr,IsHorizontal());
			m_bunch.push_back(ptr);
		}
		catch(std::bad_alloc& /*e*/)
		{
		}
		return ptr;
	}
	bool Remove(HWND hWnd,HDOCKBAR hBar,DFDOCKPOS* pHdr)
	{
		CBunch::iterator i=std::find_if(m_bunch.begin(),m_bunch.end(),
											IPinnedLabel::CCmp(hWnd));
		bool bRes=(i!=m_bunch.end());
		if(bRes)
		{
			CPinnedLabelPtr ptr=*i;
			if(pHdr==0)
				(*i)=ptr->Remove(hWnd,hBar);
			else
			{
				pHdr->dwDockSide=m_side.Side() | CDockingSide::sSingle | CSide::sPinned;
				ptr->UnPin(hWnd,hBar,pHdr);
				(*i)=0;
			}
			if((*i)!=ptr)
				delete ptr;
			if((*i)==0)
				m_bunch.erase(i);
			if(!IsVisible())
				SetRectEmpty();
		}
		return bRes;
	}
	bool GetDockingPosition(DFDOCKPOS* pHdr) const
	{
		CBunch::const_iterator i=std::find_if(m_bunch.begin(),m_bunch.end(),
											IPinnedLabel::CCmp(pHdr->hdr.hWnd));
		bool bRes=(i!=m_bunch.end());
		if(bRes)
		{
			pHdr->dwDockSide=m_side.Side() | CDockingSide::sSingle | CSide::sPinned;
			(*i)->GetDockingPosition(pHdr);
			pHdr->nBar=(unsigned long)std::distance(m_bunch.begin(),i);
		}
		return bRes;
	}
protected:
	CSide			m_side;
	CBunch			m_bunch;
};

#define HTSPLITTERH HTLEFT
#define HTSPLITTERV HTTOP

template <class T,
          class TBase = CWindow,
          class TAutoHidePaneTraits = COutlookLikeAutoHidePaneTraits>
class ATL_NO_VTABLE CAutoHidePaneImpl :
	 public CWindowImpl< T, TBase, TAutoHidePaneTraits >
{
    typedef CWindowImpl< T, TBase,  TAutoHidePaneTraits >		baseClass;
    typedef CAutoHidePaneImpl< T, TBase, TAutoHidePaneTraits >	thisClass;
protected:
	typedef typename TAutoHidePaneTraits::CCaption	CCaption;
	typedef typename CAutoHideBar::CSide			CSide;
	struct  CSplitterBar : CSimpleSplitterBarEx<>
	{
		CSplitterBar(bool bHorizontal=true):CSimpleSplitterBarEx<>(bHorizontal)
		{
		}
		void CalculateRect(CRect& rc,DWORD side)
		{
			CopyRect(rc);
			switch(side)
			{
				case CSide::sTop:
					rc.bottom=top=bottom-GetThickness();
					break;
				case CSide::sBottom:
					rc.top=bottom=top+GetThickness();
					break;
				case CSide::sRight:
					rc.left=right=left+GetThickness();
					break;
				case CSide::sLeft:
					rc.right=left=right-GetThickness();
					break;
			};
		}
	};
public:
	CAutoHidePaneImpl()
	{
		m_caption.SetPinButtonState(CPinIcons::sUnPinned/*CCaption::PinButtonStates::sPinned*/);
	}
protected:
    CSide Orientation() const
    {
        return m_side;
    }
    void Orientation(const CSide& side)
    {
		m_side=side;
		m_splitter.SetOrientation(IsHorizontal());
		m_caption.SetOrientation(IsHorizontal());
    }

	bool IsHorizontal() const
	{
		return m_side.IsHorizontal();
	}

	bool IsTop() const
	{
		return m_side.IsTop();
	}
public:
	void GetMinMaxInfo(LPMINMAXINFO pMinMaxInfo) const
	{
		pMinMaxInfo->ptMinTrackSize.x=0;
		pMinMaxInfo->ptMinTrackSize.y=0;
	}

	LRESULT NcHitTest(const CPoint& pt)
	{
		LRESULT lRes=HTNOWHERE;
		if(m_splitter.PtInRect(pt))
			lRes=(IsHorizontal()) ? HTSPLITTERV : HTSPLITTERH;
		else
		{
			lRes=m_caption.HitTest(pt);
			if(lRes==HTNOWHERE)
					lRes=HTCLIENT;
			else
			{
				if(GetCapture()==NULL)
					m_caption.HotTrack(m_hWnd,(unsigned int)lRes);
			}
		}
		return lRes;
	}
	void NcCalcSize(CRect* pRc)
	{
		m_splitter.CalculateRect(*pRc,m_side.Side());
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
		m_splitter.Draw(dc);
	}
	bool CloseBtnPress()
	{
		PostMessage(WM_CLOSE);
		return false;
	}
	bool PinBtnPress(bool bVisualize=true)
	{
		return true;
	}
	void StartResizing(const CPoint& pt)
	{
		assert(false);
	}
	bool OnClosing()
	{
		return false;
	}
	bool AnimateWindow(long time,bool bShow)
	{
		const int n=10;
		CRect rc;
		GetWindowRect(&rc);
		CWindow parent(GetParent());
		if(parent.m_hWnd!=NULL)
			parent.ScreenToClient(&rc);
		long* ppoint;
		long  step;
		CRect rcInvalidate(rc);
		long* pipoint;
		if(m_side.IsHorizontal())
		{
			step=rc.Height()/n;
			if(m_side.IsTop())
			{
				ppoint=&rc.bottom;
				pipoint=&rc.bottom;
				rcInvalidate.bottom=rcInvalidate.top;
			}
			else
			{
				ppoint=&rc.top;
				pipoint=&rc.top;
				rcInvalidate.top=rcInvalidate.bottom;
				step=-step;
			}
		}
		else
		{
			step=rc.Width()/n;
			if(m_side.IsTop())
			{
				ppoint=&rc.right;
				pipoint=&rc.right;
				rcInvalidate.left=rcInvalidate.right;
			}
			else
			{
				ppoint=&rc.left;
				pipoint=&rc.left;
				rcInvalidate.right=rcInvalidate.left;
				step=-step;
			}
		}
		if(!bShow)
			step=-step;
		else
		{
			parent.RedrawWindow(&rc,NULL,RDW_INVALIDATE | RDW_UPDATENOW);
			*ppoint-=step*n;
			SetWindowPos(HWND_TOP,&rc,SWP_FRAMECHANGED|SWP_SHOWWINDOW);
		}
		bool bRes=true;
		for(int i=0;i<n;i++)
		{
			*ppoint+=step;
			bRes=(SetWindowPos(HWND_TOP,&rc,NULL)!=FALSE);
			if(!bShow)
			{
				*pipoint+=step;
				parent.RedrawWindow(&rcInvalidate,NULL,RDW_INVALIDATE | RDW_UPDATENOW);
			}
			else
			{
				CRect rcInvalidateClient(rc);
				parent.MapWindowPoints(m_hWnd,&rcInvalidateClient);
				RedrawWindow(&rcInvalidateClient,NULL,RDW_INVALIDATE | RDW_UPDATENOW);
			}
			Sleep(time/n);
		}
		return bRes;
	}
protected:
	BEGIN_MSG_MAP(thisClass)
		MESSAGE_HANDLER(WM_GETMINMAXINFO,OnGetMinMaxInfo)
		MESSAGE_HANDLER(WM_NCCALCSIZE, OnNcCalcSize)
		MESSAGE_HANDLER(WM_NCACTIVATE, OnNcActivate)
		MESSAGE_HANDLER(WM_NCHITTEST,OnNcHitTest)
		MESSAGE_HANDLER(WM_NCPAINT,OnNcPaint)
		MESSAGE_HANDLER(WM_SETTEXT,OnCaptionChange)
		MESSAGE_HANDLER(WM_SETICON,OnCaptionChange)
		MESSAGE_HANDLER(WM_NCLBUTTONDOWN, OnNcLButtonDown)
		MESSAGE_HANDLER(WM_NCLBUTTONUP,OnNcLButtonUp)
		MESSAGE_HANDLER(WM_NCLBUTTONDBLCLK,OnNcLButtonDblClk)
		MESSAGE_HANDLER(WM_CLOSE, OnClose)
		MESSAGE_HANDLER(WM_SETTINGCHANGE, OnSettingChange)
		MESSAGE_HANDLER(WM_SYSCOLORCHANGE, OnSysColorChange)
	END_MSG_MAP()

	LRESULT OnGetMinMaxInfo(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
	{
		T* pThis=static_cast<T*>(this);
		LRESULT lRes=pThis->DefWindowProc(uMsg,wParam,lParam);
		pThis->GetMinMaxInfo(reinterpret_cast<LPMINMAXINFO>(lParam));
		return lRes;
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

	LRESULT OnNcLButtonDown(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
	{
		if( (wParam==HTSPLITTERH) || (wParam==HTSPLITTERV) )
			static_cast<T*>(this)->StartResizing(CPoint(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)));
		else
			m_caption.OnAction(m_hWnd,(unsigned int)wParam);
        return 0;
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
			case HTPIN:
				bHandled=pThis->PinBtnPress();
				break;
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
	LRESULT OnClose(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM/* lParam*/, BOOL& bHandled)
	{
		T* pThis=reinterpret_cast<T*>(this);
		bHandled=pThis->OnClosing();
		return 0;
	}

	LRESULT OnSettingChange(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
	{
		// Note: We can depend on CDWSettings already being updated
		//  since we will always be a descendant of the main frame

		m_caption.UpdateMetrics();

		T* pThis=static_cast<T*>(this);
		pThis->SetWindowPos(NULL,0,0,0,0,SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);

		bHandled = FALSE;
		return 1;
	}

	LRESULT OnSysColorChange(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
	{
		// Note: We can depend on CDWSettings already being updated
		//  since we will always be a descendant of the main frame

		m_caption.UpdateMetrics();

		T* pThis=static_cast<T*>(this);
		pThis->SetWindowPos(NULL,0,0,0,0,SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);

		bHandled = FALSE;
		return 1;
	}
protected:
	CCaption		m_caption;
	CSplitterBar	m_splitter;
	CSide			m_side;
};

template <class TAutoHidePaneTraits = COutlookLikeAutoHidePaneTraits>
class CAutoHideManager : public CAutoHidePaneImpl<CAutoHideManager<TAutoHidePaneTraits>,CWindow,TAutoHidePaneTraits>
{
	typedef CAutoHidePaneImpl<CAutoHideManager<TAutoHidePaneTraits>,CWindow,TAutoHidePaneTraits>	baseClass;
	typedef CAutoHideManager<TAutoHidePaneTraits>					thisClass;
protected:
	typedef CAutoHideBar::CSide		CSide;
	enum{ tmID=1,tmTimeout=1300};
	enum{ animateTimeout=100};
	enum{ hoverTimeout=50/*HOVER_DEFAULT*/};
	enum{ barsCount=4 };

	class CSizeTrackerFull : public IDDTracker
	{
	protected:
		typedef ssec::bounds_type<long> CBounds;
	public:
		bool IsHorizontal() const
		{
			return m_side.IsHorizontal();
		}
		bool IsTop() const
		{
			return m_side.IsTop();
		}
		CSizeTrackerFull(HWND hWnd,CPoint pt,const CSide& side,long minSize,const CRect& rcBound)
			: m_wnd(hWnd),m_side(side)
		{
			m_rc.SetRectEmpty();
			m_wnd.GetWindowRect(&m_rc);
			CWindow wndParent=m_wnd.GetParent();
			wndParent.ScreenToClient(&m_rc);
			wndParent.ScreenToClient(&pt);
			if(IsHorizontal())
			{
				if(IsTop())
					m_ppos=&m_rc.bottom;
				else
					m_ppos=&m_rc.top;
				m_bounds.low=rcBound.top;
				m_bounds.hi=rcBound.bottom;
				m_offset=pt.y-*m_ppos;
			}
			else
			{
				if(IsTop())
					m_ppos=&m_rc.right;
				else
					m_ppos=&m_rc.left;
				m_bounds.low=rcBound.left;
				m_bounds.hi=rcBound.right;
				m_offset=pt.x-*m_ppos;
			}
			m_bounds.low+=minSize;
			m_bounds.hi-=minSize;
		}
        void OnMove(long x, long y)
        {
			long pos = IsHorizontal() ? y : x;
			pos-=m_offset;
			pos=m_bounds.bind(pos);
			if(*m_ppos!=pos)
			{
				*m_ppos=pos;
				Move();
			}
		}
		void SetPosition()
		{
			m_wnd.SetWindowPos(NULL,&m_rc,SWP_NOZORDER | SWP_NOACTIVATE);
		}
		virtual void Move()
		{
			SetPosition();
		}
		bool ProcessWindowMessage(MSG* pMsg)
		{
		   return (pMsg->message==WM_TIMER);
		}

	protected:
		CWindow		m_wnd;
		CBounds		m_bounds;
		CRect		m_rc;
		const CSide	m_side;
		long*		m_ppos;
		long		m_offset;
	};
	class CSizeTrackerGhost : public CSizeTrackerFull
	{
        typedef CSimpleSplitterBarSlider<CSplitterBar> CSlider;
	public:
		CSizeTrackerGhost(HWND hWnd,CPoint pt,const CSide& side,CSplitterBar& splitter,const CRect& rcBound)
			: CSizeTrackerFull(hWnd,pt,side,splitter.GetThickness(),rcBound),m_dc(NULL)
			,m_splitter(splitter),m_slider(splitter)
		{
			m_spOffset=m_slider-*m_ppos;
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
		virtual void Move()
		{
			m_splitter.CleanGhostBar(m_dc);
			m_slider=*m_ppos+m_spOffset;
			m_splitter.DrawGhostBar(m_dc);
		}
	protected:
		CWindowDC		m_dc;
		CSplitterBar&	m_splitter;
		CSlider			m_slider;
		long			m_spOffset;
	};
public:
	CAutoHideManager() 
		: m_barThickness(0),m_pActive(0),m_pTracked(0)
	{
		m_rcBound.SetRectEmpty();
		m_side=0;
	}
	bool Initialize(HWND hWnd)
	{
		ApplySystemSettings(hWnd);
		m_bars[CSide::sTop].Initialize(CSide(CSide::sTop));
		m_bars[CSide::sBottom].Initialize(CSide(CSide::sBottom));
		m_bars[CSide::sLeft].Initialize(CSide(CSide::sLeft));
		m_bars[CSide::sRight].Initialize(CSide(CSide::sRight));
		RECT rc={0,0,0,0};
		return (Create(hWnd,rc)!=NULL);
	}
	void ApplySystemSettings(HWND hWnd)
	{
		CClientDC dc(hWnd);
		CDWSettings settings;
		HFONT hOldFont=dc.SelectFont(settings.VSysFont());
		TEXTMETRIC tm;
		dc.GetTextMetrics(&tm);
		m_barThickness=tm.tmHeight;

		dc.SelectFont(settings.HSysFont());
		dc.GetTextMetrics(&tm);
		if(m_barThickness<tm.tmHeight)
			m_barThickness=tm.tmHeight;
		dc.SelectFont(hOldFont);
		int widthIcon=settings.CXMinIcon();
		assert(widthIcon==settings.CYMinIcon()); //if it throw let me know ;)
		if(widthIcon>m_barThickness)
			m_barThickness=widthIcon;
		m_barThickness+=2*IPinnedLabel::captionPadding+IPinnedLabel::labelEdge;
	}

	void UpdateLayout(CDC& dc,CRect& rc)
	{
		long leftPadding= ( m_bars[CSide::sLeft].IsVisible() ) ? m_barThickness : 0;
		long rightPadding=( m_bars[CSide::sRight].IsVisible() ) ? m_barThickness : 0;
		m_bars[CSide::sTop].CalculateRect( dc,rc,m_barThickness,leftPadding,rightPadding);
		m_bars[CSide::sBottom].CalculateRect( dc,rc,m_barThickness,leftPadding,rightPadding);

		leftPadding=0;
		rightPadding=0;

		m_bars[CSide::sLeft].CalculateRect( dc,rc,m_barThickness,leftPadding,rightPadding);
		m_bars[CSide::sRight].CalculateRect( dc,rc,m_barThickness,leftPadding,rightPadding);

		m_rcBound.CopyRect(&rc);
		if(m_pActive)
			FitPane();
	}
	long Width() const
	{
		long width=0;
		if(m_bars[CSide::sLeft].IsVisible())
			width+=m_barThickness;
		if(m_bars[CSide::sRight].IsVisible())
			width+=m_barThickness;
		return width;
	}
	long Height() const
	{
		long height=0;
		if(m_bars[CSide::sTop].IsVisible())
			height+=m_barThickness;
		if(m_bars[CSide::sBottom].IsVisible())
			height+=m_barThickness;
		return height;
	}
	bool FitPane()
	{
		CRect rc(m_rcBound);
		long spliterWidth=m_splitter.GetThickness();
		long width=m_pActive->Width()+spliterWidth;
		if(IsHorizontal())
		{
			long maxWidth=rc.Height();
			maxWidth=(maxWidth<spliterWidth) ? spliterWidth : maxWidth-spliterWidth;
			if(IsTop())
				rc.bottom=rc.top+( (maxWidth>width) ? width : maxWidth );
			else
				rc.top=rc.bottom-( (maxWidth>width) ? width : maxWidth );
		}
		else
		{
			long maxWidth=rc.Width();
			maxWidth=(maxWidth<spliterWidth) ? spliterWidth : maxWidth-spliterWidth;
			if(IsTop())
				rc.right=rc.left+( (maxWidth>width) ? width : maxWidth );
			else
				rc.left=rc.right-( (maxWidth>width) ? width : maxWidth );
		}
		return (SetWindowPos(HWND_TOP,rc,SWP_NOACTIVATE)!=FALSE);
	}

	IPinnedLabel::CPinnedWindow* LocatePinnedWindow(const CPoint& pt) const
	{
		IPinnedLabel::CPinnedWindow* ptr=0;
		for(int i=0;i<barsCount;i++)
		{
			ptr=m_bars[i].MouseEnter(pt);
			if(ptr!=0)
				break;
		}
		return ptr;
	}
	bool IsPtIn(const CPoint& pt) const
	{
		bool bRes;
		for(int i=0;i<barsCount;i++)
		{
			bRes=m_bars[i].IsPtIn(pt);
			if(bRes)
				break;
		}
		return bRes;
	}
	bool MouseEnter(HWND hWnd,const CPoint& pt)
	{
		IPinnedLabel::CPinnedWindow* ptr=0;
		for(int i=0;i<barsCount;i++)
		{
			CAutoHideBar* pbar=m_bars+i;
			ptr=pbar->MouseEnter(pt);
			if((ptr!=0)
				&& IsVisualizationNeeded(ptr))
			{
				m_pTracked=ptr;
				TRACKMOUSEEVENT tme = { 0 };
				tme.cbSize = sizeof(tme);
				tme.hwndTrack = hWnd;
				tme.dwFlags = TME_HOVER;
				tme.dwHoverTime = hoverTimeout;
				::_TrackMouseEvent(&tme);
				break;
			}
		}
		return (ptr!=0);
	}

	bool MouseHover(HWND hWnd,const CPoint& pt)
	{
		IPinnedLabel::CPinnedWindow* ptr=0;
		for(int i=0;i<barsCount;i++)
		{
			CAutoHideBar* pbar=m_bars+i;
			ptr=pbar->MouseEnter(pt,true);
			if((ptr!=0)
				&& (ptr==m_pTracked)
					&&IsVisualizationNeeded(ptr))
			{
				CClientDC dc(hWnd);
				pbar->Draw(dc);
				Visualize(ptr,i,true);
				break;
			}
		}
		return (ptr!=0);
	}

	void Draw(CDC& dc)
	{
		EraseBackground(dc);
		for(int i=0;i<barsCount;i++)
			m_bars[i].Draw(dc,false);
	}
	void EraseBackground(CDC& dc)
	{
		CDWSettings settings;
		CBrush bgrBrush;
		bgrBrush.CreateSolidBrush(settings.CoolCtrlBackgroundColor());
		HBRUSH hOldBrush=dc.SelectBrush(bgrBrush);
		CRect rcTop(m_bars[CSide::sTop].operator const CRect&());
		CRect rcBottom(m_bars[CSide::sBottom].operator const CRect&());

		if(m_bars[CSide::sLeft].IsVisible())
		{
			const CRect& rc=m_bars[CSide::sLeft].operator const CRect&();
			rcTop.left-=rc.Height();
			rcBottom.left-=rc.Height();
			dc.PatBlt(rc.left, rc.top, rc.Width(), rc.Height(), PATCOPY);
		}
		if(m_bars[CSide::sRight].IsVisible())
		{
			const CRect& rc=m_bars[CSide::sRight].operator const CRect&();
			rcTop.right+=rc.Height();
			rcBottom.right+=rc.Height();
			dc.PatBlt(rc.left, rc.top, rc.Width(), rc.Height(), PATCOPY);
		}

		if(m_bars[CSide::sTop].IsVisible())
			dc.PatBlt(rcTop.left, rcTop.top, rcTop.Width(), rcTop.Height(), PATCOPY);
		if(m_bars[CSide::sBottom].IsVisible())
			dc.PatBlt(rcBottom.left, rcBottom.top, rcBottom.Width(), rcBottom.Height(), PATCOPY);

		dc.SelectBrush(hOldBrush);
	}

	bool PinUp(HWND hWnd,DFPINUP* pHdr,bool& bUpdate)
	{
		pHdr->hdr.hBar=m_hWnd;
		CSide side(pHdr->dwDockSide);
		assert(side.IsValid());
		CAutoHideBar* pbar=m_bars+side.Side();
		bUpdate=!pbar->IsVisible();
		IPinnedLabel* pLabel=pbar->Insert(pHdr);
		bool bRes=(pLabel!=0);
		if(bRes&& ((pHdr->dwFlags&DFPU_VISUALIZE)!=0))
			Visualize(pLabel->ActivePinnedWindow(),side);
		if(!bUpdate)
		{
			CClientDC dc(hWnd);
			pbar->UpdateLayout(dc);
			pbar->Draw(dc);
		}
		return bRes;
	}

	bool Remove(HWND hWnd,bool bUnpin=false)
	{
		if(m_pActive!=0 && (m_pActive->Wnd()==hWnd ) )
												Vanish();

		HDOCKBAR hBar=GetParent();
		assert(::IsWindow(hBar));
		DFDOCKPOS* pHdr=0;
		DFDOCKPOS dockHdr={0};
		if(bUnpin)
		{
//			dockHdr.hdr.code=DC_SETDOCKPOSITION;
			dockHdr.hdr.hWnd=hWnd;
			dockHdr.hdr.hBar=hBar;
			pHdr=&dockHdr;
		}
		bool bRes=false;
		for(int i=0;i<barsCount;i++)
		{
			CAutoHideBar* pbar=m_bars+i;
			bRes=pbar->Remove(hWnd,m_hWnd,pHdr);
			if(bRes)
			{
				if(pbar->IsVisible())
				{
					CClientDC dc(hBar);
					pbar->UpdateLayout(dc);
					pbar->Draw(dc);
				}
				else
				{
					::SendMessage(hBar, WM_SIZE, 0, 0);
					::RedrawWindow(hBar,NULL,NULL,
										RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE);
				}
				break;
			}
		}
		return bRes;
	}

	bool GetDockingPosition(DFDOCKPOS* pHdr) const
	{
		bool bRes=false;
		for(int i=0;i<barsCount;i++)
		{
			bRes=m_bars[i].GetDockingPosition(pHdr);
			if(bRes)
				break;
		}
		return bRes;
	}
///////////////////////////////////////////////////////////
	bool IsVisualizationNeeded(const IPinnedLabel::CPinnedWindow* ptr) const
	{
		return (ptr!=m_pActive);
	}

	bool Visualize(IPinnedLabel::CPinnedWindow* ptr,const CSide& side,bool bAnimate=false)
	{
		assert(ptr);
		assert(IsVisualizationNeeded(ptr));
		Vanish();
		Orientation(side);
		assert(m_pActive==0);
		m_pActive=ptr;
		bool bRes=(::SetWindowPos(m_pActive->Wnd(),HWND_TOP,0,0,0,0,
							SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW)!=FALSE);
		if(bRes)
		{
			bRes=FitPane();
			if(bRes)
			{
				SetWindowText(m_pActive->Text());
				CDWSettings setting;
				if(bAnimate && setting.IsAnimationEnabled())
					AnimateWindow(animateTimeout,true);
				else
					bRes=(SetWindowPos(HWND_TOP,0,0,0,0,
											SWP_NOMOVE | SWP_NOSIZE |
											SWP_SHOWWINDOW | SWP_FRAMECHANGED )!=FALSE);
				if(bRes)
				{
					BOOL dummy;
					OnSize(0,0,0,dummy);
					bRes=(SetTimer(tmID,tmTimeout)==tmID);
				}
			}
		}
		assert(bRes);
		return bRes;
	}

	bool Vanish(bool bAnimate=false)
	{
		bool bRes=(m_pActive==0);
		if(!bRes)
		{
			KillTimer(tmID);
			::ShowWindow(m_pActive->Wnd(),SW_HIDE);
			m_pActive=0;
			CDWSettings setting;
			if(bAnimate && setting.IsAnimationEnabled())
				AnimateWindow(animateTimeout,false);
			bRes=ShowWindow(SW_HIDE)!=FALSE;
		}
		return bRes;
	}
///////////////////////////////////////////////////////////
	void StartResizing(const CPoint& pt)
	{
		std::auto_ptr<CSizeTrackerFull> pTracker;
		CDWSettings settings;
		if(settings.GhostDrag())
		{
			CRect rc;
			GetWindowRect(&rc);
			CSplitterBar splitter(IsHorizontal());
			splitter.CalculateRect(rc,m_side.Side());
			pTracker=std::auto_ptr<CSizeTrackerFull>(
								new CSizeTrackerGhost(m_hWnd,pt,Orientation(),splitter,m_rcBound));
		}
		else
			pTracker=std::auto_ptr<CSizeTrackerFull>(
								new CSizeTrackerFull(m_hWnd,pt,Orientation(),m_splitter.GetThickness(),m_rcBound));

		HWND hWndParent=GetParent();
		assert(hWndParent);
		TrackDragAndDrop(*pTracker,hWndParent);
	}

	bool PinBtnPress()
	{
		assert(m_pActive);
		return Remove(m_pActive->Wnd(),true);
	}

	bool OnClosing()
	{
		assert(m_pActive);
		::PostMessage(m_pActive->Wnd(),WM_CLOSE,NULL,NULL);
		return true;
	}

	DECLARE_WND_CLASS(_T("CAutoHideManager"))
protected:
	BEGIN_MSG_MAP(thisClass)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_TIMER,OnTimer)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
/////////////////
		MESSAGE_HANDLER(WMDF_DOCK,OnDock)
		CHAIN_MSG_MAP(baseClass)
	END_MSG_MAP()

	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
	{
		bHandled = false;
		Vanish(false);
		return 0;
	}

	LRESULT OnTimer(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	{
		POINT pt;
		CRect rc;
		GetCursorPos(&pt);
		GetWindowRect(&rc);
		if(!rc.PtInRect(pt))
		{
			CWindow wndParent (GetParent());
			wndParent.ScreenToClient(&pt);

			IPinnedLabel::CPinnedWindow* ptr=LocatePinnedWindow(pt);
			if(ptr==0 || IsVisualizationNeeded(ptr))
			{
				HWND hWnd=GetFocus();
				while( hWnd!=m_hWnd )
				{
					if(hWnd==NULL)
					{
						Vanish(true);
							break;
					}
					hWnd=::GetParent(hWnd);
				}
			}
		}
		return 0;
	}

    LRESULT OnSize(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
    {
        if(wParam != SIZE_MINIMIZED && (m_pActive!=0))
        {
			CRect rc;
			GetClientRect(&rc);
			::SetWindowPos(m_pActive->Wnd(),NULL,
							 rc.left,rc.top,
							 rc.Width(),rc.Height(),
							 SWP_NOZORDER | SWP_NOACTIVATE);
			GetWindowRect(&rc);
			long width = (IsHorizontal())	? rc.Height() : rc.Width();
			width -= m_splitter.GetThickness();
			if(width>m_caption.GetThickness()/*0*/)
				m_pActive->Width(width);
        }
        bHandled = FALSE;
        return 1;
    }
    LRESULT OnDock(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/)
    {
		LRESULT lRes=FALSE;
		DFMHDR* pHdr=reinterpret_cast<DFMHDR*>(lParam);
		switch(pHdr->code)
		{
			case DC_UNDOCK:
				assert(::IsWindow(pHdr->hWnd));
				lRes=Remove(pHdr->hWnd);
				break;
		}
		return lRes;
	}
protected:
	long							m_barThickness;
	IPinnedLabel::CPinnedWindow*	m_pActive;
	IPinnedLabel::CPinnedWindow*	m_pTracked;
	CRect							m_rcBound;
	CAutoHideBar					m_bars[barsCount];
};

}//namespace dockwins
#endif // __WTL_DW__DWAUTOHIDE_H__
