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

#ifndef __WTL_DW__DOCKMISC_H__
#define __WTL_DW__DOCKMISC_H__

#pragma once

#ifndef __ATLMISC_H__
        #error dockmisk.h requires atlmisc.h to be included first
#endif

#include <cassert>
#include "SimpleSplitterBar.h"

namespace dockwins{

#ifdef DF_AUTO_HIDE_FEATURES

#ifdef HTOBJECT
#define HTPIN HTOBJECT
#else
#define HTPIN 19
#endif

#endif

#ifndef DF_AUTO_HIDE_FEATURES
template <class TSplitterBar,/* DWORD TDockFrameStyle=0,*/
			DWORD t_dwStyle = 0, DWORD t_dwExStyle = 0>
struct CDockingFrameTraitsT : CWinTraits <t_dwStyle,t_dwExStyle>
{
	typedef TSplitterBar CSplitterBar;
};

typedef CDockingFrameTraitsT< CSimpleSplitterBar<5>,
		WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
		WS_EX_APPWINDOW | WS_EX_WINDOWEDGE> CDockingFrameTraits;


typedef CDockingFrameTraitsT< CSimpleSplitterBarEx<6>,
		WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,0> CDockingSiteTraits;

#endif
//////////////////messages///////////////////////////
#define WMDF_FIRST  (WM_USER)
#define WMDF_LAST	(WMDF_FIRST+1)

#define WMDF_NDOCKSTATECHANGED	(WMDF_FIRST)
//wParam MAKEWPARAM(DOCK=TRUE\UNDOCK=FALSE,bHorizontal)
//lParam hBar

#define DOCKED2HORIZONTAL(wParam) (HIWORD(wParam))

#define WMDF_DOCK				(WMDF_FIRST+1)
//WMDF_DOCK'z codes:
#define DC_ACCEPT				(1)
#define DC_DOCK					(2)
#define DC_UNDOCK				(3)
#define DC_GETDOCKPOSITION		(4)
#define DC_SETDOCKPOSITION		(5)

#define DC_ADJUSTDRAGRECT		(7)
#define DC_REPLACE				(8)
#define DC_ISBOX				(9)
#define DC_ACTIVATE				(10)

#define DC_ISPINNED				(11)
#define DC_PINUP				(12)

#define DC_GETMINDIST			(13)

#define DC_PINBTNPRESS			(14)

#define HDOCKBAR				HWND
#define HNONDOCKBAR				(0)

//WMDF_DOCK'z structures
typedef struct tagDFMHDR
{
	HWND		hWnd;
	HDOCKBAR	hBar;
	UINT		code;
}DFMHDR;

typedef struct tagDFDOCKREPLACE
{
	DFMHDR	hdr;
	HWND	hWnd;
}DFDOCKREPLACE;

typedef struct tagDFDOCKRECT
{
	DFMHDR	hdr;
	RECT	rect;
	DWORD	flag;
}DFDOCKRECT;

typedef struct tagDFDOCKPOS
{
	DFMHDR			hdr;
	DWORD			dwDockSide;
	union
	{
		struct
		{
			unsigned long	nBar;
			float			fPctPos;
			unsigned long	nWidth;
			unsigned long	nHeight;
		};
		RECT	rcFloat;
	};
	unsigned long	nIndex;
}DFDOCKPOS;

typedef struct tagDFDOCKPOSEX
{
	DFDOCKPOS	dockPos;
	RECT		rect;
	BOOL		bDocking;
	BOOL		bVisible;
} DFDOCKPOSEX;

typedef struct tagDFPINUP
{
	DFMHDR			hdr;
	DWORD			dwDockSide;
	unsigned long	nWidth;
	DWORD			dwFlags;
	unsigned long	n;
	HWND*			phWnds;
} DFPINUP;

typedef struct tagDFPINBTNPRESS
{
	DFMHDR	hdr;
	BOOL	bVisualize;
} DFPINBTNPRESS;

#define DFPU_VISUALIZE	(1)

class CDockingSide
{
	enum
	{
		mskVertical=1,	//horizontal or vertical
		mskTop=2,			//top or bottom
		mskSide=3
	};
public:
	enum
	{
		sTop=2,sBottom=0,
		sLeft=3,sRight=1,
		sSingle=4,			// only one window in row (or col)
		sInvalid=8,
		sActive=16,
		sPinned=32
	};
	CDockingSide(DWORD side=sInvalid):m_side(side)
	{
	}
	bool IsHorizontal() const
	{
		assert(IsValid());
		return ((m_side&mskVertical)==0);
	}
	bool IsTop() const
	{
		assert(IsValid());
		return ((m_side&mskTop)!=0);
	}
	DWORD Side() const
	{
		assert(IsValid());
		return m_side&mskSide;
	}
	bool IsSingle() const
	{
		assert(IsValid());
		return ((m_side&sSingle)!=0);
	}
	bool IsPinned() const
	{
		assert(IsValid());
		return ((m_side&sPinned)!=0);
	}
	bool IsActive() const
	{
		return ((m_side&sActive)!=0);
	}
	bool IsValid() const
	{
		return ((m_side&sInvalid)==0);
	}
	bool Invalidate()
	{
		Invalidate(m_side);
	}
	static void Invalidate(DWORD& side)
	{
		side|=sInvalid;
	}
	operator DWORD() const
	{
		return m_side;
	}
protected:
	DWORD m_side;
};

class COrientedRect : public CRect
{
public:
	COrientedRect(bool bHorizontal,unsigned long thickness)
		:m_thickness(thickness),m_bHorizontal(bHorizontal)
	{
		SetRectEmpty();
	}
	void SetOrientation(bool bHorizontal)
	{
		m_bHorizontal=bHorizontal;
	}
	bool GetOrientation() const
	{
		return m_bHorizontal;
	}
	bool IsHorizontal() const
	{
		return m_bHorizontal;
	}
	long GetThickness() const
	{
		return m_thickness;
	}
	bool CalculateRect(CRect& rc,bool bTop)
	{
		CopyRect(rc);
		if(IsHorizontal())
		{
			if(bTop)
				rc.top=bottom=top+GetThickness();
			else
				rc.bottom=top=bottom-GetThickness();
		}
		else
		{
			if(bTop)
				rc.left=right=left+GetThickness();
			else
				rc.right=left=right-GetThickness();
		}
		return true;
	}
protected:
	bool			m_bHorizontal;
	unsigned long	m_thickness;
};

class CDWStyle
{
public:
	enum
	{
		mskIgnoreSysSettings=0x00000001,//depend on system setings (Show window contents while dragging)
		mskGhostDrag=0x00000002,		//Show window contents while dragging
		mskAnimation=0x00000004			//Animate pinned window
	};
	enum
	{
		sIgnoreSysSettings=mskIgnoreSysSettings,
		sUseSysSettings=0,
		sFullDrag=0,
		sGhostDrag=mskGhostDrag,
		sAnimation=mskAnimation,
		sNoAnimation=0
	};
	CDWStyle(DWORD style=0)
		:m_style(style)
	{
	}
	bool IgnoreSystemSettings() const
	{
		return ((m_style&mskIgnoreSysSettings)!=0);
	}
	bool GhostDrag() const
	{
		return ((m_style&mskGhostDrag)!=0);
	}
	bool IsAnimationEnabled() const
	{
		return ((m_style&mskAnimation)!=0);
	}
	CDWStyle Set(const CDWStyle& style)
	{
		m_style=style;
		return *this;
	}
	CDWStyle Set(DWORD mask,const CDWStyle& style)
	{
		m_style&=(~mask);
		m_style|=style;
		return *this;
	}
	CDWStyle Toggle(DWORD mask)
	{
		DWORD imask=~mask;
		DWORD tmask=(~m_style)&mask;
		m_style=(m_style&imask)|tmask;
		return *this;
	}
	operator DWORD() const
	{
		return m_style;
	}
protected:
	DWORD m_style;
};

class CDWSettings
{
protected:
	class CSettings
	{
	public:
		typedef CDWStyle CStyle;

