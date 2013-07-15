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

typedef enum HexTextColors {
	TXT_CLR_BLACK = 0x00,
	TXT_CLR_WHITE = 0x20,
	TXT_CLR_RED =   0x40,
	TXT_CLR_GREY =  0x60
};


typedef enum HexBgColors {
	BG_CLR_WHITE,
	BG_CLR_BLACK,
	BG_CLR_RED,
	BG_CLR_GREY,
	BG_CLR_LIGHTGREY,

	BG_CLR_RED1,
	BG_CLR_RED2,
	BG_CLR_ORANGE1,
	BG_CLR_ORANGE2,
	BG_CLR_ORANGE3,
	BG_CLR_ORANGE4,
	BG_CLR_YELLOW1,
	BG_CLR_YELLOW2,
	BG_CLR_YELLOW3,
	BG_CLR_GREEN1,
	BG_CLR_GREEN2,
	BG_CLR_GREEN3,

	BG_CLR_GREEN4,
	BG_CLR_AQUA1,
	BG_CLR_AQUA2,
	BG_CLR_LIGHTBLUE1,

	BG_CLR_LIGHTBLUE2,
	BG_CLR_BLUE1,
	BG_CLR_BLUE2,
	BG_CLR_PURPLE1,

	BG_CLR_PURPLE2,
	BG_CLR_MAGENTA1,
	BG_CLR_MAGENTA2,
	BG_CLR_PINK,

};

const BYTE ColorTranslation[] = 
{
	BG_CLR_BLACK		| TXT_CLR_GREY,	//CLR_UNKNOWN
	BG_CLR_RED			| TXT_CLR_BLACK,	//CLR_UNRECOGNIZED
	BG_CLR_LIGHTGREY	| TXT_CLR_BLACK,	//CLR_HEADER
	BG_CLR_GREY			| TXT_CLR_BLACK,	//CLR_MISC
	BG_CLR_GREY			| TXT_CLR_BLACK,	//CLR_MARKER
	BG_CLR_GREEN1		| TXT_CLR_BLACK,	//CLR_TIMESIG
	BG_CLR_GREEN2		| TXT_CLR_BLACK,	//CLR_TEMPO
	BG_CLR_PINK			| TXT_CLR_BLACK,	//CLR_PROGCHANGE
	BG_CLR_MAGENTA1		| TXT_CLR_BLACK,	//CLR_TRANSPOSE
	BG_CLR_MAGENTA2		| TXT_CLR_BLACK,	//CLR_PRIORITY
	BG_CLR_RED2			| TXT_CLR_BLACK,	//CLR_VOLUME
	BG_CLR_ORANGE1		| TXT_CLR_BLACK,	//CLR_EXPRESSION
	BG_CLR_ORANGE3		| TXT_CLR_BLACK,	//CLR_PAN
	BG_CLR_BLUE1		| TXT_CLR_BLACK,	//CLR_NOTEON
	BG_CLR_BLUE2		| TXT_CLR_BLACK,	//CLR_NOTEOFF
	BG_CLR_BLUE1		| TXT_CLR_BLACK,	//CLR_DURNOTE
	BG_CLR_LIGHTBLUE2	| TXT_CLR_BLACK,	//CLR_TIE
	BG_CLR_LIGHTBLUE1	| TXT_CLR_BLACK,	//CLR_REST
	BG_CLR_MAGENTA1		| TXT_CLR_BLACK,	//CLR_PITCHBEND
	BG_CLR_MAGENTA2		| TXT_CLR_BLACK,	//CLR_PITCHBENDRANGE
	BG_CLR_YELLOW1		| TXT_CLR_BLACK,	//CLR_MODULATION
	BG_CLR_MAGENTA1		| TXT_CLR_BLACK,	//CLR_PORTAMENTO
	BG_CLR_MAGENTA2		| TXT_CLR_BLACK,	//CLR_PORTAMENTOTIME
	BG_CLR_GREY			| TXT_CLR_BLACK,	//CLR_CHANGESTATE
	BG_CLR_GREY			| TXT_CLR_BLACK,	//CLR_ADSR
	BG_CLR_GREY			| TXT_CLR_BLACK,	//CLR_LFO
	BG_CLR_GREY			| TXT_CLR_BLACK,	//CLR_REVERB
	BG_CLR_YELLOW3		| TXT_CLR_BLACK,	//CLR_SUSTAIN
	BG_CLR_YELLOW3		| TXT_CLR_BLACK,	//CLR_LOOP
	BG_CLR_YELLOW2		| TXT_CLR_BLACK,	//CLR_LOOPFOREVER
	BG_CLR_RED			| TXT_CLR_BLACK,	//CLR_TRACKEND
};

