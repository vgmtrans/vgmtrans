#ifndef __ATLGDIX_H__
#define __ATLGDIX_H__

/////////////////////////////////////////////////////////////////////////////
// Additional GDI/USER wrappers
//
// Written by Bjarke Viksoe (bjarke@viksoe.dk)
// Copyright (c) 2001-2002 Bjarke Viksoe.
// Thanks to Daniel Bowen for COffscreenDrawRect.
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
//

#pragma once

#ifndef __cplusplus
   #error ATL requires C++ compilation (use a .cpp suffix)
#endif

#ifndef __ATLGDI_H__
   #error atlgdix.h requires atlgdi.h to be included first
#endif

namespace WTL
{

/////////////////////////////////////////////////////////////////////////////
// Macros

// The GetXValue macros below are badly designed and emit
// compiler warnings e.g. when using RGB(255,255,255)...
#pragma warning(disable : 4310)

#ifndef BlendRGB
   #define BlendRGB(c1, c2, factor) \
      RGB( GetRValue(c1) + ((GetRValue(c2) - GetRValue(c1)) * factor / 100L), \
           GetGValue(c1) + ((GetGValue(c2) - GetGValue(c1)) * factor / 100L), \
           GetBValue(c1) + ((GetBValue(c2) - GetBValue(c1)) * factor / 100L) )
#endif

#ifndef COLOR_INVALID
   #define COLOR_INVALID  (COLORREF) CLR_INVALID
#endif


#if _WTL_VER < 0x0750

/////////////////////////////////////////////////////////////////////////////
// CIcon

template< bool t_bManaged >
class CIconT
{
public:
   HICON m_hIcon;

   // Constructor/destructor/operators

   CIconT(HICON hIcon = NULL) : m_hIcon(hIcon)
   { 
   }

   ~CIconT()
   {
      if( t_bManaged && m_hIcon != NULL ) ::DestroyIcon(m_hIcon);
   }

   CIconT<t_bManaged>& operator=(HICON hIcon)
   {
      m_hIcon = hIcon;
      return *this;
   }

   void Attach(HICON hIcon)
   {
      if( t_bManaged && m_hIcon != NULL ) ::DestroyIcon(m_hIcon);
      m_hIcon = hIcon;
   }  
   HICON Detach()
   {
      HICON hIcon = m_hIcon;
      m_hIcon = NULL;
      return hIcon;
   }

   operator HICON() const { return m_hIcon; }

   bool IsNull() const { return m_hIcon == NULL; }

   // Create methods

