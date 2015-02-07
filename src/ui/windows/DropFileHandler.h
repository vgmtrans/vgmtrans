////////////////////////////////////////////////////////////////////////////////////////
//
//  WM_DROPFILES handler for Microsoft's Windows Template Library.
// 
//  Author: Pablo Aliskevicius.
//
//  The code and information  is provided by the  author and  copyright  holder 'as-is',
//  and any express or implied  warranties,  including, but not  limited to, the implied
//  warranties of  merchantability and  fitness for a particular purpose  are disclaimed.
//  In no event shall the author or copyright holders be liable for any direct, indirect,
//  incidental, special, exemplary, or consequential damages (including,  but not limited
//  to, procurement of substitute goods or services;  loss of use,  data, or profits;  or
//  business  interruption)  however caused  and on any  theory of liability,  whether in
//  contract, strict liability,  or tort  (including negligence or otherwise)  arising in
//  any way out of the use of this software,  even if advised of the  possibility of such
//  damage.
//
/////////////////////////////////////////////////////////////////////////////////////////

/*

Usage:
======

1. In a dialog:
   ------------
   
class CMyDlg : public CDialogImpl<CMyDlg>,
               public CDropFilesHandler<CMyDlg> // Inherit from the template, as usual...
{
public:
	enum { IDD = IDD_MYDIALOG }; // Make sure this one has EXSTYLE WS_EX_ACCEPTFILES defined.

	BEGIN_MSG_MAP(CMyDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog) // and others...
		...
		CHAIN_MSG_MAP(CDropFilesHandler<CMyDlg>)
	END_MSG_MAP()

    // You can use this function to initialize variables needed for one drop's batch.
	// Returning FALSE will avoid handling the drop, with no messages to the user whatsoever.
	BOOL IsReadyForDrop(void)
	{
		// If you're editing some document and the dirty bit is on, ask the user what to do...
		// else
		return TRUE; 
	}
	BOOL HandleDroppedFile(LPCTSTR szBuff)
	{
		// Implement this.
	    // Return TRUE unless you're done handling files (e.g., if you want 
		// to handle only the first relevant file, and you have already found it).
		ATLTRACE("%s\n", szBuff);
		DoSomethingWith(szBuff);
		return TRUE;
	}
	void EndDropFiles(void)
	{
		// Sometimes,  if your file handling is not trivial,  you might want to add all
		// file names to some container (std::list<WTL::CString>, perhaps),  and do the 
		// handling afterwards, in a worker thread. If so, this function is your choice.

	}
}; //  CMyDlg
   
2. In a view:
   ----------
    
You can also use this class in a view (SDI, MDI, M-SDI apps), in which case you should create 
your window with WS_EX_ACCEPTFILES, or call RegisterDropHandler after creating the view:

e.g.:
CMainFrame::OnCreate(...)
{

    m_view.Create(...);
    m_view.RegisterDropHandler(TRUE);

}

Alterations to inheritance list, message map, and the three additional functions are identical
to those done when handling dialogs.

*/

#ifndef __DROPFILEHANDLER_H__
#define __DROPFILEHANDLER_H__


