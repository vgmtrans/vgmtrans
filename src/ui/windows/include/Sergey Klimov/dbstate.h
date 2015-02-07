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

#ifndef __WTL_DW__DBSTATE_H__
#define __WTL_DW__DBSTATE_H__

#pragma once

#include <list>
#include "dwstate.h"
#include "TabDockingBox.h"

namespace sstate{

class CDockWndMgrEx: public CDockWndMgr
{
protected:
	class CImpl : public CDockWndMgr::CImpl
	{
	public:
		class CQLoader
		{
		protected:
			typedef CDockWndMgr::CImpl::CRestPos CRestPos;
		public:
			CQLoader(CRestQueue& q,IMainState* pMState,CRegKey& keyTop)
					:m_queue(q),m_pMState(pMState),m_keyTop(keyTop)
			{
			}
			void operator() (std::pair<const ID,CItem>& x) const
			{
				CRestPos dpos;
				std::basic_stringstream<TCHAR> sstrKey;
				sstrKey.flags(std::ios::hex | std::ios::showbase );
				sstrKey<<x.first;
				DWORD dwType;
				DWORD cbData=sizeof(dockwins::DFDOCKPOSEX);
				dockwins::DFDOCKPOSEX* ptr=static_cast<dockwins::DFDOCKPOSEX*>(&dpos);
				if((::RegQueryValueEx(m_keyTop,sstrKey.str().c_str(),NULL,&dwType,
									 reinterpret_cast<LPBYTE>(ptr),&cbData)==ERROR_SUCCESS)
									 &&(dwType==REG_BINARY))
				{
					if(dpos.bDocking)
					{
						dpos.id=x.first;
						dockwins::CDockingSide dside(dpos.dockPos.dwDockSide);
						if(dside.IsValid())
						{
/////////////////////////////3<<30+0x3f8<<20+0x3fff<<9+0x1ff
							assert(dpos.bDocking);
//							dpos.weight=0;
							dpos.weight=dpos.dockPos.nIndex&0x1ff;
							dpos.weight|=(DWORD(dpos.dockPos.fPctPos*0x3fff)&0x3fff)<<9;
							dpos.weight|=(dpos.dockPos.nBar&0x7f)<<20;
							dpos.weight|=((~dpos.dockPos.dwDockSide)&3)<<30;
///////////////////////////////////////////////////
						}
						else
							dpos.weight=reinterpret_cast<DWORD>(dpos.dockPos.hdr.hBar) | 0xe0000000;
						m_queue.push(dpos);
					}
					else
						x.second->Restore(m_pMState,&dpos);
				}
				else
					x.second->RestoreDefault();

			}
		protected:
			CRestQueue& m_queue;
			IMainState*	m_pMState;
			CRegKey&	m_keyTop;
		};

		class CPinner
		{
		protected:
			void PinUp()
			{
				m_pinHdr.n=(unsigned long)m_wnds.size();
				if(m_pinHdr.n>0)
				{
					try
					{
						if(m_pinHdr.n>1)
						{
							m_pinHdr.phWnds = new HWND [m_pinHdr.n];
							std::copy(m_wnds.begin(),m_wnds.end(),m_pinHdr.phWnds);
						}
						if(m_pinHdr.hdr.hWnd==NULL)
						{
							m_pinHdr.hdr.hWnd=*(m_wnds.begin());
							m_pinHdr.nWidth=m_width;
						}
						m_docker.PinUp(&m_pinHdr);
						delete [] m_pinHdr.phWnds;
						m_pinHdr.phWnds = 0;
					} catch(std::bad_alloc& /*E*/)
					{
					}
					m_pinHdr.hdr.hWnd=NULL;
					m_wnds.clear();
				}
			}
		public:
			CPinner(const dockwins::CDocker& docker)
				:m_docker(docker)
			{
				//m_pinHdr.hdr=HNONDOCKBAR;
				//m_pinHdr.hdr.code=DC_PINUP;
				ZeroMemory(&m_pinHdr,sizeof(dockwins::DFPINUP));
			}
			~CPinner()
			{
//				PinUp();
				if(!m_wnds.empty())
					PinUp();
			}
			void PinUp(const CRestPos& dpos)
			{
				dockwins::CDockingSide side (dpos.dockPos.dwDockSide);
				assert(side.IsPinned());
				dockwins::CDockingSide curSide (m_pinHdr.dwDockSide);
				if(curSide.Side()!=side.Side()
					|| (dpos.dockPos.nBar!=m_nBar) )
										PinUp();
				m_wnds.push_back(dpos.dockPos.hdr.hWnd);
				m_pinHdr.dwDockSide=side;
				m_nBar=dpos.dockPos.nBar;
				m_width=dpos.dockPos.nWidth;
				if(side.IsActive())
				{
					m_pinHdr.hdr.hWnd=dpos.dockPos.hdr.hWnd;
					m_pinHdr.nWidth=dpos.dockPos.nWidth;
				}
			}
			static void PrepareForRestoring(CRestPos& dpos)
			{
				::SetRectEmpty(&dpos.rect);
				dpos.bDocking=FALSE;
				dpos.bVisible=FALSE;
			}
		protected:
			std::list<HWND>				m_wnds;
			dockwins::DFPINUP			m_pinHdr;
			const dockwins::CDocker&	m_docker;
			unsigned long				m_nBar;
			int							m_width;
		};