   HICON LoadIcon(_U_STRINGorID icon)
   {
      ATLASSERT(m_hIcon==NULL);
#if (_ATL_VER >= 0x0700)
      m_hIcon = ::LoadIcon(ATL::_AtlBaseModule.GetResourceInstance(), icon.m_lpstr);
#else
      m_hIcon = ::LoadIcon(_Module.GetResourceInstance(), icon.m_lpstr);
#endif
      return m_hIcon;
   }
   HICON LoadIcon(_U_STRINGorID icon, int cxDesired, int cyDesired, UINT fuLoad = 0)
   {
      ATLASSERT(m_hIcon==NULL);
#if (_ATL_VER >= 0x0700)
      m_hIcon = (HICON) ::LoadImage(ATL::_AtlBaseModule.GetResourceInstance(), icon.m_lpstr, IMAGE_ICON, cxDesired, cyDesired, fuLoad);
#else
      m_hIcon = (HICON) ::LoadImage(_Module.GetResourceInstance(), icon.m_lpstr, IMAGE_ICON, cxDesired, cyDesired, fuLoad);
#endif
      return m_hIcon;
   }
   HICON LoadOEMIcon(UINT nIDIcon) // for IDI_ types
   {
      ATLASSERT(m_hIcon==NULL);
      m_hIcon = ::LoadIcon(NULL, MAKEINTRESOURCE(nIDIcon));
      return m_hIcon;
   }
   HICON CreateIcon(int nWidth, int nHeight, BYTE cPlanes, BYTE cBitsPixel, CONST BYTE* lpbANDButs, CONST BYTE *lpbXORbits)
   {
      ATLASSERT(m_hIcon==NULL);
      ATLASSERT(lpbANDbits);
      ATLASSERT(lpbXORbits);
#if (_ATL_VER >= 0x0700)
      m_hIcon = ::CreateIcon(ATL::_AtlBaseModule.GetResourceInstance(), nWidth, nHeight, cPlanes, cBitsPixel, lpbANDbits, lpbXORbits);
#else
      m_hIcon = ::CreateIcon(_Module.GetResourceInstance(), nWidth, nHeight, cPlanes, cBitsPixel, lpbANDbits, lpbXORbits);
#endif
      return m_hIcon;
   }
   HICON CreateIconFromResource(PBYTE pBits, DWORD dwResSize, DWORD dwVersion = 0x00030000)
   {
      ATLASSERT(m_hIcon==NULL);
      ATLASSERT(pBits);
      m_hIcon = ::CreateIconFromResource(pBits, dwResSize, TRUE, dwVersion);
      return m_hIcon;
   }
   HICON CreateIconFromResourceEx(PBYTE pbBits, DWORD cbBits, DWORD dwVersion = 0x00030000, int cxDesired = 0, int cyDesired = 0, UINT uFlags = LR_DEFAULTCOLOR)
   {
      ATLASSERT(m_hIcon==NULL);
      ATLASSERT(pbBits);
      ATLASSERT(cbBits>0);
      m_hIcon = ::CreateIconFromResourceEx(pbBits, cbBits, TRUE, dwVersion, cxDesired,  cyDesired, uFlags);
      return m_hIcon;
   }
   HICON CreateIconIndirect(PICONINFO pIconInfo)
   {
      ATLASSERT(m_hIcon==NULL);
      ATLASSERT(pIconInfo);
      m_hIcon = ::CreateIconIndirect(pIconInfo);
      return m_hIcon;
   }
   HICON ExtractIcon(LPCTSTR lpszExeFileName, UINT nIconIndex)
   {
      ATLASSERT(m_hIcon==NULL);
      ATLASSERT(!::IsBadStringPtr(lpszExeFileName,-1));
#if (_ATL_VER >= 0x0700)      
      m_hIcon = ::ExtractIcon(ATL::_AtlBaseModule.GetModuleInstance(), lpszExeFileName, nIconIndex);
#else
      m_hIcon = ::ExtractIcon(_Module.GetModuleInstance(), lpszExeFileName, nIconIndex);
#endif
      return m_hIcon;
   }
   HICON ExtractAssociatedIcon(HINSTANCE hInst, LPCTSTR lpIconPath, LPWORD lpiIcon)
   {
      ATLASSERT(m_hIcon==NULL);
      ATLASSERT(!::IsBadStringPtr(lpIconPath,-1));
      ATLASSERT(lpiIcon);
      m_hIcon = ::ExtractAssociatedIcon(hInst, lpIconPath, lpiIcon);
      return m_hIcon;
   }

   // Operations

   BOOL DestroyIcon()
   {
      ATLASSERT(m_hIcon!=NULL);
      BOOL bRet = ::DestroyIcon(m_hIcon);
      if( bRet ) m_hIcon = NULL;
      return bRet;
   }
   HICON CopyIcon()
   {
      ATLASSERT(m_hIcon!=NULL);
      return ::CopyIcon(m_hIcon);
   }
   HICON DuplicateIcon()
   {
      ATLASSERT(m_hIcon!=NULL);
      return ::DuplicateIcon(NULL, m_hIcon);
   }