template <class T> class CDropFilesHandler
{
	public:
	// For frame windows:
	// You might turn drop handling on or off, depending, for instance, on user's choice.
	void RegisterDropHandler(BOOL bTurnOn = TRUE)
	{
        T* pT = static_cast<T*>(this);
		ATLASSERT(::IsWindow(pT->m_hWnd));
		/* I'm ashamed; I rewrote an existing API. Luckily, I don't have to cut my small finger off...
		CWindow wnd;
		wnd.Attach(pT->m_hWnd);
		if (bTurnOn)
			wnd.ModifyStyleEx(0, WS_EX_ACCEPTFILES);
		else // Turn drop handling off.
			wnd.ModifyStyleEx(~WS_EX_ACCEPTFILES, 0);
		*/
		::DragAcceptFiles(pT->m_hWnd, bTurnOn);
	}
	protected:
		UINT m_nFiles; // Number of dropped files, in case you want to display some kind of progress.
		// We'll use a message map here, so future implementations can, if necessary, handle
		// more than one message.
		BEGIN_MSG_MAP(CDropFilesHandler<T>)
			MESSAGE_HANDLER(WM_DROPFILES, OnDropFiles) 
		END_MSG_MAP()
		// WM_DROPFILES handler: it calls a T function, with the path to one dropped file each time.
		LRESULT OnDropFiles(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
		{
			ATLTRACE("*** Got WM_DROPFILE! *******\n");
			T* pT = static_cast<T*>(this);
			// Your class should implement a boolean function. It should return TRUE only
			// if initialization (whatever is tried) was successful.
			if (!pT->IsReadyForDrop())
			{
				bHandled = FALSE;
				return 0;
			}
			CHDrop<CHAR> cd(wParam);
			// Your class should have a public member of type HWND (like CWindow does).
			if (cd.IsInClientRect(pT->m_hWnd))
			{
				m_nFiles = cd.GetNumFiles();
				if (0 < m_nFiles)
				{
					for (UINT i = 0; i < m_nFiles; i++)
					{
						if (0 < cd.DragQueryFile(i))
						{
							ATLTRACE(_T("   Dropped one! %s\n"), (LPCTSTR) cd);
							// Your class should return TRUE unless no more files are wanted.
							if (!pT->HandleDroppedFile((LPCTSTR) cd ))
								i = m_nFiles + 10; // Break the loop
						}
					} // for
					pT->EndDropFiles();
				}
			}
			else 
				bHandled = FALSE;
			m_nFiles = 0;
			return 0;
		}
	private:
		// Helper class for CDropFilesHandler, or any other handler for WM_DROPFILES.
		// This one is a template to avoid compiling when not used.
		// It is a wrapper for a HDROP.
		template <class CHAR> class CHDrop
		{
			HDROP m_hd;
			bool bHandled;             // Checks if resources should be released.
			WCHAR m_buff[MAX_PATH + 5]; // DragQueryFile() wants LPTSTR.
		public:
			// Constructor, destructor
			CHDrop(WPARAM wParam)
			{
				m_hd = (HDROP) wParam; 
				bHandled = false;
				m_buff[0] = '\0';
			}
			~CHDrop()
			{
				if (bHandled)
					::DragFinish(m_hd);
			}
			// Helper function, detects if the message is meant for 'this' window.
			BOOL IsInClientRect(HWND hw)
			{
				ATLASSERT(::IsWindow(hw));
				POINT p;
				::DragQueryPoint(m_hd, &p);
				RECT rc;
				::GetClientRect(hw, &rc);
				return ::PtInRect(&rc, p);
			}
			// This function returns the number of files dropped on the window by the current operation.
			UINT GetNumFiles(void)
			{
				return ::DragQueryFile(m_hd, 0xffffFFFF, NULL, 0);
			}
			// This function gets the whole file path for a file, given its ordinal number.
			UINT DragQueryFile(UINT iFile)
			{
				bHandled = true;
				return ::DragQueryFile(m_hd, iFile, m_buff, MAX_PATH);
			}
#ifdef _WTL_USE_CSTRING
			// CString overload for DragQueryFile (not used here, might be used by a handler
			// which is implemented outside CDropFilesHandler<T>.
			UINT DragQueryFile(UINT iFile, CString &cs)
			{
				bHandled = true;
				UINT ret = ::DragQueryFile(m_hd, iFile, m_buff, MAX_PATH);
				cs = m_buff;
				return ret;
			}
			inline operator CString() const { return CString(m_buff); }
#endif
			// Other string overloads (such as std::string) might come handy...
			
			// This class can 'be' the currently held file's path.
			inline operator const WCHAR *() const { return m_buff; }
			//inline operator const LPCTSTR () const { return m_buff; }
		}; // class CHDrop
};




#endif //__DROPFILEHANDLER_H__