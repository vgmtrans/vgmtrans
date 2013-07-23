// BmpBtn.h

#pragma once

#define WM_GETTHISPOINTER 0xFFFFF

class CBmpBtn : public CBitmapButtonImpl<CBmpBtn>
{
public:
	DECLARE_WND_SUPERCLASS(_T("WTL_BmpBtn"), GetWndClassName())

	// added border style (auto3d_single)
	CBmpBtn(DWORD dwExtendedStyle = BMPBTN_AUTOSIZE | BMPBTN_AUTO3D_SINGLE, HIMAGELIST hImageList = NULL,
			COLORREF theBkgClr = 0xFFFFFF, DWORD theUserData = NULL) : 
		CBitmapButtonImpl<CBmpBtn>(dwExtendedStyle, hImageList), bkgClr(theBkgClr), userData(theUserData)
	{ }

	BEGIN_MSG_MAP(CBmpBtn)
		CHAIN_MSG_MAP(CBitmapButtonImpl<CBmpBtn>)
	END_MSG_MAP()

	// override of CBitmapButtonImpl DoPaint(). Adds fillrect
	void DoPaint(CDCHandle dc)
	{
		// gets rid of artifacts.
		RECT rc;
		GetClientRect(&rc);
		dc.FillRect(&rc, bkgClr);

		// call ancestor DoPaint() method
		CBitmapButtonImpl<CBmpBtn>::DoPaint(dc);
	}

	DWORD GetData()
	{
		return userData;
	}

	void SetData(DWORD data)
	{
		userData = data;
	}


protected:
	DWORD userData;
	COLORREF bkgClr;
};