		CSettings() :
			m_colorCoolCtrlBackground(RGB(0,0,0)),
			m_colorAutoHideBarText(RGB(0,0,0)),
			m_hHResizeCursor(NULL),
			m_hVResizeCursor(NULL)
		{
			::ZeroMemory(&m_ncm, sizeof(m_ncm));

			Update();
		}

		void Update()
		{
			if(!m_style.IgnoreSystemSettings())
			{
				BOOL bFullDrag;
				BOOL bRes=::SystemParametersInfo(SPI_GETDRAGFULLWINDOWS, 0, &bFullDrag, 0);
				if(bRes)
					m_style.Set(CStyle::mskGhostDrag | CStyle::mskAnimation,
										bFullDrag
											? ( CStyle::sFullDrag | CStyle::sAnimation)
											: ( CStyle::sGhostDrag | CStyle::sNoAnimation));
			}
			HFONT hFont=reinterpret_cast<HFONT>(::GetStockObject(DEFAULT_GUI_FONT));
			assert(hFont);
			if(hFont!=m_hfont)
			{
				m_hfont=hFont;
				LOGFONT lf;
				m_hfont.GetLogFont(&lf);
				lf.lfOutPrecision = OUT_TT_ONLY_PRECIS;
				lf.lfEscapement   = 2700;
				lf.lfOrientation  = 2700;
				m_vfont.Attach( ::CreateFontIndirect(&lf) );
				assert(m_vfont.m_hFont);
			}
			m_ncm.cbSize = sizeof(m_ncm);
			if(::SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(m_ncm), &m_ncm, 0))
			{
				LOGFONT lf = m_ncm.lfSmCaptionFont;
				lf.lfWeight = FW_NORMAL;
				m_hfontSmCaption.Attach( ::CreateFontIndirect(&lf) );
				assert(m_hfontSmCaption.m_hFont);
				lf.lfOutPrecision = OUT_TT_ONLY_PRECIS;
				lf.lfEscapement   = 2700;
				lf.lfOrientation  = 2700;
				m_vfontSmCaption.Attach( ::CreateFontIndirect(&lf) );
				assert(m_vfontSmCaption.m_hFont);
			}