   BOOL DrawIcon(HDC hDC, int x, int y)
   {
      ATLASSERT(m_hIcon!=NULL);
      return ::DrawIcon(hDC, x, y, m_hIcon);
   }
   BOOL DrawIcon(HDC hDC, POINT pt)
   {
      ATLASSERT(m_hIcon!=NULL);
      return ::DrawIcon(hDC, pt.x, pt.y, m_hIcon);
   }
   BOOL DrawIconEx(HDC hDC, int x, int y, int cxWidth, int cyWidth, UINT uStepIfAniCur = 0, HBRUSH hbrFlickerFreeDraw = NULL, UINT uFlags = DI_NORMAL)
   {
      ATLASSERT(m_hIcon!=NULL);
      return ::DrawIconEx(hDC, x, y, m_hIcon, cxWidth, cyWidth, uStepIfAniCur, hbrFlickerFreeDraw, uFlags);
   }
   BOOL DrawIconEx(HDC hDC, POINT pt, SIZE size, UINT uStepIfAniCur = 0, HBRUSH hbrFlickerFreeDraw = NULL, UINT uFlags = DI_NORMAL)
   {
      ATLASSERT(m_hIcon!=NULL);
      return ::DrawIconEx(hDC, pt.x, pt.y, m_hIcon, size.cx, size.cy, uStepIfAniCur, hbrFlickerFreeDraw, uFlags);
   }

   BOOL GetIconInfo(PICONINFO pIconInfo)
   {
      ATLASSERT(m_hIcon!=NULL);
      ATLASSERT(pIconInfo);
      return ::GetIconInfo(m_hIcon, pIconInfo);
   }
};

typedef CIconT<true> CIcon;
typedef CIconT<false> CIconHandle;


/////////////////////////////////////////////////////////////////////////////
// CCursor

// Protect template against silly macro
#ifdef CopyCursor
   #undef CopyCursor
#endif

template< bool t_bManaged >
class CCursorT
{
public:
   HCURSOR m_hCursor;

   // Constructor/destructor/operators

   CCursorT(HCURSOR hCursor = NULL) : m_hCursor(hCursor)
   { 
   }

   ~CCursorT()
   {
      if( t_bManaged && m_hCursor != NULL ) ::DestroyCursor(m_hCursor);
   }

   CCursorT<t_bManaged>& operator=(HCURSOR hCursor)
   {
      m_hCursor = hCursor;
      return *this;
   }

   void Attach(HCURSOR hCursor)
   {
      if( t_bManaged && m_hCursor != NULL ) ::DestroyCursor(m_hCursor);
      m_hCursor = hCursor;
   }
   HCURSOR Detach()
   {
      HCURSOR hCursor = m_hCursor;
      m_hCursor = NULL;
      return hCursor;
   }

   operator HCURSOR() const { return m_hCursor; }

   bool IsNull() const { return m_hCursor == NULL; }

   // Create methods

