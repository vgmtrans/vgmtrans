
#ifndef __LIST_VIEW_NO_FLICKER_H__
#define __LIST_VIEW_NO_FLICKER_H__

// CListViewNoFlickerT is meant as a mix-in class to
//  help reduce flicker of a SysListView32.
//
// You can use CListViewNoFlickerT for a class that is
//  superclassing or subclassing a SysListView32.
//
// You can use CListViewNoFlicker as-is if you don't
//  need to specialize the list view

// If you can require Windows XP at a minimum, you should
//  use the extended style LVS_EX_DOUBLEBUFFER instead.
//  LVS_EX_DOUBLEBUFFER will also give you an alpha-blended
//  drag selection like windows explorer has in its file list views.

#ifndef __ATLAPP_H__
  #error ListViewNoFlicker.h requires atlapp.h to be included first
#endif

#ifndef __ATLGDIX_H__
  #error ListViewNoFlicker.h requires atlgdix.h to be included first
#endif

template <class T>
class CListViewNoFlickerT
{
protected:
	typedef CListViewNoFlickerT<T> thisClass;

protected:
	ATL::CWindow m_headerCtrl;

public:
	CListViewNoFlickerT() : m_headerCtrl(NULL)
	{
	}

	// We don't have a virtual destructor because this is a mix-in class
	~CListViewNoFlickerT()
	{
	}

public:
	// Call in WM_CREATE handler or SubclassWindow
	void Initialize(HWND hWndHeaderCtrl)
	{
		m_headerCtrl = hWndHeaderCtrl;
	}

	// Call in WM_DESTROY handler or UnsubclassWindow
	void Uninitialize(void)
	{
		m_headerCtrl = NULL;
	}

public:
	BEGIN_MSG_MAP(thisClass)
	ALT_MSG_MAP(1)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBackground)
		MESSAGE_HANDLER(WM_PAINT, OnPaint)
		MESSAGE_HANDLER(WM_PRINTCLIENT, OnPaint)
	END_MSG_MAP()

	LRESULT OnEraseBackground(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	{
		// Erase the background in OnPaint.
		// This is part of the key to flicker free drawing.
		return 1;
	}

	LRESULT OnPaint(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	{
		T* pT = static_cast<T*>(this);
		if( wParam != NULL )
		{
			WTL::CMemDC memdc((HDC)wParam, NULL);
			pT->DoPaint(memdc.m_hDC, memdc.m_rc);
		}
		else
		{
			WTL::CPaintDC dc(pT->m_hWnd);
			WTL::CMemDC memdc(dc.m_hDC, &dc.m_ps.rcPaint);
			pT->DoPaint(memdc.m_hDC, dc.m_ps.rcPaint);
		}
		return 0;
	}

	inline void DoPaint(WTL::CDCHandle dc, RECT& rcClip)
	{
		T* pT = static_cast<T*>(this);

		if(m_headerCtrl.IsWindow())
		{
			// Draw the header first
			m_headerCtrl.SendMessage(WM_ERASEBKGND, (WPARAM)(HDC)dc, 0);
			m_headerCtrl.SendMessage(WM_PAINT, (WPARAM)(HDC)dc, 0);
			m_headerCtrl.ValidateRect(&rcClip);

			// Prevent the header being drawn over
			CRect rcHeader;
			m_headerCtrl.GetClientRect(&rcHeader);
			dc.ExcludeClipRect(&rcHeader);
		}

		// Now draw the listview
		pT->DefWindowProc(WM_ERASEBKGND, (WPARAM)(HDC)dc, 0);
		pT->DefWindowProc(WM_PAINT, (WPARAM)(HDC)dc, 0);
	}

};

typedef CWinTraits<
			WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN |
			LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS,
			WS_EX_CLIENTEDGE> CListViewNoFlickerWinTraits;

class CListViewNoFlicker :
	public CWindowImpl<CListViewNoFlicker, CListViewCtrl, CListViewNoFlickerWinTraits>,
	public CListViewNoFlickerT<CListViewNoFlicker>
{
protected:
	typedef CListViewNoFlicker thisClass;
	typedef CWindowImpl<CListViewNoFlicker, CListViewCtrl, CListViewNoFlickerWinTraits> baseClass;
	typedef CListViewNoFlickerT<CListViewNoFlicker> noFlickerClass;

// Constructors
public:
	CListViewNoFlicker() { }

// Base Class overrides
public:
	BOOL SubclassWindow(HWND hWnd)
	{
		BOOL bRet = baseClass::SubclassWindow(hWnd);
		if(bRet)
		{
			noFlickerClass::Initialize(this->GetHeader());
		}
		return bRet;
	}

	HWND UnsubclassWindow(BOOL bForce = FALSE)
	{
		noFlickerClass::Uninitialize();

		return baseClass::UnsubclassWindow(bForce);
	}

// Message Handling
public:
	BEGIN_MSG_MAP(thisClass)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)

		CHAIN_MSG_MAP_ALT(noFlickerClass, 1)
		DEFAULT_REFLECTION_HANDLER()  // Just in case
	END_MSG_MAP()

	LRESULT OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	{
		LRESULT lRet = DefWindowProc(uMsg, wParam, lParam);

		noFlickerClass::Initialize(this->GetHeader());

		// We've already called DefWindowProc
		bHandled = TRUE;

		return lRet;
	}

	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
	{
		noFlickerClass::Uninitialize();

		// Let anyone else (including DefWindowProc) see the message
		bHandled = FALSE;
		return 0;
	}
};

#endif //__LIST_VIEW_NO_FLICKER_H__