const COLORREF TextClr[4] =
{
	RGB(0, 0, 0),				//Black
	RGB(255, 255, 255),			//White
	RGB(255, 0, 0),				//Red
	RGB(192, 192, 192),			//Grey
};
const COLORREF BGTextClr[32] =
{
	RGB(0xFF, 0xFF, 0xFF),				// White			- Black
	RGB(0x00, 0x00, 0x00),				// Black			- White
	RGB(0xFF, 0x00, 0x00),				// Red				- White
	RGB(0x95, 0x95, 0x95),				// Grey				- Black
	RGB(0xC0, 0xC0, 0xC0),				// Light Grey
	
	RGB(0xFE, 0x42, 0x42),					// Red1 - White
	RGB(0xFE, 0x5B, 0x42),					// Red2  - Black
	RGB(0xFE, 0x77, 0x42),					// Orange1 - Black
	RGB(0xFE, 0x94, 0x42),					// Orange2	 - Black
	RGB(0xFE, 0xAD, 0x42),					// Orange3 - Black
	RGB(0xFE, 0xC3, 0x42),					// Orange4 - Black

	RGB(0xFF, 0xDA, 0x42),					// Yellow1
	RGB(0xFF, 0xED, 0x42),					// Yellow2
	RGB(0xFA, 0xFF, 0x42),					// Yellow3
	RGB(0xB8, 0xFF, 0x42),					// Green1
	RGB(0x73, 0xFF, 0x42),					// Green2
	RGB(0x42, 0xFF, 0x4E),					// Green3

	RGB(0x42, 0xFF, 0x7A),					// Green4
	RGB(0x42, 0xFF, 0xB0),					// Aqua1
	RGB(0x42, 0xFF, 0xE2),					// Aqua2
	RGB(0x42, 0xE8, 0xFF),					// Light Blue1
	RGB(0x42, 0xB3, 0xFF),					// Light Blue2
	RGB(0x42, 0x84, 0xFF),					// Blue1

	RGB(0x42, 0x51, 0xFF),					// Blue2 - White
	RGB(0x68, 0x42, 0xFF),					// Purple1 - White
	RGB(0x68, 0x42, 0xFF),					// Purple2 - White
	RGB(0xC9, 0x42, 0xFF),					// Magenta1 - Black
	RGB(0xFF, 0x42, 0xFE),					// Magenta2 - Black
	RGB(0xFF, 0x42, 0x9D)					// Pink1 - Black

	

	//43BEFF						// Light Blue - Black
	//435FFF
	//FF4356
	//FF7243
	//FFFB43
	//86FF43


	//RGB(0x91, 0x43, 0xFF),				// Purple			- White
	//RGB(0xD0, 0x43, 0xFF),				// Purple			- Black
	//RGB(0x43, 0xD4, 0xFF),				// Light Blue		- Black
	//RGB(0x43, 0x8B, 0xFF),				// Blue				- Black
	////RGB(0x43, 0x48, 0xFF),			// Darker Blue		- White
	//

	//RGB(0xF2, 0x43, 0xFF),				// Hot Pink			- Black
	//RGB(0xFF, 0x43, 0x91),				// Very Hot Pink	- Black
	//RGB(0x43, 0x73, 0xFF),				// Blue				- White
	//RGB(0x5F, 0x43, 0xFF),				// Purple			- White
	////RGB(0xA4, 0x43, 0xFF),			// Light Purple		- White
	//

	//RGB(0xFF, 0xAA, 0x43),				// Orange			- Black
	//RGB(0xFF, 0xC8, 0x43),				// Light Orange		- Black
	//RGB(0xFF, 0x43, 0x7F),				// Pink				- Black
	//RGB(0xFF, 0x59, 0x43),				// Orangish Red		- Black
	////RGB(0xFF, 0x80, 0x43),			// Reddish Orange	- Black
	//

	//RGB(0xFF, 0x67, 0x43),				// Orange			- Black
	//RGB(0xFF, 0x91, 0x43),				// Orange			- Black
	////RGB(0xFF, 0xB3, 0x43),			// Light Orange		- Black
	//RGB(0xFF, 0xD6, 0x43),				// Yellowish Orange - Black
	//RGB(0xFF, 0xF0, 0x43),				// Yellowish Orange - Black

	//RGB(0x43, 0xFF, 0x75),				// Sea Foamy		- Black
	//RGB(0x43, 0xFF, 0xBB),				// Sea Foamy		- Black
	//RGB(0xFF, 0xF3, 0x43),				// Yellow			- Black
	//RGB(0xC6, 0xFF, 0x43),				// Yellow/Green		- Black
	////RGB(0x68, 0xFF, 0x43),			// Bright Green		- Black
	//
	//RGB(0x43, 0xFF, 0xDC),				// Light Blue		- Black
	//RGB(0x43, 0xDE, 0xFF),				// Light Blue		- Black
	//RGB(0xA3, 0xFF, 0x43),				// Light Green		- Black
	//RGB(0x43, 0xFF, 0x45)				// Bright Green		- Black
	//RGB(0x43, 0xFF, 0x8A),			// Sea Foam			- Black
	

};


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