   HCURSOR LoadCursor(_U_STRINGorID cursor)
   {
      ATLASSERT(m_hCursor==NULL);
#if (_ATL_VER >= 0x0700)
      m_hCursor = ::LoadCursor(ATL::_AtlBaseModule.GetResourceInstance(), cursor.m_lpstr);
#else
      m_hCursor = ::LoadCursor(_Module.GetResourceInstance(), cursor.m_lpstr);
#endif
      return m_hCursor;
   }
   HCURSOR LoadOEMCursor(UINT nIDCursor) // for IDC_ types
   {
      ATLASSERT(m_hCursor==NULL);
      m_hCursor = ::LoadCursor(NULL, MAKEINTRESOURCE(nIDCursor));
      return m_hCursor;
   }
   HICON LoadCursor(_U_STRINGorID cursor, int cxDesired, int cyDesired, UINT fuLoad = 0)
   {
      ATLASSERT(m_hCursor==NULL);
#if (_ATL_VER >= 0x0700)
      m_hCursor = (HCURSOR) ::LoadImage(ATL::_AtlBaseModule.GetResourceInstance(), cursor.m_lpstr, IMAGE_CURSOR, cxDesired, cyDesired, fuLoad);
#else
      m_hCursor = (HCURSOR) ::LoadImage(_Module.GetResourceInstance(), cursor.m_lpstr, IMAGE_CURSOR, cxDesired, cyDesired, fuLoad);
#endif
      return m_hCursor;
   }
   HCURSOR LoadCursorFromFile(LPCTSTR pstrFilename)
   {
      ATLASSERT(m_hCursor==NULL);
      ATLASSERT(!::IsBadStringPtr(pstrFilename,-1));
      m_hCursor = ::LoadCursorFromFile(pstrFilename);
      return m_hCursor;
   }
   HCURSOR CreateCursor(int xHotSpot, int yHotSpot, int nWidth, int nHeight, CONST VOID *pvANDPlane, CONST VOID *pvXORPlane)
   {
      ATLASSERT(m_hCursor==NULL);
#if (_ATL_VER >= 0x0700)
      m_hCursor = ::CreateCursor(ATL::_AtlBaseModule.GetResourceInstance(), xHotSpot, yHotSpot, nWidth, nHeight, pvANDPlane, pvXORPlane);
#else
      m_hCursor = ::CreateCursor(_Module.GetResourceInstance(), xHotSpot, yHotSpot, nWidth, nHeight, pvANDPlane, pvXORPlane);
#endif
      return m_hCursor;
   }
   HICON CreateCursorFromResource(PBYTE pBits, DWORD dwResSize, DWORD dwVersion = 0x00030000)
   {
      ATLASSERT(m_hIcon==NULL);
      ATLASSERT(pBits);
      m_hIcon = ::CreateIconFromResource(pBits, dwResSize, FALSE, dwVersion);
      return m_hIcon;
   }
   HICON CreateCursorFromResourceEx(PBYTE pbBits, DWORD cbBits, DWORD dwVersion = 0x00030000, int cxDesired = 0, int cyDesired = 0, UINT uFlags = LR_DEFAULTCOLOR)
   {
      ATLASSERT(m_hIcon==NULL);
      ATLASSERT(pbBits);
      ATLASSERT(cbBits>0);
      m_hIcon = ::CreateIconFromResourceEx(pbBits, cbBits, FALSE, dwVersion, cxDesired,  cyDesired, uFlags);
      return m_hIcon;
   }
  
   // Operations

   BOOL DestroyCursor()
   {
      ATLASSERT(m_hCursor!=NULL);
      BOOL bRet = ::DestroyCursor(m_hCursor);
      if( bRet ) m_hCursor = NULL;
      return bRet;
   }

   HCURSOR CopyCursor()
   {
      ATLASSERT(m_hCursor!=NULL);
      return (HCURSOR) ::CopyIcon( (HICON) m_hCursor );
   }

#if(WINVER >= 0x0500)
   BOOL GetCursorInfo(LPCURSORINFO pCursorInfo)
   {
      ATLASSERT(m_hCursor!=NULL);
      ATLASSERT(pCursorInfo);
      return ::GetCursorInfo(pCursorInfo);
   }
#endif
};

typedef CCursorT<true> CCursor;
typedef CCursorT<false> CCursorHandle;


/////////////////////////////////////////////////////////////////////////////
// CAccelerator

template< bool t_bManaged >
class CAcceleratorT
{
public:
   HACCEL m_hAccel;

   // Constructor/destructor/operators

   CAcceleratorT(HACCEL hAccel = NULL) : m_hAccel(hAccel)
   { 
   }

   ~CAcceleratorT()
   {
      if( t_bManaged && m_hAccel != NULL ) ::DestroyAcceleratorTable(m_hAccel);
   }

   CAcceleratorT<t_bManaged>& operator=(HACCEL hAccel)
   {
      m_hAccel = hAccel;
      return *this;
   }

   void DestroyObject()
   {
      if( m_hAccel != NULL ) {
         ::DestroyAcceleratorTable(m_hAccel);
         m_hAccel = NULL;
      }
   }

   void Attach(HACCEL hAccel)
   {
      if( t_bManaged && m_hAccel != NULL ) ::DestroyAcceleratorTable(m_hAccel);
      m_hAccel = hAccel;
   }  
   HCURSOR Detach()
   {
      HACCEL hAccel = m_hAccel;
      m_hAccel = NULL;
      return hAccel;
   }

   operator HACCEL() const { return m_hAccel; }

   bool IsNull() const { return m_hAccel == NULL; }

   // Create methods

