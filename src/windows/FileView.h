// HtmlView.h : interface of the CHtmlView class
//
/////////////////////////////////////////////////////////////////////////////

#if !defined(AFX_FileView_H__053AD676_0AE2_11D6_8BF1_00500477589F__INCLUDED_)
#define AFX_FileView_H__053AD676_0AE2_11D6_8BF1_00500477589F__INCLUDED_

#pragma once

#include "VGMItem.h"
#include "RawFile.h"
#include "VGMTransWindow.h"
#include "DataSeg.h"

#define COL_BLOCK_SIZE 0x100
#define COL_BLOCKDEST_SIZE (COL_BLOCK_SIZE * 1.2 + 12)

/*
#define TEXT_CLR_BLACK 0
#define TEXT_CLR_RED 0x20
#define TEXT_CLR_PURPLE 0x40
#define TEXT_CLR_BLUE 0x60

#define BG_CLR_WHITE 0
#define BG_CLR_RED 1
#define BG_CLR_GREEN 2
#define BG_CLR_BLUE 3
#define BG_CLR_YELLOW 4
#define BG_CLR_MAGENTA 5
#define BG_CLR_CYAN 6
#define BG_CLR_DARKRED 7
#define BG_CLR_BLACK 8
#define BG_CLR_DARKBLUE 9
#define BG_CLR_STEEL 10
#define BG_CLR_CHEDDAR 11
#define BG_CLR_PINK 12
#define BG_CLR_LIGHTBLUE 13
#define BG_CLR_AQUA 14
#define BG_CLR_PEACH 15
#define BG_CLR_WHEAT 16

#define CLR_UNKNOWN (BG_CLR_BLACK|TEXT_CLR_RED)
#define CLR_NOTEON BG_CLR_BLUE
#define CLR_NOTEOFF BG_CLR_DARKBLUE
#define CLR_PROGRAMCHANGE BG_CLR_MAGENTA
#define CLR_SETVOLUME BG_CLR_PEACH
#define CLR_SETEXPRESSION BG_CLR_PINK
#define CLR_TEMPO BG_CLR_AQUA
*/

#define DRAWMODE_OFFSETS 1
#define DRAWMODE_HEX 2
#define DRAWMODE_ASCII 4

#define CHAR_WIDTH 8
#define CHAR_HEIGHT 16


class CFileView:
	public CScrollWindowImpl<CFileView>,
	public VGMTransWindow<CFileView>
{
public:
	DECLARE_WND_CLASS(_T("FileView"))

	BOOL PreTranslateMessage(MSG* pMsg)
	{
		if((pMsg->message < WM_KEYFIRST || pMsg->message > WM_KEYLAST) &&
		   (pMsg->message < WM_MOUSEFIRST || pMsg->message > WM_MOUSELAST))
			return FALSE;

		// give HTML page a chance to translate this message
		return (BOOL)SendMessage(WM_FORWARDMSG, 0, (LPARAM)pMsg);
	}

	BEGIN_MSG_MAP(CFileView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)
		//MESSAGE_HANDLER(WM_PAINT, CScrollImpl<CFileView>::OnPaint)
		MSG_WM_LBUTTONDOWN(OnLButtonDown)
		MSG_WM_SIZE(OnSize);
		MSG_WM_SIZING(OnSizing);
		MESSAGE_HANDLER(WM_RBUTTONUP, OnRButtonUp)
		MSG_WM_CONTEXTMENU(OnContextMenu)
		MSG_WM_KEYDOWN(OnKeyDown)
		CHAIN_MSG_MAP(CScrollWindowImpl<CFileView>)
		//MSG_WM_PAINT(OnPaint)
		DEFAULT_REFLECTION_HANDLER()
		MSG_WM_ERASEBKGND(OnEraseBkgnd)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

public:
	CFileView();
	LRESULT OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
	void InitTextColors(void);
	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled);
	LRESULT OnSetFocus(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled);
	LRESULT OnEraseBkgnd ( HDC hdc );
	LRESULT OnContextMenu(HWND hwndCtrl, CPoint ptClick );
	void OnLButtonDown(UINT nFlags, CPoint point);
	LRESULT OnRButtonUp(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
	void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
	virtual void DoPaint(CDCHandle dc);
	void WriteLine(UINT nLine, CDCHandle& dc);
	void OnSize(UINT nType, CSize size);
	void OnSizing(UINT nSide, LPRECT lpRect);
	void SelectItem(VGMItem* newItem);
	void InvalidateFromOffsets(UINT first, UINT last);
	int ScrollToByte(DWORD byte);
	inline UINT GetLeftMostOffset();
	inline UINT GetRightMostOffset();

	VGMFile* GetCurFile();
	void SetCurFile(VGMFile* file);
	void SetOffsetRange(ULONG newBeginOffset, ULONG newEndOffset);
	void SetupColor();
	BYTE GetItemColor(VGMItem* item);
	//void UpdateColorBuffer(ULONG offset);

protected:
	//RawFile* curFile;
	VGMFile* curFile;

	UINT m_cxWidth;
	UINT m_cxOffset;
	UINT m_nLinesPerPage;
	UINT m_nLinesTotal;

	BYTE ucDrawMode;
	VGMItem *SelectedItem;
	ULONG curOffset;

	ULONG beginOffset;
	ULONG endOffset;

	UINT m_cxScreen;
	UINT m_cyScreen;
	CFont m_fontScreen;
	COLORREF TextClr[4];
	COLORREF BGTextClr[32];

	CompDataSeg col;

	//vector< pair<BYTE*, long> > col;
	//BYTE colBuf[COL_BLOCK_SIZE];
	//int colBufOffset;
};

extern CFileView fileView;


/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_HtmlView_H__053AD676_0AE2_11D6_8BF1_00500477589F__INCLUDED_)
