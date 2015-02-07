// Copyright (c) 2002
// Sergey Klimov (kidd@ukr.net)

#ifndef __WTL_DW__SSTATE_H__
#define __WTL_DW__SSTATE_H__

#pragma once

#include<memory>
#include<utility>
#include<algorithm>
#include<map>
#include<cassert>
#include<limits>
#include<string>
#include<sstream>

namespace sstate{

const	TCHAR ctxtGeneral[]		= _T("General");
const	TCHAR ctxtCXScreen[]	=_T("SM_CXSCREEN");
const	TCHAR ctxtCYScreen[]	=_T("SM_CYSCREEN");
const	TCHAR ctxtPlacement[]	=_T("placement");
const	TCHAR ctxtMainWindow[]	=_T("Main Window");
const	TCHAR ctxtVisible[]		=_T("visible");
const	TCHAR ctxtBand[]		=_T("band");
const	TCHAR ctxtStoreVer[]	=_T("version");

typedef std::basic_string<TCHAR> tstring;
typedef unsigned long ID;

//i'll replace CRegKey with a abstract interface later ;) 

struct IMainState
{
	virtual float XRatio() const = 0;
	virtual float YRatio() const = 0;
	virtual CRegKey& MainKey()=0;
};

struct IState
{
	virtual ~IState(){}
	virtual bool Store(IMainState* /*pMState*/,CRegKey& /*key*/)=0;
	virtual bool Restore(IMainState* /*pMState*/,CRegKey& /*key*/)=0;
	virtual bool RestoreDefault()=0;
	virtual void AddRef()=0;
	virtual void Release()=0;
};

template<class T>
class CStateBase : public T
{
public:
	CStateBase():m_ref(1)
	{
	}
	virtual void AddRef()
	{
		m_ref++;
	}
	virtual void Release()
	{
		if(--m_ref==0)
			delete this;
	}
	virtual ~CStateBase()
	{
		assert(m_ref==0);
	}
protected:
	unsigned long m_ref;
};

template<class T=IState>
class CStateHolder
{
	typedef CStateHolder<T> thisClass;
public:
	CStateHolder():m_pState(0)
	{
	}
	CStateHolder(T* pState)
	{
		pState->AddRef();
		m_pState=pState;
	}
	CStateHolder(const thisClass& sholder)
	{
		*this=(sholder);
	}
	thisClass& operator = (const thisClass& sholder)
	{
		m_pState=const_cast<T*>(sholder.m_pState);
		if(m_pState!=0)
			m_pState->AddRef();
		return *this;
	}
	~CStateHolder()
	{
		if(m_pState!=0)
			m_pState->Release();
	}
	const T* operator ->() const
	{
		return m_pState;
	}
	T* operator ->()
	{
		return m_pState;
	}
protected:
	T* m_pState;
};

class CMainState : public IMainState
{
public:
    CMainState()
    {
    }
    CMainState(LPCTSTR lpszKeyName):m_strMainKey(lpszKeyName)
    {
    }
    CMainState(const tstring& strKeyName):m_strMainKey(strKeyName)
    {
    }
    virtual CRegKey& MainKey()
    {
        assert(m_keyMain.m_hKey);
        return m_keyMain;
    }
    virtual float XRatio() const
    {
		return m_xratio;
    }
    virtual float YRatio() const
    {
		return m_yratio;
    }
    bool Store()
    {
        DWORD dwDisposition;
        assert(!m_strMainKey.empty());
        bool bRes=(m_keyMain.Create(HKEY_CURRENT_USER,m_strMainKey.c_str(),REG_NONE,
                                    REG_OPTION_NON_VOLATILE, KEY_WRITE|KEY_READ,
                                    NULL,&dwDisposition)==ERROR_SUCCESS);
        if(bRes)
        {
            CRegKey keyGeneral;
            bRes=(keyGeneral.Create(m_keyMain,ctxtGeneral,REG_NONE,
                                    REG_OPTION_NON_VOLATILE, KEY_WRITE|KEY_READ,
                                    NULL,&dwDisposition)==ERROR_SUCCESS);
            if(bRes)
            {

				DWORD val=::GetSystemMetrics(SM_CXSCREEN);
				::RegSetValueEx(keyGeneral, ctxtCXScreen, NULL, REG_DWORD,
								reinterpret_cast<BYTE*>(&val), sizeof(DWORD));
				val=::GetSystemMetrics(SM_CYSCREEN);
				::RegSetValueEx(keyGeneral, ctxtCYScreen, NULL, REG_DWORD,
								reinterpret_cast<BYTE*>(&val), sizeof(DWORD));
/*
				keyGeneral.SetValue(::GetSystemMetrics(SM_CXSCREEN),ctxtCXScreen);
				keyGeneral.SetValue(::GetSystemMetrics(SM_CYSCREEN),ctxtCYScreen);
*/
            }
        }
        return bRes;
    }
    bool Restore()
    {
        assert(!m_strMainKey.empty());
        bool bRes=(m_keyMain.Open(HKEY_CURRENT_USER,m_strMainKey.c_str(),KEY_READ)==ERROR_SUCCESS);
        if(bRes)
        {
            CRegKey keyGeneral;
            bRes=(keyGeneral.Open(m_keyMain,ctxtGeneral,KEY_READ)==ERROR_SUCCESS);
            {
                SIZE szScreen;
				DWORD dwCount = sizeof(DWORD);
				m_xratio=(::RegQueryValueEx(keyGeneral,ctxtCXScreen,NULL,NULL,
								reinterpret_cast<LPBYTE>(&szScreen.cx),&dwCount) ==ERROR_SUCCESS
								  && (dwCount == sizeof(DWORD)))
                                                ?float(::GetSystemMetrics(SM_CXSCREEN))/szScreen.cx
                                                :float(1.0);
				dwCount = sizeof(DWORD);
				m_yratio=(::RegQueryValueEx(keyGeneral,ctxtCYScreen,NULL,NULL,
								reinterpret_cast<LPBYTE>(&szScreen.cy),&dwCount) ==ERROR_SUCCESS
								  &&(dwCount == sizeof(DWORD)))
                                                ?float(::GetSystemMetrics(SM_CYSCREEN))/szScreen.cy
                                                :float(1.0);
/*
                m_xratio=(keyGeneral.QueryValue(reinterpret_cast<DWORD&>(szScreen.cx),ctxtCXScreen)==ERROR_SUCCESS)
                                                ?float(::GetSystemMetrics(SM_CXSCREEN))/szScreen.cx
                                                :float(1.0);
                m_yratio=(keyGeneral.QueryValue(reinterpret_cast<DWORD&>(szScreen.cy),ctxtCYScreen)==ERROR_SUCCESS)
                                                ?float(::GetSystemMetrics(SM_CYSCREEN))/szScreen.cy
                                                :float(1.0);
*/
            }
        }
        return bRes;
    }
public:
    CRegKey m_keyMain;
    tstring m_strMainKey;
    float   m_xratio;
    float   m_yratio;
};

class CContainerImpl : public CStateBase<IState>
{
protected:
	typedef CStateHolder<IState> CItem;
	typedef std::map<ID,CItem> CBunch;
    class CStorer
    {
    public:
		CStorer(IMainState* pMState,CRegKey& keyTop)
				:m_pMState(pMState),m_keyTop(keyTop)
        {
        }
        void operator() (std::pair<const ID,CItem>& x) const
        {
            std::basic_stringstream<TCHAR> sstrKey;
            sstrKey.flags(std::ios::hex | std::ios::showbase );
            sstrKey<<x.first;
            CRegKey key;
            DWORD dwDisposition;
            LONG lRes = key.Create(m_keyTop,sstrKey.str().c_str(),REG_NONE,
                                    REG_OPTION_NON_VOLATILE, KEY_WRITE|KEY_READ,
                                    NULL,&dwDisposition);
            if(lRes==ERROR_SUCCESS)
                    x.second->Store(m_pMState,key);
        }
    protected:
		IMainState*	m_pMState;
		CRegKey&	m_keyTop;
    };
	class CRestorer
	{
	public:
		CRestorer(IMainState* pMState,CRegKey& keyTop)
				:m_pMState(pMState),m_keyTop(keyTop)
		{
		}
		void operator() (std::pair<const ID,CItem>& x) const
		{
            std::basic_stringstream<TCHAR> sstrKey;
            sstrKey.flags(std::ios::hex | std::ios::showbase );
            sstrKey<<x.first;
            CRegKey key;
            LONG lRes = key.Open(m_keyTop,sstrKey.str().c_str(),KEY_READ);
            if(lRes==ERROR_SUCCESS)
                x.second->Restore(m_pMState,key);
			else
				x.second->RestoreDefault();
		}
	protected:
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
public:
	CContainerImpl():m_nextFreeID(/*std::numeric_limits<ID>::max()*/ULONG_MAX)
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
        std::for_each(m_bunch.begin(),m_bunch.end(),CRestorer(pMState,key));
		return true;
	}
	virtual bool RestoreDefault()
	{
        std::for_each(m_bunch.begin(),m_bunch.end(),CDefRestorer());
		return true;
	}
	ID Add(IState* pState)
	{
		ID id=GetUniqueID();
		Add(id,pState);
		return id;
	}
	void Add(ID id,IState* pState)
	{
		CStateHolder<IState> h (pState);
		m_bunch[id]=h;
	}
	void Remove(ID id)
	{
		assert(m_bunch.find(id)!=m_bunch.end());
		m_bunch.erase(id);
	}
protected:
	mutable ID	m_nextFreeID;
	CBunch m_bunch;
};

class CWindowStateMgr
{
protected:
	class CImpl : public CContainerImpl
	{
		typedef CContainerImpl baseClass;
	public:
		CImpl(HWND hWnd=NULL,int nDefCmdShow=SW_SHOWNOACTIVATE)
			:m_hWnd(hWnd),m_nDefCmdShow(nDefCmdShow)
		{
		}
        void SetWindow(HWND hWnd=NULL,int nDefCmdShow=SW_SHOWNOACTIVATE)
        {
            assert(::IsWindow(hWnd));
            m_hWnd=hWnd;
            m_nDefCmdShow=nDefCmdShow;
        }
		virtual bool Store(IMainState* pMState,CRegKey& key)
		{
			assert(IsWindow(m_hWnd));

            WINDOWPLACEMENT wp;
            wp.length = sizeof(WINDOWPLACEMENT);
            bool bRes=false;
            if (::GetWindowPlacement(m_hWnd,&wp))
            {
                wp.flags = 0;
                if(::IsZoomed(m_hWnd))
					wp.flags |= WPF_RESTORETOMAXIMIZED;
				if(wp.showCmd==SW_SHOWMINIMIZED)
					wp.showCmd=SW_SHOWNORMAL;					
                bRes=(::RegSetValueEx(key,ctxtPlacement,NULL,REG_BINARY,
										reinterpret_cast<CONST BYTE *>(&wp),
										sizeof(WINDOWPLACEMENT))==ERROR_SUCCESS);
            }
			return baseClass::Store(pMState,key);
		}
		virtual bool Restore(IMainState* pMState,CRegKey& key)
		{
			assert(IsWindow(m_hWnd));
            WINDOWPLACEMENT wp;
            DWORD dwType;
            DWORD cbData=sizeof(WINDOWPLACEMENT);

            bool bRes=(::RegQueryValueEx(key,ctxtPlacement,NULL,&dwType,
											reinterpret_cast<LPBYTE>(&wp),&cbData)==ERROR_SUCCESS)
											&&(dwType==REG_BINARY);
            if(bRes)
			{
				UINT nCmdShow=wp.showCmd;
//				LockWindowUpdate(m_hWnd);
				if(wp.showCmd==SW_MAXIMIZE)
					::ShowWindow(m_hWnd,nCmdShow);
				wp.showCmd=SW_HIDE;
                ::SetWindowPlacement(m_hWnd,&wp);
				bRes=baseClass::Restore(pMState,key);
				::ShowWindow(m_hWnd,nCmdShow);
//				LockWindowUpdate(NULL);
			}
			else
				bRes=baseClass::RestoreDefault();
			return bRes;
		}
		virtual bool RestoreDefault()
		{
			assert(IsWindow(m_hWnd));
			bool bRes=baseClass::RestoreDefault();
			ShowWindow(m_hWnd,m_nDefCmdShow);
			return bRes;
		}
	protected:
		HWND	m_hWnd;
		int		m_nDefCmdShow;
	};
public:
	CWindowStateMgr(HWND hWnd=NULL,int nDefCmdShow=SW_SHOWNOACTIVATE)
	{
		m_pImpl=new CImpl(hWnd,nDefCmdShow);
	}
	~CWindowStateMgr()
	{
		assert(m_pImpl);
		m_pImpl->Release();
	}
	operator IState* ()
	{
		return m_pImpl;
	}
	ID Add(IState* pState)
	{
		return m_pImpl->Add(pState);
	}
	void Add(ID id,IState* pState)
	{
		m_pImpl->Add(id,pState);
	}
	void Remove(ID id)
	{
		m_pImpl->Remove(id);
	}
    void Initialize(const tstring& strMainKey,HWND hWnd,int nDefCmdShow=SW_SHOWNOACTIVATE)
    {
		m_pImpl->SetWindow(hWnd,nDefCmdShow);
		m_strMainKey=strMainKey;
    }
    void Store()
    {
        CMainState mstate(m_strMainKey);
        if(mstate.Store())
        {
            CRegKey key;
            DWORD dwDisposition;
            if(key.Create(mstate.MainKey(),ctxtMainWindow,REG_NONE,
							REG_OPTION_NON_VOLATILE, KEY_WRITE|KEY_READ,
							NULL,&dwDisposition)==ERROR_SUCCESS)
					m_pImpl->Store(&mstate,key);
        }
    }
    void Restore()
    {
        CMainState mstate(m_strMainKey);
        CRegKey key;
        if(mstate.Restore()&&
            (key.Open(mstate.MainKey(),ctxtMainWindow,KEY_READ)==ERROR_SUCCESS))
                    m_pImpl->Restore(&mstate,key);
        else
			m_pImpl->RestoreDefault();
    }
protected:
    tstring	m_strMainKey;
	CImpl*	m_pImpl;
};

class CWindowStateAdapter
{
protected:
	class CImpl : public CStateBase<IState>
	{
	public:
		CImpl(HWND hWnd,int nDefCmdShow=SW_SHOWNA)
			:m_hWnd(hWnd),m_nDefCmdShow(nDefCmdShow)
		{
			assert(::IsWindow(hWnd));
		}
		virtual bool Store(IMainState* /*pMState*/,CRegKey& key)
		{
            WINDOWPLACEMENT wp;
            wp.length = sizeof(WINDOWPLACEMENT);
            assert(::IsWindow(m_hWnd));
            bool bRes=false;
            if (::GetWindowPlacement(m_hWnd,&wp))
            {
                wp.flags = 0;
                if (::IsZoomed(m_hWnd))
                        wp.flags |= WPF_RESTORETOMAXIMIZED;
                bRes=(::RegSetValueEx(key,ctxtPlacement,NULL,REG_BINARY,
										reinterpret_cast<CONST BYTE *>(&wp),
										sizeof(WINDOWPLACEMENT))==ERROR_SUCCESS);
            }
			return bRes;
		}
		virtual bool Restore(IMainState* /*pMState*/,CRegKey& key)
		{
            assert(::IsWindow(m_hWnd));
            WINDOWPLACEMENT wp;
            DWORD dwType;
            DWORD cbData=sizeof(WINDOWPLACEMENT);
            bool bRes=(::RegQueryValueEx(key,ctxtPlacement,NULL,&dwType,
											reinterpret_cast<LPBYTE>(&wp),&cbData)==ERROR_SUCCESS)
											&&(dwType==REG_BINARY);
            if(bRes)
                    bRes=(::SetWindowPlacement(m_hWnd,&wp)!=FALSE);
            return bRes;
		}
		virtual bool RestoreDefault()
		{
			::ShowWindow(m_hWnd,m_nDefCmdShow);
			return true;
		}
	protected:
		HWND	m_hWnd;
		int		m_nDefCmdShow;
	};
public:
    CWindowStateAdapter(HWND hWnd,int nDefCmdShow=SW_SHOWNOACTIVATE)
    {
		m_pImpl = new CImpl(hWnd,nDefCmdShow);
    }
	~CWindowStateAdapter()
	{
		assert(m_pImpl);
		m_pImpl->Release();
	}
	operator IState* ()
	{
		return m_pImpl;
	}
protected:
	CImpl* m_pImpl;
};

class CToggleWindowAdapter
{
protected:
	class CImpl : public CStateBase<IState>
	{
	public:
		CImpl(HWND hWnd,int nDefCmdShow=SW_SHOWNA)
			:m_hWnd(hWnd),m_nDefCmdShow(nDefCmdShow)
		{
			assert(::IsWindow(hWnd));
		}
		virtual bool Store(IMainState* /*pMState*/,CRegKey& key)
		{
            DWORD visible=::IsWindowVisible(m_hWnd);
			return (::RegSetValueEx(key, ctxtVisible, NULL, REG_DWORD,
								reinterpret_cast<BYTE*>(&visible), sizeof(DWORD))==ERROR_SUCCESS);
//            return (key.SetValue(visible,ctxtVisible)==ERROR_SUCCESS);
		}
		virtual bool Restore(IMainState* /*pMState*/,CRegKey& key)
		{
            DWORD visible;
//          bool bRes=(key.QueryValue(visible,ctxtVisible)==ERROR_SUCCESS);
			DWORD dwCount = sizeof(DWORD);
			bool bRes=(::RegQueryValueEx(key,ctxtVisible,NULL,NULL,
								reinterpret_cast<LPBYTE>(&visible),&dwCount)==ERROR_SUCCESS
									 && (dwCount == sizeof(DWORD)));
            if(bRes)
                    ::ShowWindow(m_hWnd, (visible!=0) ? SW_SHOWNA : SW_HIDE);
            else
                    RestoreDefault();
            return bRes;
		}
		virtual bool RestoreDefault()
		{
            ::ShowWindow(m_hWnd,m_nDefCmdShow);
            return true;
		}
	protected:
		HWND	m_hWnd;
		int		m_nDefCmdShow;
	};
public:
    CToggleWindowAdapter(HWND hWnd,int nDefCmdShow=SW_SHOWNOACTIVATE)
    {
		m_pImpl = new CImpl(hWnd,nDefCmdShow);
    }
	~CToggleWindowAdapter()
	{
		assert(m_pImpl);
		m_pImpl->Release();
	}
	operator IState* ()
	{
		return m_pImpl;
	}
protected:
	CImpl* m_pImpl;
};
class CRebarStateAdapter
{
protected:
	class CImpl : public CStateBase<IState>
	{
	public:
		CImpl(HWND hWnd, int storeVersion)
			:m_rebar(hWnd), m_storageVersion(storeVersion)
		{
			assert(::IsWindow(hWnd));
		}