   HACCEL LoadAccelerators(_U_STRINGorID accel)
   {
      ATLASSERT(m_hAccel==NULL);
#if (_ATL_VER >= 0x0700)
      m_hAccel = ::LoadAccelerators(ATL::_AtlBaseModule.GetResourceInstance(), accel.m_lpstr);
#else
      m_hAccel = ::LoadAccelerators(_Module.GetResourceInstance(), accel.m_lpstr);
#endif
      return m_hAccel;
   }
   HACCEL CreateAcceleratorTable(LPACCEL pAccel, int cEntries)
   {
      ATLASSERT(m_hAccel==NULL);
      ATLASSERT(!::IsBadReadPtr(lpAccelDst, sizeof(ACCEL)*cEntries));
      m_hAccel = ::CreateAcceleratorTable(pAccel, cEntries);
      return m_hAccel;
   }

   // Operations

   int CopyAcceleratorTable(LPACCEL lpAccelDst, int cEntries)
   {
      ATLASSERT(m_hAccel!=NULL);
      ATLASSERT(!::IsBadWritePtr(lpAccelDst, sizeof(ACCEL)*cEntries));
      return ::CopyAcceleratorTable(m_hAccel, lpAccelDst, cEntries);
   }

   BOOL TranslateAccelerator(HWND hWnd, LPMSG pMsg)
   {
      ATLASSERT(m_hAccel!=NULL);
      ATLASSERT(::IsWindow(hWnd));
      ATLASSERT(pMsg);
      return ::TranslateAccelerator(hWnd, m_hAccel, pMsg);
   }
};

typedef CAcceleratorT<true> CAccelerator;
typedef CAcceleratorT<false> CAcceleratorHandle;


/////////////////////////////////////////////////////////////////////////////
// CLogFont

class CLogFont : public LOGFONT
{
public:
   CLogFont() 
   { 
      ::ZeroMemory( (LOGFONT*) this, sizeof(LOGFONT) );
   }
   CLogFont(const LOGFONT& lf) 
   { 
      Copy(&lf);
   }
   CLogFont(HFONT hFont)
   {
      ATLASSERT(::GetObjectType(hFont)==OBJ_FONT);
      ::GetObject(hFont, sizeof(LOGFONT), (LOGFONT*) this);
   }
   HFONT CreateFontIndirect() 
   { 
      return ::CreateFontIndirect(this); 
   }
   void SetBold() 
   { 
      lfWeight = FW_BOLD; 
   }
   BOOL IsBold() const 
   { 
      return lfWeight >= FW_BOLD; 
   }
   void MakeBolder(int iScale = 1)
   {
      lfWeight += FW_BOLD * iScale;
   }
   void MakeLarger(int iScale)
   {
      if( lfHeight > 0 ) lfHeight += iScale; else lfHeight -= iScale;
   }
   void SetHeight(long PointSize, HDC hDC = NULL) 
   { 
      // For MM_TEXT mapping mode...
      // NOTE: MulDiv() gives correct rounding.
      lfHeight = -MulDiv(PointSize, ::GetDeviceCaps(hDC, LOGPIXELSY), 72); 
   }
   long GetHeight(HDC hDC = NULL) const
   {
      // For MM_TEXT mapping mode...
      // NOTE: MulDiv() gives correct rounding.
      return MulDiv(-lfHeight, 72, ::GetDeviceCaps(hDC, LOGPIXELSY));
   }
   long GetDeciPointHeight(HDC hDC = NULL)
   {
      POINT ptOrg = { 0, 0 };
      ::DPtoLP(hDC, &ptOrg, 1);
      POINT pt = { 0, 0 };
      pt.y = abs(lfHeight) + ptOrg.y;
      ::LPtoDP(hDC,&pt,1);
      return MulDiv(pt.y, 720, ::GetDeviceCaps(hDC,LOGPIXELSY)); // 72 points/inch, 10 decipoints/point
   }
   void SetHeightFromDeciPoint(long DeciPtHeight, HDC hDC = NULL)
   {
      POINT pt;
      pt.y = MulDiv(::GetDeviceCaps(hDC, LOGPIXELSY), DeciPtHeight, 720); // 72 points/inch, 10 decipoints/point
      ::DPtoLP(hDC, &pt, 1);
      POINT ptOrg = { 0, 0 };
      ::DPtoLP(hDC, &ptOrg, 1);
      lfHeight = -abs(pt.y - ptOrg.y);
   }
   void SetCaptionFont()
   {
      NONCLIENTMETRICS ncm = { 0 };
      ncm.cbSize = sizeof(ncm);
      ::SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
      Copy(&ncm.lfCaptionFont);
   }
   void SetMenuFont()
   {
      NONCLIENTMETRICS ncm = { 0 };
      ncm.cbSize = sizeof(ncm);
      ::SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
      Copy(&ncm.lfMenuFont);
   }
   void SetStatusFont()
   {
      NONCLIENTMETRICS ncm = { 0 };
      ncm.cbSize = sizeof(ncm);
      ::SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
      Copy(&ncm.lfStatusFont);
   }
   void SetMessageBoxFont()
   {
      NONCLIENTMETRICS ncm = { 0 };
      ncm.cbSize = sizeof(ncm);
      ::SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
      Copy(&ncm.lfMessageFont);
   }
   void Copy(const LOGFONT* lf)
   {
      ATLASSERT(lf);
      ::CopyMemory( (LOGFONT*) this, lf, sizeof(LOGFONT) );
   }
   CLogFont& operator=(const CLogFont& src)
   {
      Copy(&src);
      return *this;
   }
   CLogFont& operator=(const LOGFONT& src)
   {
      Copy(&src);
      return *this;
   }
   CLogFont& operator=(HFONT hFont)
   {
      ATLASSERT(::GetObjectType(hFont)==OBJ_FONT);
      ::GetObject(hFont, sizeof(LOGFONT), (LOGFONT*) this);
      return *this;
   }
   bool operator==(const LOGFONT& logfont) const
   {
      return( logfont.lfHeight == lfHeight &&
         logfont.lfWidth == lfWidth &&
         logfont.lfEscapement == lfEscapement &&
         logfont.lfOrientation == lfOrientation &&
         logfont.lfWeight == lfWeight &&
         logfont.lfItalic == lfItalic &&
         logfont.lfUnderline == lfUnderline &&
         logfont.lfStrikeOut == lfStrikeOut &&
         logfont.lfCharSet == lfCharSet &&
         logfont.lfOutPrecision == lfOutPrecision &&
         logfont.lfClipPrecision == lfClipPrecision &&
         logfont.lfQuality == lfQuality &&
         logfont.lfPitchAndFamily == lfPitchAndFamily &&
         ::lstrcmp(logfont.lfFaceName, lfFaceName) == 0 );
   }
};

#endif // _WTL_VER


/////////////////////////////////////////////////////////////////////////////
// CMemDC

class CMemDC : public CDC
{
public:
   CDCHandle     m_dc;          // Owner DC
   CBitmap       m_bitmap;      // Offscreen bitmap
   CBitmapHandle m_hOldBitmap;  // Originally selected bitmap
   RECT          m_rc;          // Rectangle of drawing area

