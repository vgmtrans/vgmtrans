// HtmlFrame.h : interface of the CHtmlFrame class
//
/////////////////////////////////////////////////////////////////////////////

#if !defined(AFX_HexViewFrame_H__053AD675_0AE2_11D6_8BF1_00500477589F__INCLUDED_)
#define AFX_HexViewFrame_H__053AD675_0AE2_11D6_8BF1_00500477589F__INCLUDED_

#if _MSC_VER >= 1000
#pragma once
#endif // _MSC_VER >= 1000

#include "ExDispid.h"
#include "MySplitter.h"
#include "HexView.h"
#include "ItemTreeView.h"


typedef CWinTraits<WS_OVERLAPPEDWINDOW | WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | WS_MAXIMIZE, WS_EX_MDICHILD>	CHexViewFrameWinTraits;

class CHexViewFrame : public CFrameWindowImpl<CHexViewFrame>
{
protected:
	typedef CFrameWindowImpl<CHexViewFrame> baseClass;
	//typedef CTabbedFrameImpl<CSongFrame, CDotNetButtonTabCtrl<CTabViewTabItem>, CTabbedMDIChildWindowImpl<CSongFrame, CMDIWindow, CSongFrameWinTraits> > baseClass;
	typedef CHexViewFrame thisClass;

public:
	DECLARE_FRAME_WND_CLASS(NULL, IDR_MDICHILD)

	CHexView m_HexView;
	CItemTreeView m_ItemTreeView;
	//CPlainTextView m_HtmlSourceView;

	CMySplitter m_wndSplitter;

	CHexViewFrame();


	BEGIN_MSG_MAP(CHexViewFrame)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_SETTINGCHANGE, OnSettingChange)
		MESSAGE_HANDLER(WM_FORWARDMSG, OnForwardMsg)
		//COMMAND_ID_HANDLER(ID_VIEWSONG_SOURCE, OnViewSource)
		CHAIN_MSG_MAP(baseClass)
		REFLECT_NOTIFICATIONS()
	END_MSG_MAP()

	LRESULT OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled);
	LRESULT OnSettingChange(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/);
	LRESULT OnForwardMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/);

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

	inline VGMFile* GetCurFile()
	{
		return m_HexView.GetCurFile();
	}

	inline void SetCurFile(VGMFile* file)
	{
		return m_HexView.SetCurFile(file);
	}

	void SelectItem(VGMItem* newItem);
	


protected:
	bool bPopulatingItemTreeView;
	DWORD populateItemViewThreadID;
	HANDLE populateItemViewThread;
};


/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_HtmlFrame_H__053AD675_0AE2_11D6_8BF1_00500477589F__INCLUDED_)