			m_hHResizeCursor=::LoadCursor(NULL, IDC_SIZENS );
			m_hVResizeCursor=::LoadCursor(NULL, IDC_SIZEWE );
			m_colorCoolCtrlBackground=CreateCoolCtrlBackgroundColor();
			m_colorAutoHideBarText=CreateCoolCtrlTextColor();
		}
		static COLORREF CreateCoolCtrlTextColor()
		{
			COLORREF clrBtnTxt=::GetSysColor(COLOR_BTNTEXT);
			COLORREF clrGrayTxt=::GetSysColor(COLOR_GRAYTEXT);
			int red =   GetRValue(clrBtnTxt);
			int green = GetGValue(clrBtnTxt);
			int blue =  GetBValue(clrBtnTxt);

			red+=(GetRValue(clrGrayTxt)-red)/3*2;
			green+=(GetGValue(clrGrayTxt)-green)/3*2;
			blue+=(GetBValue(clrGrayTxt)-blue)/3*2;

			return RGB(red, green, blue);
		}
//I stole the code for background brush color from the cool Daniel Bowen's(dbowen@es.com)
//class CDotNetTabCtrlImpl(DotNetTabCtrl.h) to make background of the auto-hide bars and
//the tab controls identically.
		static COLORREF CreateCoolCtrlBackgroundColor()
		{
			COLORREF clrBtnFace = ::GetSysColor(COLOR_BTNFACE);
			// This is a brave attempt to mimic the algorithm that Visual Studio.Net
			// uses to calculate the tab's background color and inactive tab color.
			// The other colors that VS.Net uses seems to be standard ones,
			// but these two colors are calculated.
			BYTE nRed = 0, nGreen = 0, nBlue = 0, nMax = 0;

			// Early experiments seemed to reveal that the background color is dependant
			// on COLOR_BTNFACE.  The following algorithm is just an attempt
			// to match several empirical results.  I tested with 20 variations
			// on COLOR_BTNFACE and kept track of what the tab background became.
			// I then brought the numbers into Excel, and started crunching on the numbers
			// until I came up with a formula that seems to pretty well match.

			nRed = GetRValue(clrBtnFace);
			nGreen = GetGValue(clrBtnFace);
			nBlue = GetBValue(clrBtnFace);

			nMax = (nRed > nGreen) ? ((nRed > nBlue) ? nRed : nBlue) : ((nGreen > nBlue) ? nGreen : nBlue);
			const BYTE nMagicBackgroundOffset = (nMax > (0xFF - 35)) ? (BYTE)(0xFF - nMax) : (BYTE)35;
			if(nMax == 0)
			{
				nRed = (BYTE)(nRed + nMagicBackgroundOffset);
				nGreen = (BYTE)(nGreen + nMagicBackgroundOffset);
				nBlue = (BYTE)(nBlue + nMagicBackgroundOffset);
			}
			else
			{
				nRed = (BYTE)(nRed + (nMagicBackgroundOffset*(nRed/(double)nMax) + 0.5));
				nGreen = (BYTE)(nGreen + (nMagicBackgroundOffset*(nGreen/(double)nMax) + 0.5));
				nBlue = (BYTE)(nBlue + (nMagicBackgroundOffset*(nBlue/(double)nMax) + 0.5));
			}

			return RGB(nRed, nGreen, nBlue);
		}
		CStyle SetStyle(const CStyle& style)
		{
			m_style=style;
			return m_style;
		}
		CStyle GetStyle() const
		{
			return m_style;
		}
		bool IgnoreSystemSettings() const
		{
			return m_style.IgnoreSystemSettings();
		}
		bool GhostDrag() const
		{
			return m_style.GhostDrag();
		}
		bool IsAnimationEnabled() const
		{
			return m_style.IsAnimationEnabled();
		}
		HFONT HSysFont() const
		{
			return m_hfont;
		}
		HFONT VSysFont() const
		{
			return m_vfont;
		}
		HFONT HSmCaptionFont() const
		{
			return m_hfontSmCaption;
		}
		HFONT VSmCaptionFont() const
		{
			return m_vfontSmCaption;
		}
		long CXMinIcon() const
		{
			return ::GetSystemMetrics(SM_CXSMICON);
		}
		long CYMinIcon() const
		{
			return ::GetSystemMetrics(SM_CYSMICON);
		}
		int CXSmCaption() const
		{
			return m_ncm.iSmCaptionWidth;
		}
		int CYSmCaption() const
		{
			return m_ncm.iSmCaptionHeight;
		}
		COLORREF AutoHideBarTextColor() const
		{
			return m_colorAutoHideBarText;
		}
		COLORREF CoolCtrlBackgroundColor() const
		{
			return m_colorCoolCtrlBackground;
		}
		HCURSOR	HResizeCursor() const
		{
			return m_hHResizeCursor;
		}
		HCURSOR	VResizeCursor() const
		{
			return m_hVResizeCursor;
		}
	protected:
		COLORREF	m_colorCoolCtrlBackground;
		COLORREF	m_colorAutoHideBarText;
		CStyle		m_style;
		CFont		m_vfont;
		CFontHandle m_hfont;
		CFont		m_hfontSmCaption;
		CFont		m_vfontSmCaption;
		HCURSOR		m_hHResizeCursor;
		HCURSOR		m_hVResizeCursor;
		NONCLIENTMETRICS m_ncm;

	};
public:
	typedef CSettings::CStyle CStyle;
	void Update() const
	{
		settings.Update();
	}
	CStyle SetStyle(const CStyle& style) const
	{
		return settings.SetStyle(style);
	}
	CStyle GetStyle() const
	{
		return settings.GetStyle();
	}
	bool IgnoreSystemSettings() const
	{
		return settings.IgnoreSystemSettings();
	}
	bool GhostDrag() const
	{
		return settings.GhostDrag();
	}
	bool IsAnimationEnabled() const
	{
		return settings.IsAnimationEnabled();
	}