   CMemDC(HDC hDC, LPRECT pRect = NULL)
   {
      ATLASSERT(hDC!=NULL);
      m_dc = hDC;
      if( pRect != NULL ) m_rc = *pRect; else m_dc.GetClipBox(&m_rc);

      CreateCompatibleDC(m_dc);
      ::LPtoDP(m_dc, (LPPOINT) &m_rc, sizeof(RECT) / sizeof(POINT));
      m_bitmap.CreateCompatibleBitmap(m_dc, m_rc.right - m_rc.left, m_rc.bottom - m_rc.top);
      m_hOldBitmap = SelectBitmap(m_bitmap);
      ::DPtoLP(m_dc, (LPPOINT) &m_rc, sizeof(RECT) / sizeof(POINT));
      SetWindowOrg(m_rc.left, m_rc.top);
   }
   ~CMemDC()
   {
      // Copy the offscreen bitmap onto the screen.
      m_dc.BitBlt(m_rc.left, m_rc.top, m_rc.right - m_rc.left, m_rc.bottom - m_rc.top,
                  m_hDC, m_rc.left, m_rc.top, SRCCOPY);
      // Swap back the original bitmap.
      SelectBitmap(m_hOldBitmap);
   }
};


/////////////////////////////////////////////////////////////////////////////
// COffscreenDraw

// To use it, derive from it and chain it in the message map.
template< class T >
class COffscreenDraw
{
public:
   BEGIN_MSG_MAP(COffscreenDraw)
      MESSAGE_HANDLER(WM_PAINT, OnPaint)
      MESSAGE_HANDLER(WM_PRINTCLIENT, OnPaint)
      MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBackground)
   END_MSG_MAP()

