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

#ifndef __WTL_DW__DWSTATE_H__
#define __WTL_DW__DWSTATE_H__

#pragma once

#include <cassert>
#include <queue>
#include "sstate.h"
#include "DockMisc.h"


namespace sstate{



struct IDockWndState : public IState
{
	virtual void Reset()=0;
	virtual bool Store(IMainState* /*pMState*/,CRegKey& /*key*/)=0;
	virtual bool Restore(IMainState* /*pMState*/,CRegKey& /*key*/)=0;
	virtual bool Store(IMainState* /*pMState*/,dockwins::DFDOCKPOSEX* /*pDocPos*/)=0;
	virtual bool Restore(IMainState* /*pMState*/,dockwins::DFDOCKPOSEX* /*pDocPos*/)=0;
};

class CDockWndMgr
{
#if _MSC_VER >= 7000
protected:
#else
public:
#endif
	class CImpl : public CStateBase<IState>
	{
#if _MSC_VER >= 7000
protected:
#else
public:
#endif
		struct CRestPos : dockwins::DFDOCKPOSEX
		{
			DWORD	weight;
			ID		id;
		};
protected:
		struct weighter : std::binary_function<CRestPos, CRestPos, bool>
		{
			bool operator()(const CRestPos& x, const CRestPos& y) const
			{
				return (x.weight > y.weight);
			}
		};

		typedef CStateHolder<IDockWndState>	CItem;
		typedef std::map<ID,CItem>			CBunch;
		typedef std::priority_queue<CRestPos,std::deque<CRestPos>, weighter > CRestQueue;