		virtual bool Store(IMainState* /*pMState*/,CRegKey& key)
		{
			assert(m_rebar.IsWindow());
			::RegSetValueEx(key,ctxtStoreVer, NULL, REG_DWORD, (LPBYTE)&m_storageVersion, sizeof(DWORD));
			
			unsigned int bandCount=m_rebar.GetBandCount();
			for(unsigned int i=0;i<bandCount;i++)
			{
				std::basic_stringstream<TCHAR> sstrKey;
				sstrKey<<ctxtBand<<i;
				REBARBANDINFO rbi;
				ZeroMemory(&rbi,sizeof(REBARBANDINFO));
				rbi.cbSize = sizeof(REBARBANDINFO);
				rbi.fMask = RBBIM_ID | 
							RBBIM_SIZE | RBBIM_STYLE
							// The following causes the app to remember rebar colors,
							// breaking windows theme changes.
							//RBBIM_COLORS |
							// The following causes the rebars to shift left on restore.
							#if (_WIN32_IE >= 0x0400)
								| /*RBBIM_HEADERSIZE |*/ RBBIM_IDEALSIZE
							#endif	
								;
				m_rebar.GetBandInfo(i, &rbi);
				::RegSetValueEx(key,sstrKey.str().c_str(),NULL,REG_BINARY,
								reinterpret_cast<CONST BYTE *>(&rbi),
								rbi.cbSize);
			}
			return true;
		}

