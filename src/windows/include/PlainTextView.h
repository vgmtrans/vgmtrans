// PlainTextView.h : interface of the CPlainTextView class
//
/////////////////////////////////////////////////////////////////////////////

#if !defined(AFX_PlainTextView_H__053AD676_0AE2_11D6_8BF1_00500477589F__INCLUDED_)
#define AFX_PlainTextView_H__053AD676_0AE2_11D6_8BF1_00500477589F__INCLUDED_

#pragma once

typedef CWinTraits<WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_HSCROLL | WS_VSCROLL | ES_AUTOHSCROLL | ES_AUTOVSCROLL | ES_MULTILINE | ES_NOHIDESEL, WS_EX_CLIENTEDGE> CPlainTextViewWinTraits;

class CPlainTextView:
	public CWindowImpl<CPlainTextView, CEdit, CPlainTextViewWinTraits>,
	public CEditCommands<CPlainTextView>
{
protected:
	typedef CPlainTextView thisClass;
	typedef CWindowImpl<CPlainTextView, CEdit, CPlainTextViewWinTraits> baseClass;

protected:
	HACCEL m_hAccel;
	WTL::CFont m_font;

public:
	CPlainTextView() : 
		m_hAccel(NULL)
	{
	}

public:
	DECLARE_WND_SUPERCLASS(NULL, CEdit::GetWndClassName())

	BOOL PreTranslateMessage(MSG* pMsg)
	{
		if(pMsg)
		{
			if((pMsg->hwnd == m_hWnd) || ::IsChild(m_hWnd, pMsg->hwnd))
			{
				// We'll have the Accelerator send the WM_COMMAND to our view
				if(m_hAccel != NULL && ::TranslateAccelerator(m_hWnd, m_hAccel, pMsg))
				{
					return TRUE;
				}
			}
		}
		return FALSE;
	}

	BEGIN_MSG_MAP(thisClass)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		if(uMsg == WM_FORWARDMSG)
			if(PreTranslateMessage((LPMSG)lParam)) return TRUE;
		CHAIN_MSG_MAP_ALT(CEditCommands<CPlainTextView>, 1)

		DEFAULT_REFLECTION_HANDLER()
	END_MSG_MAP()

	LRESULT OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	{
		// "base::OnCreate()"
		LRESULT lRet = DefWindowProc(uMsg, wParam, lParam);
		bHandled = TRUE;

		// "OnInitialUpdate"
		this->InitializeFont();
		this->CreateAccelerators();

		return lRet;
	}

	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
	{
		this->DestroyAccelerators();

		bHandled = FALSE;
		return 0;
	}

// Helpers
protected:

	void InitializeFont(void)
	{
		// Set the font of the edit control.
		CLogFont logFont;
		CClientDC dc(m_hWnd);
		logFont.SetHeight(8, dc);
		::lstrcpy(logFont.lfFaceName, _T("Courier New"));

		m_font.Attach(logFont.CreateFontIndirect());
		this->SetFont(m_font);

		// Set the tab stops to 4 characters.
		// Tab stops are in dialog units.
		TEXTMETRIC tm = {0};
		CFontHandle oldFont = dc.SelectFont(m_font);
		dc.GetTextMetrics(&tm);
		dc.SelectFont(oldFont);

		// Tab stops are in dialog units. We'll use a 4 character tab stop
		int dialogUnitsX = ::MulDiv(4, tm.tmAveCharWidth, LOWORD(GetDialogBaseUnits()));
		int tabStops = 4*dialogUnitsX;

		this->SetTabStops(tabStops);
	}


	void CreateAccelerators(void)
	{
		const int cAccel = 13;
		ACCEL AccelTable[cAccel] = {
			{FVIRTKEY | FCONTROL | FNOINVERT,  'A',        ID_EDIT_SELECT_ALL},
			{FVIRTKEY | FCONTROL | FNOINVERT,  'X',        ID_EDIT_CUT},
			{FVIRTKEY | FCONTROL | FNOINVERT,  'C',        ID_EDIT_COPY},
			{FVIRTKEY | FCONTROL | FNOINVERT,  'V',        ID_EDIT_PASTE},
			{FVIRTKEY | FCONTROL | FNOINVERT,  'F',        ID_EDIT_FIND},
			{FVIRTKEY | FNOINVERT,             VK_F3,      ID_EDIT_REPEAT},
			{FVIRTKEY | FSHIFT | FNOINVERT,    VK_F3,      ID_EDIT_REPEAT},
			{FVIRTKEY | FCONTROL | FNOINVERT,  'H',        ID_EDIT_REPLACE},
			{FVIRTKEY | FCONTROL | FNOINVERT,  'Z',        ID_EDIT_UNDO},
			{FVIRTKEY | FCONTROL | FNOINVERT,  'Y',        ID_EDIT_REDO},
			// Don't do this one, DEL doesn't work right if its in here,
			// and the delete key works fine without it
			//{FVIRTKEY | FNOINVERT,             VK_DELETE,  ID_EDIT_CLEAR},
			{FVIRTKEY | FSHIFT | FNOINVERT,    VK_DELETE,  ID_EDIT_CUT},
			{FVIRTKEY | FCONTROL | FNOINVERT,  VK_INSERT,  ID_EDIT_COPY},
			{FVIRTKEY | FSHIFT | FNOINVERT,    VK_INSERT,  ID_EDIT_PASTE}
		};

		m_hAccel = ::CreateAcceleratorTable(AccelTable, cAccel);
	}

	void DestroyAccelerators(void)
	{
		if(m_hAccel)
		{
			::DestroyAcceleratorTable(m_hAccel);
			m_hAccel = NULL;
		}
	}

};


/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_PlainTextView_H__053AD676_0AE2_11D6_8BF1_00500477589F__INCLUDED_)