   LRESULT OnPaint(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/)
   {
      T* pT = static_cast<T*>(this);
      if( wParam != NULL )
      {
         CMemDC memdc( (HDC) wParam, NULL );
         pT->DoPaint(memdc.m_hDC);
      }
      else
      {
         RECT rc;
         pT->GetClientRect(&rc);
         CPaintDC dc(pT->m_hWnd);
         CMemDC memdc(dc.m_hDC, &rc);
         pT->DoPaint(memdc.m_hDC);
      }
      return 0;
   }
   LRESULT OnEraseBackground(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
   {
      return 1; // handled; no need to erase background; do it in DoPaint();
   }
   void DoPaint(CDCHandle dc)
   {
      ATLASSERT(false); // must override this
   }
};

// To use it, derive from it and chain it in the message map.
template< class T >
class COffscreenDrawRect
{
public:
   BEGIN_MSG_MAP(COffscreenDrawRect)
      MESSAGE_HANDLER(WM_PAINT, OnPaint)
      MESSAGE_HANDLER(WM_PRINTCLIENT, OnPaint)
      MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBackground)
   END_MSG_MAP()

   LRESULT OnPaint(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/)
   {
      T* pT = static_cast<T*>(this);
      if( wParam != NULL )
      {
         CMemDC memdc( (HDC) wParam, NULL );
         pT->DoPaint(memdc.m_hDC, memdc.m_rc);
      }
      else
      {
         CPaintDC dc(pT->m_hWnd);
         CMemDC memdc(dc.m_hDC, &dc.m_ps.rcPaint);
         pT->DoPaint(memdc.m_hDC, dc.m_ps.rcPaint);
      }
      return 0;
   }
   LRESULT OnEraseBackground(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
   {
      return 1; // handled; no need to erase background; do it in DoPaint();
   }
   void DoPaint(CDCHandle dc, RECT& rcClip)
   {
      ATLASSERT(false); // must override this
   }
};


/////////////////////////////////////////////////////////////////////////////
// CSaveDC

class CSaveDC
{
public:
   HDC m_hDC;
   int m_iState;

   CSaveDC(HDC hDC) : m_hDC(hDC)
   {
      ATLASSERT(::GetObjectType(m_hDC)==OBJ_DC || ::GetObjectType(m_hDC)==OBJ_MEMDC);
      m_iState = ::SaveDC(hDC);
      ATLASSERT(m_iState!=0);
   }
   ~CSaveDC()
   {
      Restore();
   }
   void Restore()
   {
      if( m_iState == 0 ) return;
      ATLASSERT(::GetObjectType(m_hDC)==OBJ_DC || ::GetObjectType(m_hDC)==OBJ_MEMDC);
      ::RestoreDC(m_hDC, m_iState);
      m_iState = 0;
   }
};


/////////////////////////////////////////////////////////////////////////////
// CHandle

#if (_ATL_VER < 0x0700)

class CHandle
{
public:
   HANDLE m_h;

   CHandle(HANDLE hSrc = INVALID_HANDLE_VALUE) : m_h(hSrc)
   { }

   ~CHandle()
   {
      Close();
   }

   operator HANDLE() const { return m_h; };
  
   LPHANDLE operator&()
   {
      ATLASSERT(!IsValid());
      return &m_h;
   }

