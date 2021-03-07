#ifndef __SIMPLE_CUSTOMTABCTRLS_H__
#define __SIMPLE_CUSTOMTABCTRLS_H__

#pragma once

/////////////////////////////////////////////////////////////////////////////
// Sample tab controls derived from CCustomTabCtrl
//
// CButtonTabCtrl
// CFolderTabCtrl
// CSimpleDotNetTabCtrl
//
// Written by Bjarke Viksoe (bjarke@viksoe.dk)
// Updated for new CCustomTabCtrl by Daniel Bowen (dbowen@es.com).
// Copyright (c) 2001-2002 Bjarke Viksoe.
//
// CFolderTabCtrl code based on a Paul DiLascia MSJ 1999 article.
//
// This code may be used in compiled form in any way you desire. This
// file may be redistributed by any means PROVIDING it is 
// not sold for profit without the authors written consent, and 
// providing that this notice and the authors name is included. 
//
// This file is provided "as is" with no expressed or implied warranty.
// The author accepts no liability if it causes any damage to you or your
// computer whatsoever. It's free, so don't hassle me about it.
//
// Beware of bugs.

template <class TItem = CCustomTabItem>
class CButtonTabCtrl : 
   public CCustomTabCtrl<CButtonTabCtrl<TItem>, TItem>
{
protected:
    typedef CCustomTabCtrl<CButtonTabCtrl, TItem> customTabClass;

public:
   DECLARE_WND_CLASS2(_T("WTL_ButtonTabCtrl"), CButtonTabCtrl)

   BEGIN_MSG_MAP(CButtonTabCtrl)
      MESSAGE_HANDLER(WM_SETTINGCHANGE, OnSettingChange)
      CHAIN_MSG_MAP(customTabClass)
   END_MSG_MAP()

   LRESULT OnSettingChange(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
   {
      this->m_settings.iPadding = 10;
      this->m_settings.iMargin = 3;

      this->UpdateLayout();
      this->Invalidate();
      return 0;
   }

   // Overrides from CCustomTabCtrl
   void Initialize(void)
   {
      ATLASSERT(::IsWindow(this->m_hWnd));
      ATLASSERT(this->GetStyle() & WS_CHILD);
      this->ModifyStyle(0, SS_NOTIFY); // We need this for mouse-clicks

      customTabClass::Initialize();
   }

   void DoItemPaint(LPNMCTCCUSTOMDRAW lpNMCustomDraw)
   {
      CDCHandle dc(lpNMCustomDraw->nmcd.hdc);
      RECT &rc = lpNMCustomDraw->nmcd.rc;
      int nItem = lpNMCustomDraw->nmcd.dwItemSpec;
      UINT uItemState = lpNMCustomDraw->nmcd.uItemState;

      UINT state = DFCS_BUTTONPUSH;
      if( CDIS_SELECTED == (uItemState & CDIS_SELECTED) )
      {
          state |= DFCS_PUSHED;
      }
      if( CDIS_DISABLED == (uItemState & CDIS_DISABLED) )
      {
          state |= DFCS_INACTIVE;
      }
      dc.DrawFrameControl(&rc, DFC_BUTTON, state );
      
	    auto pItem = this->GetItem(nItem);
      if(pItem)
      {
         if( CDIS_SELECTED == (uItemState & CDIS_SELECTED) )
         {
            rc.left += 2;
            rc.top += 2;
         }
         CString sText = pItem->GetText();
         dc.DrawText(sText, sText.GetLength(), &rc, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
      }
   }
};

template <class TItem = CCustomTabItem>
class CFolderTabCtrl : 
   public CCustomTabCtrl<CFolderTabCtrl<TItem>, TItem>
{
protected:
    typedef CCustomTabCtrl<CFolderTabCtrl, TItem> customTabClass;

public:
   DECLARE_WND_CLASS2(_T("WTL_FolderTabCtrl"), CFolderTabCtrl)

   enum { CXOFFSET = 8 };     // defined pitch of trapezoid slant
   enum { CXMARGIN = 2 };     // left/right text margin
   enum { CYMARGIN = 1 };     // top/bottom text margin
   enum { CYBORDER = 1 };     // top border thickness
 
   BEGIN_MSG_MAP(CFolderTabCtrl)
      MESSAGE_HANDLER(WM_SETTINGCHANGE, OnSettingChange)
      CHAIN_MSG_MAP(customTabClass)
   END_MSG_MAP()

   LRESULT OnSettingChange(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
   {
      // Initialize font
     if (!this->m_font.IsNull())
       this->m_font.DeleteObject();
      LOGFONT lf = { 0 };      
      lf.lfHeight = ::GetSystemMetrics(SM_CYHSCROLL) - CYMARGIN;
      lf.lfWeight = FW_NORMAL;
      lf.lfCharSet = DEFAULT_CHARSET;
      ::lstrcpy(lf.lfFaceName, _T("Arial"));
      this->m_font.CreateFontIndirect(&lf);
 /*     
      NONCLIENTMETRICS ncm = { 0 };
      ncm.cbSize = sizeof(ncm);
      ::SystemParametersInfo(SPI_GETNONCLIENTMETRICS, 0, &ncm, 0);
      ncm.lfSmCaptionFont.lfWeight = FW_NORMAL;
      m_font.CreateFontIndirect(&ncm.lfSmCaptionFont);
 */

      this->m_settings.iPadding = CXOFFSET + 3;
      this->m_settings.iMargin = -CXOFFSET;

      this->UpdateLayout();
      this->Invalidate();
      return 0;
   }

   // Overrides from CCustomTabCtrl
   void Initialize(void)
   {
      ATLASSERT(::IsWindow(this->m_hWnd));
      ATLASSERT(this->GetStyle() & WS_CHILD);
      this->ModifyStyle(0, SS_NOTIFY); // We need this for mouse-clicks

      customTabClass::Initialize();
   }

   void DoItemPaint(LPNMCTCCUSTOMDRAW lpNMCustomDraw)
   {
      CDCHandle dc( lpNMCustomDraw->nmcd.hdc );
      RECT &rc = lpNMCustomDraw->nmcd.rc;
      bool bSelected = (CDIS_SELECTED == (lpNMCustomDraw->nmcd.uItemState & CDIS_SELECTED));

      COLORREF bgColor = bSelected ? ::GetSysColor(COLOR_WINDOW)     : lpNMCustomDraw->clrBtnFace;
      COLORREF fgColor = bSelected ? ::GetSysColor(COLOR_WINDOWTEXT) : lpNMCustomDraw->clrBtnText;

      CBrush brush;
      brush.CreateSolidBrush(bgColor);     // background brush
      dc.SetBkColor(bgColor);              // text background
      dc.SetTextColor(fgColor);            // text color = fg color

      CPen shadowPen;
      shadowPen.CreatePen(PS_SOLID, 1, lpNMCustomDraw->clrBtnShadow);

      // Fill trapezoid
      POINT pts[4];
      _GetTrapezoid(rc, pts);
      CPenHandle hOldPen = dc.SelectStockPen(BLACK_PEN);   
      CRgn rgn;
      rgn.CreatePolygonRgn(pts, 4, WINDING);
      dc.FillRgn(rgn, brush);

      // Draw edges. This requires two corrections:
      // 1) Trapezoid dimensions don't include the right and bottom edges,
      //    so must use one pixel less on bottom (cybottom)
      // 2) the endpoint of LineTo is not included when drawing the line, so
      //    must add one pixel (cytop)
      pts[1].y--;       // correction #1: true bottom edge y-coord
      pts[2].y--;       // ...ditto
      pts[3].y--;       // correction #2: extend final LineTo
      dc.MoveTo(pts[0]);              // upper left
      dc.LineTo(pts[1]);              // bottom left
      dc.SelectPen(shadowPen);        // bottom line is shadow color
      dc.MoveTo(pts[1]);              // line is inside trapezoid bottom
      dc.LineTo(pts[2]);              // ...
      dc.SelectStockPen(BLACK_PEN);   // upstroke is black
      dc.LineTo(pts[3]);              // y-1 to include endpoint
      if( !bSelected ) {
         // If not highlighted, upstroke has a 3D shadow, one pixel inside
         pts[2].x--;    // offset left one pixel
         pts[3].x--;    // ...ditto
         dc.SelectPen(shadowPen);
         dc.MoveTo(pts[2]);
         dc.LineTo(pts[3]);
      }
      dc.SelectPen(hOldPen);

	  auto pItem = this->GetItem(lpNMCustomDraw->nmcd.dwItemSpec);
      if(pItem) {
         // Draw text
         CString sText = pItem->GetText();
         ::InflateRect(&rc, -(CXOFFSET + CXMARGIN), -CYMARGIN);
         dc.DrawText(sText, sText.GetLength(), &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
      }

   }

   // Helper methods
   void _GetTrapezoid(const RECT& rc, POINT* pts) const
   {
      pts[0].x = rc.left;
      pts[0].y = rc.top;
      pts[1].x = rc.left + CXOFFSET;
      pts[1].y = rc.bottom;
      pts[2].x = rc.right - CXOFFSET - 1;
      pts[2].y = rc.bottom;
      pts[3].x = rc.right - 1, rc.top;
      pts[3].y = rc.top;
   }

};

template <class TItem = CCustomTabItem>
class CSimpleDotNetTabCtrl : 
   public CCustomTabCtrl<CSimpleDotNetTabCtrl<TItem>, TItem>
{
protected:
    typedef CCustomTabCtrl<CSimpleDotNetTabCtrl, TItem> customTabClass;

public:
   DECLARE_WND_CLASS2(_T("WTL_SimpleDotNetTabCtrl"), CSimpleDotNetTabCtrl)

   CFont m_font;  // DDB 2002/04/22: Leave this here even though the
                  //  base class now has CFont m_font, because there's
                  //  a "SetFont(m_fontSel)" that would cause
                  //  m_font and m_fontSel to be m_fontSel.
                  //  Keeping a version in this class here
                  //  will have the base class version
                  //  keep a copy of bold, but not cause
                  //  us to lose m_font.
   CBrush m_hbrBack;

   BEGIN_MSG_MAP(CSimpleDotNetTabCtrl)
      MESSAGE_HANDLER(WM_SETTINGCHANGE, OnSettingChange)
      CHAIN_MSG_MAP(customTabClass)
   END_MSG_MAP()

   LRESULT OnSettingChange(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
   {
      // Initialize font
     if (!this->m_font.IsNull())
        this->m_font.DeleteObject();
      NONCLIENTMETRICS ncm = { 0 };
      ncm.cbSize = sizeof(ncm);
      ::SystemParametersInfo(SPI_GETNONCLIENTMETRICS, 0, &ncm, 0);
      ncm.lfSmCaptionFont.lfWeight = FW_NORMAL;
      this->m_font.CreateFontIndirect(&ncm.lfSmCaptionFont);
      ncm.lfSmCaptionFont.lfWeight = FW_BOLD;
      this->m_fontSel.CreateFontIndirect(&ncm.lfSmCaptionFont);
      SetFont(this->m_fontSel);  // Bold font scales tabs correctly

      // Background brush
      if( !m_hbrBack.IsNull() ) m_hbrBack.DeleteObject();
      CWindowDC dc(NULL);
      int nBitsPerPixel = dc.GetDeviceCaps(BITSPIXEL);
      if( nBitsPerPixel > 8 ) {
         COLORREF clrBtnHilite = ::GetSysColor(COLOR_BTNHILIGHT);
         COLORREF clrBtnFace = ::GetSysColor(COLOR_BTNFACE);
         COLORREF clrLight = 
            RGB( GetRValue(clrBtnFace) + ((GetRValue(clrBtnHilite) - GetRValue(clrBtnFace)) / 2),
                 GetGValue(clrBtnFace) + ((GetGValue(clrBtnHilite) - GetGValue(clrBtnFace)) / 2),
                 GetBValue(clrBtnFace) + ((GetBValue(clrBtnHilite) - GetBValue(clrBtnFace)) / 2)
         );
         m_hbrBack.CreateSolidBrush(clrLight);
      }
      else {
         m_hbrBack =  CDCHandle::GetHalftoneBrush();
      }

      this->m_settings.iIndent = 6;
      this->m_settings.iPadding = 0;
      this->m_settings.iMargin = 2;
      this->m_settings.iSelMargin = 4;

      this->UpdateLayout();
      this->Invalidate();
      return 0;
   }

   // Overrides from CCustomTabCtrl
   void Initialize(void)
   {
      ATLASSERT(::IsWindow(this->m_hWnd));
      ATLASSERT(this->GetStyle() & WS_CHILD);
      this->ModifyStyle(0, SS_NOTIFY);  // We need this for mouse-clicks

      customTabClass::Initialize();
   }

   void InitializeDrawStruct(LPNMCTCCUSTOMDRAW lpNMCustomDraw)
   {
      DWORD dwStyle = this->GetStyle();

      lpNMCustomDraw->hFontInactive = this->m_font;
      if(CTCS_BOLDSELECTEDTAB == (dwStyle & CTCS_BOLDSELECTEDTAB))
      {
        lpNMCustomDraw->hFontSelected = (this->m_fontSel.IsNull() ? this->m_font : this->m_fontSel);
      }
      else
      {
        lpNMCustomDraw->hFontSelected = this->m_font;
      }
      lpNMCustomDraw->hBrushBackground = this->m_hbrBack;
      lpNMCustomDraw->clrTextSelected = ::GetSysColor(COLOR_BTNTEXT);
      lpNMCustomDraw->clrTextInactive = ::GetSysColor(COLOR_BTNTEXT);
      lpNMCustomDraw->clrSelectedTab = ::GetSysColor(COLOR_BTNFACE);
      lpNMCustomDraw->clrBtnFace = ::GetSysColor(COLOR_BTNFACE);
      lpNMCustomDraw->clrBtnShadow = ::GetSysColor(COLOR_BTNSHADOW);
      lpNMCustomDraw->clrBtnHighlight = ::GetSysColor(COLOR_BTNHIGHLIGHT);
      lpNMCustomDraw->clrBtnText = ::GetSysColor(COLOR_BTNTEXT);
      lpNMCustomDraw->clrHighlight = ::GetSysColor(COLOR_HIGHLIGHT);
#if WINVER >= 0x0500 || _WIN32_WINNT >= 0x0500
      lpNMCustomDraw->clrHighlightHotTrack = ::GetSysColor(COLOR_HOTLIGHT);
#else
      lpNMCustomDraw->clrHighlightHotTrack = ::GetSysColor(COLOR_HIGHLIGHT);
#endif
      lpNMCustomDraw->clrHighlightText = ::GetSysColor(COLOR_HIGHLIGHTTEXT);
   }

   void DoItemPaint(LPNMCTCCUSTOMDRAW lpNMCustomDraw)
   {
      CDCHandle dc( lpNMCustomDraw->nmcd.hdc );
      bool bSelected = (CDIS_SELECTED == (lpNMCustomDraw->nmcd.uItemState & CDIS_SELECTED));
      RECT &rc = lpNMCustomDraw->nmcd.rc;

      dc.FillRect(&rc, m_hbrBack);
      if( bSelected ) {
         // Tab is selected, so paint tab folder
         RECT rcTab = rc;
         rcTab.top += 5;
         rcTab.right--;
         dc.FillSolidRect(&rcTab, lpNMCustomDraw->clrSelectedTab);
         dc.SelectStockPen(WHITE_PEN);
         dc.MoveTo(rcTab.left, rcTab.bottom);
         dc.LineTo(rcTab.left, rcTab.top);
         dc.LineTo(rcTab.right, rcTab.top);
         dc.SelectStockPen(BLACK_PEN);
         dc.LineTo(rcTab.right, rcTab.bottom);
      }

	    auto pItem = this->GetItem(lpNMCustomDraw->nmcd.dwItemSpec);

      // Draw text
      //HFONT hOldFont = dc.SelectFont(bSelected ? lpNMCustomDraw->hFontSelected : lpNMCustomDraw->hFontInactive);
      HFONT hOldFont = dc.SelectFont(bSelected ? this->m_fontSel : this->m_font);
      RECT rcText = rc;
      ::InflateRect(&rcText, -this->m_settings.iPadding, 0);
      rcText.bottom -= 3;

      CString sText = pItem->GetText();
      dc.DrawText(sText, sText.GetLength(), &rcText, DT_CENTER | DT_BOTTOM | DT_SINGLELINE);
      dc.SelectFont(hOldFont);
   }
};

#endif
