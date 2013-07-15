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

#include "DockMisc.h"
#include "DockingBox.h"

namespace dockwins{

CDWSettings::CSettings CDWSettings::settings;

CDockingBoxMessage	CDockingBox::m_message;

#ifdef DF_AUTO_HIDE_FEATURES

COutlookLikeCaption ::CPinButton::CIcons COutlookLikeCaption ::CPinButton::m_icons;
CVC6LikeCaption::CPinButton::CIcons CVC6LikeCaption::CPinButton::m_icons;

#endif

void DrawEllipsisText(CDC& dc,LPCTSTR sText,int n,LPRECT prc,bool bHorizontal)
{
	assert(n>0);
	long width=bHorizontal ? prc->right - prc->left : prc->bottom - prc->top;
	CSize size;
	LPTSTR sTmp=0;
	bool bRes=(GetTextExtentPoint32(dc, sText, n,&size)!=FALSE);
	assert(bRes);
	if(width<size.cx)
	{
		LPCTSTR sEllipsis=_T("...");
		const int ellipsisLen=sizeof(sEllipsis)-1;
		sTmp=new TCHAR[ellipsisLen+n];
		::lstrcpy(sTmp,_T("..."));
		::lstrcpyn(sTmp+ellipsisLen,sText,n);
		bRes=(GetTextExtentExPoint(dc,sTmp,ellipsisLen+n,width,&n,NULL,&size)!=FALSE);
		if(bRes)
		{
			if(n<ellipsisLen+1)
				n=ellipsisLen+1;
			::lstrcpyn(sTmp,sText,n-ellipsisLen);
			::lstrcpyn(sTmp+(n-ellipsisLen),sEllipsis,ellipsisLen);
			sText=sTmp;
		}
	}	

//	UINT prevAlign=dc.SetTextAlign(TA_LEFT | TA_TOP | TA_NOUPDATECP);
	CPoint pt(prc->left,prc->top);
	if(bHorizontal)
		pt.y = (prc->bottom - prc->top-size.cy)/2+prc->top;
	else
		pt.x = prc->right-(prc->right - prc->left-size.cy)/2;
	dc.ExtTextOut(pt.x,pt.y,ETO_CLIPPED,prc,sText,n,NULL);
//	dc.SetTextAlign(prevAlign);
	delete [] sTmp;
}

}//namespace dockwins