		CImpl(HWND hDockingFrameWnd)
			:m_docker(hDockingFrameWnd)
		{
		}
		virtual bool Restore(IMainState* pMState,CRegKey& key)
		{
//bad style, probably I'll fix it later
			std::for_each(m_bunch.begin(),m_bunch.end(),CResetter());
			std::for_each(m_bunch.begin(),m_bunch.end(),CQLoader(m_queue,pMState,key));
			DWORD weight=0;
			HDOCKBAR hBar=HNONDOCKBAR;
			BOOL bVisible=TRUE;
			HWND hActiveWnd=NULL;
			CPinner pinner (m_docker);
			while(!m_queue.empty())
			{
//				CRestPos& dpos=const_cast<CRestPos&>(m_queue.top());
				CRestPos dpos=m_queue.top();
				assert(m_bunch.find(dpos.id)!=m_bunch.end());
				dockwins::CDockingSide side(dpos.dockPos.dwDockSide);
				if(side.IsValid() && side.IsPinned())
				{
					CPinner::PrepareForRestoring(dpos);
					m_bunch[dpos.id]->Restore(pMState,&dpos);
					pinner.PinUp(dpos);
				}
				else
				{
//					dpos.dockPos.hdr.hBar=((dpos.weight&0xfffffe00)==weight) ? hBar : HNONDOCKBAR;
////////////////////////
//very ugly :(
					if((dpos.weight&0xfffffe00)!=weight)
					{
						if(hBar!=HNONDOCKBAR)
						{
							if(dockwins::CDockingBox::IsWindowBox(hBar) && (hActiveWnd!=NULL))
							{
								dockwins::CDockingBox box(hBar);
								box.Activate(hActiveWnd);
							}
							if(!bVisible )
								::SendMessage(::GetParent(hBar),WM_CLOSE,0,0);
						}
						hActiveWnd=NULL;
						hBar=HNONDOCKBAR;
					}
					bVisible=dpos.bVisible;
					dpos.bVisible=TRUE;
					dpos.dockPos.hdr.hBar=hBar;
					m_bunch[dpos.id]->Restore(pMState,&dpos);

					if(side.IsActive())
						hActiveWnd=dpos.dockPos.hdr.hWnd;
					hBar=dpos.dockPos.hdr.hWnd;
					weight=dpos.weight&0xfffffe00;
				}
				m_queue.pop();
			}
			if(hBar!=HNONDOCKBAR)
			{
				if(dockwins::CDockingBox::IsWindowBox(hBar) && (hActiveWnd!=NULL))
				{
					dockwins::CDockingBox box(hBar);
					box.Activate(hActiveWnd);
				}
				if(!bVisible )
					::SendMessage(::GetParent(hBar),WM_CLOSE,0,0);
			}
			return true;
		}
	protected:
		dockwins::CDocker m_docker;
	};
public:
	CDockWndMgrEx(HWND hDockingFrameWnd)
		:CDockWndMgr(new CImpl(hDockingFrameWnd))
	{
	}
};

template<class T>
struct CDockingWindowStateAdapterEx : CDockingWindowStateAdapter<T>
{
	CDockingWindowStateAdapterEx(T& x,int nDefCmdShow=SW_SHOWNOACTIVATE)
		: CDockingWindowStateAdapter<T>(new CImpl(x,nDefCmdShow))
	{
	}
};
}//namespace sstate
#endif // __WTL_DW__DBSTATE_H__
