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

#ifndef __WTL_DW__SIMPLESPLITTERBAR_H__
#define __WTL_DW__SIMPLESPLITTERBAR_H__

#pragma once

#ifndef __ATLMISC_H__
        #error SimpleSplitterBar.h requires atlmisc.h to be included first
#endif

namespace dockwins{

template<long TThickness=5>
class CSimpleSplitterBar :  public CRect
{
public:
	enum{sbThickness=TThickness};

	CSimpleSplitterBar(bool bHorizontal=true)
		:m_bHorizontal(bHorizontal)
	{
		SetRectEmpty();
	}
	CSimpleSplitterBar(const CSimpleSplitterBar& ref)
		:CRect(ref),m_bHorizontal(ref.IsHorizontal())
	{

	}
	static long GetThickness()
	{
		return sbThickness;
	}
	void SetOrientation(bool bHorizontal)
	{
		m_bHorizontal=bHorizontal;
	}
	bool IsHorizontal() const
	{
		return m_bHorizontal;
	}

	bool IsPtIn(const CPoint& pt) const
	{
		return (PtInRect(pt)!=FALSE);
	}

	HCURSOR GetCursor(const CPoint& pt) const
	{
		HCURSOR hCursor=NULL;
		if(IsPtIn(pt))
		{
			CDWSettings settings;
			 hCursor=((IsHorizontal())
								?settings.HResizeCursor()
								:settings.VResizeCursor());
		}
		return hCursor;
	}

    void Draw(CDC& dc) const
    {
        dc.FillRect(this, (HBRUSH)LongToPtr(COLOR_3DFACE + 1));
    }
	void DrawGhostBar(CDC& dc) const
	{
        CBrush brush = CDCHandle::GetHalftoneBrush();
        if(brush.m_hBrush != NULL)
        {
            HBRUSH hBrushOld = dc.SelectBrush(brush);
            dc.PatBlt(this->left, this->top,this->Width(),this->Height(), PATINVERT);
            dc.SelectBrush(hBrushOld);

        }

	}
	void CleanGhostBar(CDC& dc) const
	{
		DrawGhostBar(dc);
	}
protected:
	bool m_bHorizontal;
};


template<long TThickness=5>
class CSimpleSplitterBarEx: public CSimpleSplitterBar<TThickness>
{
	typedef CSimpleSplitterBar<TThickness> baseClass;
public:
	CSimpleSplitterBarEx(bool bHorizontal=true)
		:baseClass(bHorizontal)
	{
	}
	CSimpleSplitterBarEx(const CSimpleSplitterBarEx& ref)
		:baseClass(ref)
	{

	}

    void Draw(CDC& dc) const
    {
        dc.FillRect(this, (HBRUSH)LongToPtr(COLOR_3DFACE + 1));
		CRect rc(*this);
//		dc.DrawEdge(&rc, EDGE_RAISED, (IsHorizontal()) ? (BF_TOP | BF_BOTTOM) : (BF_LEFT | BF_RIGHT) );
		dc.DrawEdge(&rc, BDR_RAISEDINNER, (IsHorizontal()) ? (BF_TOP | BF_BOTTOM) : (BF_LEFT | BF_RIGHT) );

    }

};

template<class T>
class CSimpleSplitterBarSlider
{
	typedef T CSplitter;
	typedef CSimpleSplitterBarSlider<CSplitter> thisClass;
public:
	CSimpleSplitterBarSlider(CSplitter& splitter)
		:m_splitter(splitter)
	{
		if(!m_splitter.IsHorizontal())
		{
			m_pTop=&m_splitter.left;
			m_pDependant=&m_splitter.right;
		}
		else
		{
			m_pTop=&m_splitter.top;
			m_pDependant=&m_splitter.bottom;
		}
	}
	operator long() const
	{
		return *m_pTop;
	}

	long operator=(long val)
	{
		*m_pDependant=val+m_splitter.GetThickness();
		return *m_pTop=val;
	}

	thisClass& operator += (long val)
	{
		*this=*this+val;
		return *this;
	}
	thisClass& operator -= (long val)
	{
		*this=*this-val;
		return *this;
	}

protected:
	CSplitter& m_splitter;
	long	*m_pTop;
	long	*m_pDependant;
};

}//namespace dockwins

#endif // __WTL_DW__SIMPLESPLITTERBAR_H__
