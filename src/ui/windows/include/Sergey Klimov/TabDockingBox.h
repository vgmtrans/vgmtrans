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

#ifndef __WTL_DW__TABDOCKINGBOX_H__
#define __WTL_DW__TABDOCKINGBOX_H__

#pragma once

#include "DockingBox.h"
#include "FlyingTabs.h"

namespace dockwins{
	

template<class TTraits=COutlookLikeDockingBoxTraits>
class CTabDockingBox :
			public  CDockingBoxBaseImpl<CTabDockingBox<TTraits>,CWindow,TTraits>
{
	typedef	CDockingBoxBaseImpl<CTabDockingBox<TTraits>,CWindow,TTraits> baseClass;
	typedef CTabDockingBox  thisClass;
	typedef CFlyingTabCtrl	CTabCtrl;
protected:
	static void SetIndex(DFDOCKRECT* pHdr,int index)
	{
		pHdr->rect.left=index;
		pHdr->rect.right=index;
		pHdr->rect.top=index;
		pHdr->rect.bottom=index;
	}
	static int GetIndex(DFDOCKRECT* pHdr)
	{
		return pHdr->rect.left;
	}
public:
	static HWND CreateInstance(HWND hWnd)
	{
		thisClass* ptr=new thisClass;
		HWND hNewWnd=ptr->Create(hWnd);
		assert(hNewWnd);
		if(hNewWnd==NULL)
			delete ptr;
		return hNewWnd;
	}
	virtual bool DockMe(DFDOCKRECT* pHdr)
	{
		if(CDockingBox::IsWindowBox(pHdr->hdr.hBar))
		{
			assert(m_wnd.m_hWnd);
			HWND hActiveWnd=m_wnd.m_hWnd;
			int n=m_tabs.GetItemCount();
			assert(n>=2);
			while(n>0)
			{
				pHdr->hdr.hWnd=GetItemHWND(--n);
				if(pHdr->hdr.hWnd!=NULL)
				{
					RemoveWindow(pHdr->hdr.hWnd);
					m_docker.Dock(pHdr);
				}
			}
			pHdr->hdr.hWnd=hActiveWnd;
//			pHdr->hdr.code=DC_ACTIVATE;
			m_docker.Activate(&pHdr->hdr);

			PostMessage(WM_CLOSE);
		}
		else
			m_docker.Dock(pHdr);
		return true;
	}
	bool IsPointInAcceptedArea(POINT *pPt) const
	{
		HWND hWnd=::WindowFromPoint(*pPt);
		while( (hWnd!=m_hWnd)
					&&(hWnd!=NULL))
			hWnd=::GetParent(hWnd);
		bool bRes=(hWnd!=NULL);
		if(bRes)
		{
			CRect rc;
			CPoint pt(*pPt);
			m_tabs.GetTabsRect(&rc);
			m_tabs.ScreenToClient(pPt);
			bRes=(rc.PtInRect(*pPt)!=FALSE);
			if(!bRes)
			{
				bRes=::SendMessage(m_hWnd,WM_NCHITTEST,NULL,MAKELPARAM(pt.x, pt.y))==HTCAPTION;
				if(bRes)
				{
					if( !IsDocking() || m_caption.IsHorizontal() )
						pPt->y=(rc.bottom+rc.top)/2;
					else
						*pPt=rc.CenterPoint();
					// now only horizontal tab control supported
///11					assert((m_tabs.GetWindowLong(GWL_STYLE)&TCS_VERTICAL)==0);
					assert((m_tabs.GetWindowLong(GWL_STYLE)&CTCS_VERTICAL)==0);
				}
			}
		}
		return bRes;
	}
	LRESULT OnAcceptDock(DFDOCKRECT* pHdr)
	{
		CPoint pt(pHdr->rect.left,pHdr->rect.top);
		BOOL bRes=IsPointInAcceptedArea(&pt);
		if(bRes)
		{
			MSG msg={0};
			int curSel=m_tabs.GetCurSel();
			assert(curSel!=-1);
			HWND hWnd=GetItemHWND(curSel);
///11		bool bHorizontal=!(m_tabs.GetWindowLong(GWL_STYLE)&TCS_VERTICAL);
			bool bHorizontal=!(m_tabs.GetWindowLong(GWL_STYLE)&CTCS_VERTICAL);
			int pos = bHorizontal ? pt.x : pt.y;

///11		TCHITTESTINFO tchti = { 0 };
			CTCHITTESTINFO tchti = { 0 };
			tchti.pt.x = pt.x;
			tchti.pt.y = pt.y;
			int index=m_tabs.HitTest(&tchti);
			if((hWnd!=NULL) && (hWnd!=pHdr->hdr.hWnd))
			{
				if(index==-1)
				{
					RECT rc;
					m_tabs.GetItemRect(0,&rc);
					if(bHorizontal)
						index=(rc.left>pos) ? 0 : m_tabs.GetItemCount();
					else
						index=(rc.top>pos) ? 0 : m_tabs.GetItemCount();
				}
				m_prevSelItem=curSel;
				curSel=InsertWndTab(index,pHdr->hdr.hWnd,0);
				// dispatch all notifications
				while(PeekMessage(&msg, NULL, WM_NOTIFY, WM_NOTIFY, PM_REMOVE))
					DispatchMessage(&msg);
				m_prevItem=curSel;
				m_prevPos=pos;
				assert(index==curSel);
			}
			if(
				(index!=-1)
					&&( index!=curSel)
						 &&
						 !( ( index==m_prevItem)
								&& ( (pos-m_prevPos)*(m_prevItem-curSel) <=0) ) )
			{
				m_tabs.SwapItemPositions(curSel,index, false, false);
//				m_tabs.MoveItem(curSel,index);
				curSel=index;
				m_tabs.SetCurSel(curSel);
				m_prevItem=m_tabs.HitTest(&tchti);
			}
			m_prevPos = pos;
			pHdr->hdr.hBar=m_hWnd;
			SetIndex(pHdr,curSel);

			//check next message
			while(WaitMessage())
			{
				//redraw first
				while(PeekMessage(&msg, NULL, WM_PAINT, WM_PAINT, PM_REMOVE))
					DispatchMessage(&msg);
				if(PeekMessage(&msg, 0, WM_SYSKEYDOWN, WM_SYSKEYDOWN, PM_NOREMOVE))
					break;
				if(PeekMessage(&msg,pHdr->hdr.hWnd,0, 0, PM_NOREMOVE))
				{
					if(pHdr->hdr.hWnd==msg.hwnd)
											break;
					else
					{
						if(PeekMessage(&msg, msg.hwnd, msg.message, msg.message, PM_REMOVE))
							DispatchMessage(&msg);
					}
				}
			}

			pt.x=GET_X_LPARAM( msg.lParam );
			pt.y=GET_Y_LPARAM( msg.lParam );
			::ClientToScreen(msg.hwnd,&pt);

			if( msg.message!=WM_MOUSEMOVE
				|| (::GetKeyState(VK_CONTROL)&0x8000)
					|| !IsPointInAcceptedArea(&pt) )
			{
				if(GetItemHWND(curSel)==NULL)
				{
					m_tabs.DeleteItem(curSel,false);
					assert(m_prevSelItem>=0);
					assert(m_prevSelItem<m_tabs.GetItemCount());
					m_tabs.SetCurSel(m_prevSelItem);
				}
				//let control update itself!!!
				while(PeekMessage(&msg, NULL, WM_PAINT, WM_PAINT, PM_REMOVE))
					DispatchMessage(&msg);
			}
		}
		return bRes;
	}
	LRESULT OnDock(DFDOCKRECT* pHdr)
	{
		assert(pHdr->hdr.hWnd);
		int index=GetIndex(pHdr);
		int n=m_tabs.GetItemCount();
		if( (index<0) || (index>n) )
								index=n;
		return (InsertWndTab(index,pHdr->hdr.hWnd)!=-1);
	}

	LRESULT OnUndock(DFMHDR* pHdr)
	{
		CWindow wnd(pHdr->hWnd);
		assert(::IsWindow(pHdr->hWnd));
		BOOL bRes=RemoveWindow(pHdr->hWnd);
		IsStillAlive();
		return bRes;
	}

	LRESULT OnActivate(DFMHDR* pHdr)
	{
		int index=FindItem(pHdr->hWnd);
		BOOL bRes=(index!=-1);
		if(bRes)
			m_tabs.SetCurSel(index);
		if(!IsWindowVisible())
			Show();
		return bRes;
	}

	void PrepareForDock(CWindow wnd)
	{
		wnd.ShowWindow(SW_HIDE);
		DWORD style = wnd.GetWindowLong(GWL_STYLE);
		DWORD newStyle = style&(~(WS_POPUP | WS_CAPTION))|WS_CHILD;
		wnd.SetWindowLong( GWL_STYLE, newStyle);
		wnd.SetParent(m_hWnd);
		wnd.SendMessage(WM_NCACTIVATE,TRUE);
		wnd.SendMessage(WMDF_NDOCKSTATECHANGED,
			MAKEWPARAM(TRUE,FALSE),
			reinterpret_cast<LPARAM>(m_hWnd));

	}
	void PrepareForUndock(CWindow wnd)
	{
		wnd.ShowWindow(SW_HIDE);
		DWORD style = wnd.GetWindowLong(GWL_STYLE);
		DWORD newStyle = style&(~WS_CHILD) | WS_POPUP | WS_CAPTION;
		wnd.SetWindowLong( GWL_STYLE, newStyle);
		wnd.SetParent(NULL);
		wnd.SendMessage(WMDF_NDOCKSTATECHANGED,
			FALSE,
			reinterpret_cast<LPARAM>(m_hWnd));
	}
	int InsertWndTab(int index,CWindow wnd)
	{
		assert(wnd.IsWindow());
		PrepareForDock(wnd);
		return InsertWndTab(index,wnd,reinterpret_cast<DWORD_PTR>(wnd.m_hWnd));
	}
	int InsertWndTab(int index,CWindow wnd,DWORD_PTR param)
	{
		assert(index>=0);
		assert(index<=m_tabs.GetItemCount());
		assert(wnd.IsWindow());
		int txtLen=wnd.GetWindowTextLength()+1;
		TCHAR* ptxt = new TCHAR[txtLen];
		wnd.GetWindowText(ptxt,txtLen);
		int image = -1;
		HICON hIcon=wnd.GetIcon(FALSE);
// need conditional code because types don't match in winuser.h
#ifdef _WIN64
		if(hIcon == NULL)
			hIcon = (HICON)::GetClassLongPtr(wnd.m_hWnd, GCLP_HICONSM);
#else
		if(hIcon == NULL)
			hIcon = (HICON)LongToHandle(::GetClassLongPtr(wnd.m_hWnd, GCLP_HICONSM));
#endif

		if(hIcon)
			image = m_images.AddIcon(hIcon);
		index=m_tabs.InsertItem(index,ptxt,image,param);
		delete[] ptxt;
		return index;
	}
	BOOL RemoveWindow(CWindow wnd)
	{
		int index=FindItem(wnd);
		BOOL bRes=(index!=-1);
		if(bRes)
		{
/*
			TCITEM tci;
			tci.mask=TCIF_IMAGE ;
			if(m_tabs.GetItem(index,&tci))
				m_images.Remove(tci.iImage);
*/
			if(m_wnd.m_hWnd==wnd.m_hWnd)
				m_wnd.m_hWnd=NULL;
			bRes=m_tabs.DeleteItem(index);
			if(bRes)
				PrepareForUndock(wnd);
		}
		return bRes;
	}
	int FindItem(HWND hWnd) const
	{
		int n=m_tabs.GetItemCount();
		for( int i=0;i<n;i++)
		{
			if(GetItemHWND(i)==hWnd)
				return i;
		}
		return -1;
	}
	HWND GetItemHWND(int index) const
	{
		return reinterpret_cast<HWND>(m_tabs.GetItemData(index));
	}

	void AdjustCurentItem()
	{
		if(m_wnd.m_hWnd!=0)
		{
			assert(m_wnd.GetParent()==m_hWnd);
			CRect rc;
			GetClientRect(&rc);
			m_tabs.AdjustRect(FALSE,&rc);
			assert( (m_wnd.GetWindowLong(GWL_STYLE) & WS_CAPTION ) == 0 );
			m_wnd.SetWindowPos(HWND_TOP,&rc,SWP_SHOWWINDOW);
		}
	}
	void UpdateWindowCaption()
	{
		if(m_wnd.m_hWnd!=0)
		{
			assert(m_wnd.GetParent()==m_hWnd);
			int len= m_wnd.GetWindowTextLength()+1;
			TCHAR* ptxt = new TCHAR[len];
			m_wnd.GetWindowText(ptxt,len);
			SetWindowText(ptxt);
			HICON hIcon=m_wnd.GetIcon(FALSE);
			SetIcon(hIcon , FALSE);
			delete [] ptxt;
		}
	}

	void IsStillAlive()
	{
		int n=m_tabs.GetItemCount();
		if(n<=1)
		{
			PostMessage(WM_CLOSE,TRUE);
			if(n==0)
				Hide();
		}
	}
	bool CanBeClosed(unsigned long param)
	{
		int count=m_tabs.GetItemCount();
		bool bRes=(param==0);
		if(!bRes)
		{
			bRes=count<2;
			if(bRes && (count!=0))
			{
				HWND hWnd=GetItemHWND(0);
				assert(hWnd);
				if(hWnd)
				{
					::ShowWindow(hWnd,SW_HIDE);
					RemoveWindow(hWnd);
					if(IsDocking())
					{
						DFDOCKREPLACE dockHdr;
						dockHdr.hdr.hBar=GetOwnerDockingBar();
						dockHdr.hdr.hWnd=m_hWnd;
						dockHdr.hWnd=hWnd;
						m_docker.Replace(&dockHdr);
					}
					else
					{
						RECT rc;
						BOOL bRes=GetWindowRect(&rc);
						if(bRes)
							bRes=::SetWindowPos(hWnd,HWND_TOP,rc.left, rc.top,
													rc.right - rc.left, rc.bottom - rc.top,SWP_SHOWWINDOW);
					}
					assert(!IsDocking());
				}
			}
		}
		else
		{
			bRes=count<2;
			if(!bRes)
			{
				int curSel=m_tabs.GetCurSel();
				assert(curSel!=-1);
				HWND hWnd=GetItemHWND(curSel);      
				assert(hWnd);
				if(hWnd)
				{
					::PostMessage(hWnd, WM_CLOSE, 0, 0);
					if(curSel < (count-1))
					{
						m_tabs.SetCurSel(curSel+1);
					}
					else if(curSel > 0 && count > 1)
					{
						m_tabs.SetCurSel(curSel-1);
					}
					bRes = false;
				}
				else
				{
					bRes=baseClass::CanBeClosed(param);
				}
			}
		}
		return bRes;
	}
public:
	bool OnGetDockingPosition(DFDOCKPOS* pHdr) const
	{
		pHdr->hdr.hBar=GetOwnerDockingBar();
		bool bRes=baseClass::OnGetDockingPosition(pHdr);
		pHdr->nIndex=FindItem(pHdr->hdr.hWnd);
		assert(pHdr->nIndex!=-1);
		if(m_tabs.GetItemCount()==2)
			pHdr->hdr.hBar=GetItemHWND((pHdr->nIndex==0)?1:0);
		else
			pHdr->hdr.hBar=m_hWnd;
		if(m_wnd.m_hWnd==pHdr->hdr.hWnd)
			pHdr->dwDockSide|=CDockingSide::sActive;
		return bRes;
	}

	bool OnSetDockingPosition(DFDOCKPOS* pHdr)
	{
		assert(pHdr->hdr.hWnd);
		int index=pHdr->nIndex;
		int n=m_tabs.GetItemCount();
		if( (index<0) || (index>n) )
								index=n;
		return (InsertWndTab(index,pHdr->hdr.hWnd)!=-1);
	}
#ifdef DF_AUTO_HIDE_FEATURES
	bool PinUp(const CDockingSide& side,unsigned long width,bool bVisualize=false)
	{
		if(IsDocking())
					Undock();
		DFPINUP pinHdr;
		pinHdr.hdr.hWnd=m_wnd;
		pinHdr.hdr.hBar=GetOwnerDockingBar();
//		pinHdr.hdr.code=DC_PINUP;
		pinHdr.dwDockSide=side;
		pinHdr.nWidth=width;
		pinHdr.dwFlags=(bVisualize) ? DFPU_VISUALIZE : 0 ;
		pinHdr.n=m_tabs.GetItemCount();
		bool bRes=false;
		try{
			pinHdr.phWnds=new HWND[pinHdr.n];
			int n=pinHdr.n;
			while(--n>=0)
			{
				pinHdr.phWnds[n]=GetItemHWND(n);
				assert(::IsWindow(pinHdr.phWnds[n]));
				RemoveWindow(pinHdr.phWnds[n]);
			}

			bRes=m_docker.PinUp(&pinHdr);
			delete [] pinHdr.phWnds;
			PostMessage(WM_CLOSE);
		}
		catch(std::bad_alloc& /*e*/)
		{
		}
		return bRes;
	}
#endif

	DECLARE_WND_CLASS(_T("CTabDockingBox"))
	BEGIN_MSG_MAP(thisClass)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)

///11	NOTIFY_CODE_HANDLER(TCN_SELCHANGE, OnTabSelChange)
///11	NOTIFY_CODE_HANDLER(TCN_TABLEAVCTRL, OnTabLeavCtrl)
		NOTIFY_CODE_HANDLER(CTCN_SELCHANGE, OnTabSelChange)
		NOTIFY_CODE_HANDLER(CTCN_TABLEAVCTRL, OnTabLeavCtrl)