	HFONT HSysFont() const
	{
		return settings.HSysFont();
	}
	HFONT VSysFont() const
	{
		return settings.VSysFont();
	}
	HFONT HSmCaptionFont() const
	{
		return settings.HSmCaptionFont();
	}
	HFONT VSmCaptionFont() const
	{
		return settings.VSmCaptionFont();
	}
	long CXMinIcon() const
	{
		return settings.CXMinIcon();
	}
	long CYMinIcon() const
	{
		return settings.CYMinIcon();
	}
	int CXSmCaption() const
	{
		return settings.CXSmCaption();
	}
	int CYSmCaption() const
	{
		return settings.CYSmCaption();
	}
	COLORREF AutoHideBarTextColor() const
	{
		return settings.AutoHideBarTextColor();
	}
	COLORREF CoolCtrlBackgroundColor() const
	{
		return settings.CoolCtrlBackgroundColor();
	}
	HCURSOR	HResizeCursor() const
	{
		return settings.HResizeCursor();
	}
	HCURSOR	VResizeCursor() const
	{
		return settings.VResizeCursor();
	}

protected:
	static CSettings settings;
};

void DrawEllipsisText(CDC& dc,LPCTSTR sText,int n,LPRECT prc,bool bHorizontal);

}//namespace dockwins
#endif // __WTL_DW__DOCKMISC_H__