		class CStorer
		{
		public:
			CStorer(IMainState* pMState,CRegKey& keyTop)
					:m_pMState(pMState),m_keyTop(keyTop)
			{
			}
			void operator() (std::pair<const ID,CItem>& x) const
			{
				dockwins::DFDOCKPOSEX dpos={0};
				if(x.second->Store(m_pMState,&dpos))
				{
					std::basic_stringstream<TCHAR> sstrKey;
					sstrKey.flags(std::ios::hex | std::ios::showbase );
					sstrKey<<x.first;
					::RegSetValueEx(m_keyTop,sstrKey.str().c_str(),NULL,REG_BINARY,
										reinterpret_cast<CONST BYTE *>(&dpos),
										sizeof(dockwins::DFDOCKPOSEX));
				}
			}
		 protected:
			IMainState*	m_pMState;
			CRegKey&	m_keyTop;
		};
		class CQLoader
		{
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
///////////////			dpos.UpdateWeight();
						assert(dpos.bDocking);
//						dpos.weight=0;
						dpos.weight=DWORD(dpos.dockPos.fPctPos*0xffff)&0xffff;
						dpos.weight|=(dpos.dockPos.nBar&0x3fff)<<16;
						dpos.weight|=((~dpos.dockPos.dwDockSide)&3)<<30;
///////////////////////////////////////////////////
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
		struct CDefRestorer
		{
			void operator() (std::pair<const ID,CItem>& x) const
			{
				x.second->RestoreDefault();
			}
		};
		struct CResetter
		{
			void operator() (std::pair<const ID,CItem>& x) const
			{
				x.second->Reset();
			}
		};
	public:
		CImpl():m_nextFreeID(/*std::numeric_limits<ID>::max()*/ULONG_MAX)
		{
		}
		ID GetUniqueID() const
		{
			return m_nextFreeID--;
		}
		virtual bool Store(IMainState* pMState,CRegKey& key)
		{
			std::for_each(m_bunch.begin(),m_bunch.end(),CStorer(pMState,key));
			return true;
		}
		virtual bool Restore(IMainState* pMState,CRegKey& key)
		{
			std::for_each(m_bunch.begin(),m_bunch.end(),CResetter());
			std::for_each(m_bunch.begin(),m_bunch.end(),CQLoader(m_queue,pMState,key));
			while(!m_queue.empty())
			{
//				 CRestPos& dpos=const_cast<CRestPos&>(m_queue.top());
				 CRestPos dpos=m_queue.top();
				 assert(m_bunch.find(dpos.id)!=m_bunch.end());
				 m_bunch[dpos.id]->Restore(pMState,&dpos);
				 m_queue.pop();
			}
			return true;
		}
		virtual bool RestoreDefault()
		{
			std::for_each(m_bunch.begin(),m_bunch.end(),CDefRestorer());
			return true;
		}
		ID Add(IDockWndState* pState)
		{
			ID id=GetUniqueID();
			Add(id,pState);
			return id;
		}
		void Add(ID id,IDockWndState* pState)
		{
			CStateHolder<IDockWndState> h (pState);
			m_bunch[id]=h;
		}
		void Remove(ID id)
		{
			assert(m_bunch.find(id)!=m_bunch.end());
			m_bunch.erase(id);
		}
	protected:
		mutable ID	m_nextFreeID;
		CBunch		m_bunch;
		CRestQueue	m_queue;
	};
protected:
	CDockWndMgr(CImpl* pImpl):m_pImpl(pImpl)
	{
	}
public:
	CDockWndMgr()
	{
		m_pImpl=new CImpl();
	}
	~CDockWndMgr()
	{
		assert(m_pImpl);
		m_pImpl->Release();
	}
	operator IState* ()
	{
		return m_pImpl;
	}
	ID Add(IDockWndState* pState)
	{
		return m_pImpl->Add(pState);
	}
	void Add(ID id,IDockWndState* pState)
	{
		m_pImpl->Add(id,pState);
	}
	void Remove(ID id)
	{
		m_pImpl->Remove(id);
	}
protected:
	CImpl* m_pImpl;
};

template<class T>
class CDockingWindowStateAdapter
{
#if _MSC_VER >= 7000
protected:
#else
public:
#endif
	template<class T>
	class CImplT: public CStateBase<IDockWndState>
	{
	public:
        CImplT(T& x,int nDefCmdShow=SW_SHOWNOACTIVATE)
			:m_dockWnd(x),m_nDefCmdShow(nDefCmdShow)
        {
			::ZeroMemory(&m_pos, sizeof(dockwins::DFDOCKPOSEX));
			m_pos.dockPos.hdr.hWnd=NULL;
        }
		virtual void Reset()
		{
			if(m_dockWnd.GetDockingWindowPlacement(&m_pos))
				m_dockWnd.Hide();
			else
				m_pos.dockPos.hdr.hWnd=NULL;
		}
		virtual bool Store(IMainState* pMState,CRegKey& key)
		{
			dockwins::DFDOCKPOSEX dpos={0};
			ZeroMemory(&dpos,sizeof(dockwins::DFDOCKPOSEX));
			bool bRes=Store(pMState,&dpos);
			if(bRes)
				bRes=(::RegSetValueEx(key,ctxtPlacement,NULL,REG_BINARY,
										reinterpret_cast<CONST BYTE *>(&dpos),
										sizeof(dockwins::DFDOCKPOSEX))==ERROR_SUCCESS);
			return bRes;
		}
		virtual bool Restore(IMainState* pMState,CRegKey& key)
		{
			dockwins::DFDOCKPOSEX dpos={0};
			DWORD dwType;
			DWORD cbData=sizeof(dockwins::DFDOCKPOSEX);
            bool bRes=(::RegQueryValueEx(key,ctxtPlacement,NULL,&dwType,
							reinterpret_cast<LPBYTE>(&dpos),&cbData)==ERROR_SUCCESS)
							&&(dwType==REG_BINARY);
			if(bRes)
				bRes=Restore(pMState,&dpos);
			return bRes;
		}
		virtual bool RestoreDefault()
		{
			assert( (m_pos.dockPos.hdr.hWnd==NULL) || (m_pos.dockPos.hdr.hWnd==m_dockWnd.m_hWnd) );
			bool bRes=(m_pos.dockPos.hdr.hWnd!=NULL);
			if(bRes)
				bRes=m_dockWnd.SetDockingWindowPlacement(&m_pos);
			if(!bRes
				&&!m_dockWnd.IsWindowVisible()
					&& !m_dockWnd.IsDocking())
				m_dockWnd.ShowWindow(m_nDefCmdShow);
            return true;
		}
		virtual bool Store(IMainState* /*pMState*/,dockwins::DFDOCKPOSEX* pDocPos)
		{
			return m_dockWnd.GetDockingWindowPlacement(pDocPos);
		}
		virtual bool Restore(IMainState* pMState,dockwins::DFDOCKPOSEX* pDocPos)
		{
            if(pDocPos->bDocking)
            {
                dockwins::CDockingSide dside(pDocPos->dockPos.dwDockSide);
				if(dside.IsValid())
				{
					if(dside.IsHorizontal())
					{
						pDocPos->dockPos.nWidth=unsigned long(pDocPos->dockPos.nWidth*pMState->YRatio());
						pDocPos->dockPos.nHeight=unsigned long(pDocPos->dockPos.nHeight*pMState->XRatio());
					}
					else
					{
						pDocPos->dockPos.nWidth=unsigned long(pDocPos->dockPos.nWidth*pMState->XRatio());
						pDocPos->dockPos.nHeight=unsigned long(pDocPos->dockPos.nHeight*pMState->YRatio());
					}
				}
            }
            pDocPos->rect.left=LONG(pDocPos->rect.left*pMState->XRatio());
            pDocPos->rect.right=LONG(pDocPos->rect.right*pMState->XRatio());
            pDocPos->rect.top=LONG(pDocPos->rect.top*pMState->YRatio());
            pDocPos->rect.bottom=LONG(pDocPos->rect.bottom*pMState->YRatio());

			return m_dockWnd.SetDockingWindowPlacement(pDocPos);
		}
	protected:
		dockwins::DFDOCKPOSEX	m_pos;
		T&						m_dockWnd;
        int						m_nDefCmdShow;
		static	RECT			rcDefault;
	};
	typedef CImplT<T> CImpl;
	CDockingWindowStateAdapter(const CDockingWindowStateAdapter& x) : m_pImpl(0)
	{
	}
	CDockingWindowStateAdapter(CImpl* pImpl):m_pImpl(pImpl)
	{
	}
public:
	CDockingWindowStateAdapter(T& x,int nDefCmdShow=SW_SHOWNOACTIVATE)
	{
		m_pImpl=new CImpl(x,nDefCmdShow);
	}
	~CDockingWindowStateAdapter()
	{
		assert(m_pImpl);
		m_pImpl->Release();
	}
	operator IDockWndState* ()
	{
		return m_pImpl;
	}
protected:
	CImpl* m_pImpl;
};

}//namespace sstate
#endif // __WTL_DW__DWSTATE_H__