   CHandle& operator=(HANDLE h)
   {
      ATLASSERT(!IsValid());
      m_h = h;
      return *this;
   }

   bool IsValid() const { return m_h != INVALID_HANDLE_VALUE; };
   
   void Attach(HANDLE h)
   {
      if( IsValid() ) ::CloseHandle(m_h);
      m_h = h;
   }   
   HANDLE Detach()
   {
      HANDLE h = m_h;
      m_h = INVALID_HANDLE_VALUE;
      return h;
   }
   
   BOOL Close()
   {
      BOOL bRes = FALSE;
      if( m_h != INVALID_HANDLE_VALUE ) {
         bRes = ::CloseHandle(m_h);
         m_h = INVALID_HANDLE_VALUE;
      }
      return bRes;
   }

   BOOL Duplicate(HANDLE hSource, bool bInherit = false)
   {
      ATLASSERT(!IsValid());
      HANDLE hOurProcess = ::GetCurrentProcess();
      BOOL b = ::DuplicateHandle(hOurProcess, 
         hSource,
         hOurProcess, 
         &m_h,
         DUPLICATE_SAME_ACCESS,
         bInherit,
         DUPLICATE_SAME_ACCESS);
      ATLASSERT(b);
      return b;
   }
};

#endif // _ATL_VER


/////////////////////////////////////////////////////////////////////////////
// Mouse Hover helper

#ifndef NOTRACKMOUSEEVENT

#ifndef WM_MOUSEENTER
   #define WM_MOUSEENTER WM_USER + 253
#endif // WM_MOUSEENTER

// To use it, derive from it and chain it in the message map.
// Make sure to set bHandled to FALSE when handling WM_MOUSEMOVE or
// the WM_MOUSELEAVE message!
template< class T >
class CMouseHover
{
public:   
   bool m_fMouseOver;          // Internal mouse-over state
   bool m_fMouseForceUpdate;   // Update window immediately on event

   CMouseHover() : 
      m_fMouseOver(false),
      m_fMouseForceUpdate(true)
   {
   }

   void SetForceMouseOverUpdate(bool bForce = false)
   {
      m_fMouseForceUpdate = bForce;
   }

   BEGIN_MSG_MAP(CMouseHover)
      MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
      MESSAGE_HANDLER(WM_MOUSELEAVE, OnMouseLeave)
   END_MSG_MAP()

   LRESULT OnMouseMove(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
   {
      T* pT = static_cast<T*>(this);
      if( !m_fMouseOver )   {
         m_fMouseOver = true;
         pT->SendMessage(WM_MOUSEENTER, wParam, lParam);         
         if( m_fMouseForceUpdate ) {
            pT->Invalidate();
            pT->UpdateWindow();
         }
         _StartTrackMouseLeave(pT->m_hWnd);
      }
      bHandled = FALSE;
      return 0;
   }
   LRESULT OnMouseLeave(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
   {
      T* pT = static_cast<T*>(this);
      if( m_fMouseOver ) {
         m_fMouseOver = false;         
         if( m_fMouseForceUpdate ) {
            pT->Invalidate();
            pT->UpdateWindow();
         }
      }
      bHandled = FALSE;
      return 0;
   }
   BOOL _StartTrackMouseLeave(HWND hWnd) const
   {
      ATLASSERT(::IsWindow(hWnd));
      TRACKMOUSEEVENT tme = { 0 };
      tme.cbSize = sizeof(tme);
      tme.dwFlags = TME_LEAVE;
      tme.hwndTrack = hWnd;
      return _TrackMouseEvent(&tme);
   }
   BOOL _CancelTrackMouseLeave(HWND hWnd) const
   {
      TRACKMOUSEEVENT tme = { 0 };
      tme.cbSize = sizeof(tme);
      tme.dwFlags = TME_LEAVE | TME_CANCEL;
      tme.hwndTrack = hWnd;
      return _TrackMouseEvent(&tme);
   }
};

#endif // NOTRACKMOUSEEVENT


}; // namespace WTL

#endif // __ATLGDIX_H__