		virtual bool Restore(IMainState* /*pMState*/,CRegKey& key)
		{
			DWORD dwType;
			DWORD dwVal;
			DWORD cbData = sizeof(DWORD);

			if( ::RegQueryValueEx(key, ctxtStoreVer, NULL, &dwType,
				(LPBYTE)&dwVal, &cbData) == ERROR_SUCCESS && (dwType == REG_DWORD) )
			{
				// If the versions aren't the same, then we don't bother to
				// restore - we'll probably brake the ReBars by doing so.
				if( dwVal != (DWORD)m_storageVersion )
					return false;
			}
			else
			{
				// If we couldn't query that key, then we never wrote a version
				// number before, so it must be an old version.
				return false;
			}

			unsigned int bandCount=m_rebar.GetBandCount();
			for(unsigned int i=0;i<bandCount;i++)
			{
				std::basic_stringstream<TCHAR> sstrKey;
				sstrKey<<ctxtBand<<i;
				CRegKey keyBand;
				REBARBANDINFO rbi;
				//ZeroMemory(&rbi,sizeof(REBARBANDINFO));
				
				cbData=sizeof(REBARBANDINFO);
	            if((::RegQueryValueEx(key,sstrKey.str().c_str(),NULL,&dwType,
							reinterpret_cast<LPBYTE>(&rbi),&cbData)==ERROR_SUCCESS)
							&&(dwType==REG_BINARY))
				{
					m_rebar.MoveBand(m_rebar.IdToIndex(rbi.wID), i);
					m_rebar.SetBandInfo(i, &rbi);
				}
			}
			return true;

		}
		virtual bool RestoreDefault()
		{
			return true;
		}
	protected:
		CReBarCtrl	m_rebar;
		int			m_storageVersion;
	};
public:
    CRebarStateAdapter(HWND hWnd, int storeVersion = 0)
    {
		m_pImpl = new CImpl(hWnd, storeVersion);
    }
	~CRebarStateAdapter()
	{
		assert(m_pImpl);
		m_pImpl->Release();
	}
	operator IState* ()
	{
		return m_pImpl;
	}
protected:
	CImpl* m_pImpl;
};

}//namespace sstate
#endif // __WTL_DW__SSTATE_H__