class CHexViewFrame;


class CHexView:
	public CScrollWindowImpl<CHexView>,
	public VGMTransWindow<CHexView>
{
public:
	DECLARE_WND_CLASS(_T("HexView"))

	BOOL PreTranslateMessage(MSG* pMsg)
	{
		if((pMsg->message < WM_KEYFIRST || pMsg->message > WM_KEYLAST) &&
		   (pMsg->message < WM_MOUSEFIRST || pMsg->message > WM_MOUSELAST))
			return FALSE;

		// give HTML page a chance to translate this message
		return (BOOL)SendMessage(WM_FORWARDMSG, 0, (LPARAM)pMsg);
	}

	BEGIN_MSG_MAP(CHexView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)
		//MESSAGE_HANDLER(WM_PAINT, CScrollImpl<CHexView>::OnPaint)
		MSG_WM_LBUTTONDOWN(OnLButtonDown)
	//	MSG_WM_SIZE(OnSize);
	//	MSG_WM_SIZING(OnSizing);
		MESSAGE_HANDLER(WM_RBUTTONUP, OnRButtonUp)
		MSG_WM_CONTEXTMENU(OnContextMenu)
		MSG_WM_KEYDOWN(OnKeyDown)
		CHAIN_MSG_MAP(CScrollWindowImpl<CHexView>)
		//MSG_WM_PAINT(OnPaint)
		DEFAULT_REFLECTION_HANDLER()
		MSG_WM_ERASEBKGND(OnEraseBkgnd)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

public:
	CHexView(CHexViewFrame* parFrame);
	LRESULT OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled);
	LRESULT OnSetFocus(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled);
	LRESULT OnEraseBkgnd ( HDC hdc );
	LRESULT OnContextMenu(HWND hwndCtrl, CPoint ptClick );
	void OnLButtonDown(UINT nFlags, CPoint point);
	LRESULT OnRButtonUp(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
	void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
	virtual void DoPaint(CDCHandle dc);
	void DrawBox(CDCHandle& dc, int x1, int y1, int x2, int y2);
	void DrawComplexOutline(CDCHandle& dc, int leftEdge, int rightEdge, int x1, int y1, int x2, int y2);
	void WriteLine(UINT nLine, CDCHandle& dc);
	void OnSize(UINT nType, CSize size);
	void OnSizing(UINT nSide, LPRECT lpRect);
	void SelectItem(VGMItem* newItem);
	void InvalidateFromOffsets(UINT first, UINT last);
	int ScrollToByte(DWORD byte);
	inline int GetHexLeftEdge();
	inline int GetHexRightEdge();
	inline int GetAsciiLeftEdge();
	inline int GetAsciiRightEdge();

	VGMFile* GetCurFile();
	void SetCurFile(VGMFile* file);
	void SetOffsetRange(ULONG newBeginOffset, ULONG newEndOffset);
	void SetupScrollInfo();
	void SetupColor();
	BYTE GetItemColor(VGMItem* item);
	//void UpdateColorBuffer(ULONG offset);

protected:
	//RawFile* curFile;
	VGMFile* curFile;
	CHexViewFrame* parFrame;

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
	

	//DataSeg col;
	BYTE* col;

	//vector< pair<BYTE*, long> > col;
	//BYTE colBuf[COL_BLOCK_SIZE];
	//int colBufOffset;
};

//extern CHexView fileView;


/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_HtmlView_H__053AD676_0AE2_11D6_8BF1_00500477589F__INCLUDED_)