		NOTIFY_CODE_HANDLER(NM_DBLCLK, OnTabDblClk)
		REFLECT_NOTIFICATIONS()
		CHAIN_MSG_MAP(baseClass)
	END_MSG_MAP()

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	{
///11	m_tabs.Create(m_hWnd, rcDefault, NULL,WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | TCS_TOOLTIPS | TCS_BOTTOM);
		m_tabs.Create(m_hWnd, rcDefault, NULL,WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | CTCS_TOOLTIPS | CTCS_BOTTOM);
		BOOL bRes=m_images.Create(16, 16, ILC_COLOR32 | ILC_MASK , 0, 5);
		assert(bRes);
		if(bRes)
			m_tabs.SetImageList(m_images);
		return 0;
	}

	LRESULT OnSize(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
	{
		if(wParam != SIZE_MINIMIZED )
		{
			RECT rc;
			GetClientRect(&rc);
			m_tabs.SetWindowPos(NULL, &rc ,SWP_NOZORDER | SWP_NOACTIVATE);
			AdjustCurentItem();
		}
		bHandled = FALSE;
		return 1;
	}

	LRESULT OnEraseBkgnd(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	{
		return 0;
	}

	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
	{
		bHandled = FALSE;
		m_images.Destroy();
		return 0;
	}

	LRESULT OnSetFocus(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
	{
		int index=m_tabs.GetCurSel();
		if(index!=-1)
		{
			HWND hWnd=GetItemHWND(index);
			if(hWnd != NULL && ::IsWindowVisible(hWnd))
				::SetFocus(hWnd);
		}

		bHandled = FALSE;
		return 1;
	}

	LRESULT OnTabSelChange(int /*idCtrl*/, LPNMHDR pnmh, BOOL& /*bHandled*/)
	{
		if(pnmh->hwndFrom==m_tabs)
		{
			int index=m_tabs.GetCurSel();
			if(index!=-1)
			{
				HWND hWnd=GetItemHWND(index);
				if(hWnd!=NULL && (hWnd!=m_wnd) )
				{
					if(m_wnd.m_hWnd!=NULL)
					{
						assert(::GetParent(m_wnd.m_hWnd)==m_hWnd);
						m_wnd.ShowWindow(SW_HIDE);
					}
					m_wnd=hWnd;
					UpdateWindowCaption();
					AdjustCurentItem();
					if(hWnd != NULL && ::IsWindowVisible(hWnd))
						::SetFocus(hWnd);
				}
			}
		}
		return 0;
	}

	LRESULT OnTabLeavCtrl(int /*idCtrl*/, LPNMHDR pnmh, BOOL& /*bHandled*/)
	{
		BOOL bRes=TRUE;
		if(pnmh->hwndFrom==m_tabs)
		{
			int index=m_tabs.GetCurSel();
			assert(index!=-1);
			if(index!=-1)
			{
				HWND hWnd=GetItemHWND(index);
				assert(::IsWindow(hWnd));
				CRect rc;
				::GetWindowRect(hWnd,&rc);
				::PostMessage(hWnd,WM_NCLBUTTONDOWN,HTCAPTION,MAKELPARAM(rc.right,rc.bottom));
				/*
				DWORD dwPos = ::GetMessagePos();
				CPoint pt(GET_X_LPARAM(dwPos), GET_Y_LPARAM(dwPos));
				DWORD style=m_tabs.GetWindowLong(GWL_STYLE);
///11			if(style&TCS_VERTICAL)
				if(style&CTCS_VERTICAL)
				{
///11				if(style&TCS_BOTTOM)
					if(style&CTCS_BOTTOM)
						pt.x=2*rc.right-pt.x;
					else
						pt.x=2*rc.left-pt.x;
				}
				else
				{
///11				if(style&TCS_BOTTOM)
					if(style&CTCS_BOTTOM)
						pt.y=2*rc.bottom-pt.y;
					else
						pt.y=2*rc.top-pt.y;

				}
				::PostMessage(hWnd,WM_NCLBUTTONDOWN,HTCAPTION,MAKELPARAM(pt.x,pt.y));
				*/
				bRes=FALSE;
			}
		}
		return bRes;
	}
	LRESULT OnTabDblClk(int /*idCtrl*/, LPNMHDR pnmh, BOOL& /*bHandled*/)
	{
		if(pnmh->hwndFrom==m_tabs)
		{
			int index=m_tabs.GetCurSel();
			assert(index!=-1);
			if(index!=-1)
			{
				HWND hWnd=GetItemHWND(index);
				assert(::IsWindow(hWnd));
				::PostMessage(hWnd,WM_NCLBUTTONDBLCLK,HTCAPTION,0);
			}
		}
		return 0;
	}
protected:
	CImageList	m_images;
	CTabCtrl	m_tabs;
	CWindow		m_wnd;
////
	int			m_prevSelItem;
	int			m_prevItem;
	int			m_prevPos;
};

template <class TCaption,class TBox,DWORD t_dwStyle = 0, DWORD t_dwExStyle = 0>
struct CBoxedDockingWindowTraits
		: CDockingBoxTraits<TCaption,t_dwStyle,t_dwExStyle>
{
	typedef TBox	CBox;
};

typedef CBoxedDockingWindowTraits<COutlookLikeCaption, CTabDockingBox<COutlookLikeDockingBoxTraits>,
									WS_OVERLAPPEDWINDOW | WS_POPUP | WS_VISIBLE |
									WS_CLIPCHILDREN | WS_CLIPSIBLINGS,WS_EX_TOOLWINDOW>
								COutlookLikeBoxedDockingWindowTraits;

typedef CBoxedDockingWindowTraits<COutlookLikeExCaption, CTabDockingBox<COutlookLikeExDockingBoxTraits>,
									WS_OVERLAPPEDWINDOW | WS_POPUP | WS_VISIBLE |
									WS_CLIPCHILDREN | WS_CLIPSIBLINGS,WS_EX_TOOLWINDOW>
								COutlookLikeExBoxedDockingWindowTraits;

typedef CBoxedDockingWindowTraits<CVC6LikeCaption, CTabDockingBox<CVC6LikeDockingBoxTraits>,
									WS_OVERLAPPEDWINDOW | WS_POPUP | WS_VISIBLE |
									WS_CLIPCHILDREN | WS_CLIPSIBLINGS,WS_EX_TOOLWINDOW>
								CVC6LikeBoxedDockingWindowTraits;


}//namespace dockwins

#endif // __WTL_DW__TABDOCKINGBOX_H__
