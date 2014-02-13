#ifndef VGMTRANSWINDOW_H_INCLUDED
#define VGMTRANSWINDOW_H_INCLUDED

#include "VGMItem.h"
#include "resource.h"
//#include "mainfrm.h"

template <class T>
class VGMTransWindow
{
public:
	VGMTransWindow() {}
	~VGMTransWindow() {}
 
    //BEGIN_MSG_MAP(CPaintBkgnd)
    //    MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
    //END_MSG_MAP()
 
	//bool ItemContextMenu(HWND hwnd, CPoint menuPt, VGMItem* item);
	bool ItemContextMenu(HWND hwnd, CPoint menuPt, VGMItem* item)
	{
		// Build up the menu to show
		CMenu mnuContext;

		if(mnuContext.CreatePopupMenu())
		{
			//int cchWindowText = ((T)this)->GetWindowTextLength();
			//CString sWindowText;
			//((T)this)->GetWindowText(sWindowText.GetBuffer(cchWindowText+1), cchWindowText+1);
			//sWindowText.ReleaseBuffer();

			//CString sSave(_T("&Save '"));
			//sSave += sWindowText;
			//sSave += _T("'");

			std::vector<const wchar_t*>* menuItemNames = item->GetMenuItemNames();

			if (!menuItemNames)
				return false;
			for (UINT i=0; i<menuItemNames->size(); i++)		//for every menu item
			{
				//USES_CONVERSION;
				//LPCTSTR str = A2W((*menuItemNames)[i]);
				mnuContext.AppendMenu((MF_ENABLED | MF_STRING), i+1, (*menuItemNames)[i]);
			}
			//Menu* menu = item->GetMenu();
			//if (!menu)
			//	return false;
			//vector<MenuItem>* pvMenuItems = menu->GetMenuItems();

			//for (UINT i=0; i<pvMenuItems->size(); i++)		//for every menu item
			//{
			//	USES_CONVERSION;
			//	LPCTSTR str = A2W((*pvMenuItems)[i].name);
			//	mnuContext.AppendMenu((MF_ENABLED | MF_STRING), i+1, str);
			//}


			
			//mnuContext.AppendMenu(MF_SEPARATOR);
			//mnuContext.AppendMenu((MF_ENABLED | MF_STRING), ID_VIEW_SOURCE, _T("&View Source"));

			//if(m_pCmdBar != NULL)
			//{
				// NOTE: The CommandBarCtrl in our case is the mainframe's, so the commands
				//  would actually go to the main frame if we don't specify TPM_RETURNCMD.
				//  In the main frame's message map, if we don't specify
				//  CHAIN_MDI_CHILD_COMMANDS, we are not going to see those command
				//  messages. We have 2 choices here - either specify TPM_RETURNCMD,
				//  then send/post the message to our window, or don't specify
				//  TPM_RETURNCMD, and be sure to have CHAIN_MDI_CHILD_COMMANDS
				//  in the main frame's message map.

				//m_pCmdBar->TrackPopupMenu(mnuContext, TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_TOPALIGN | TPM_VERTICAL,
				//	ptPopup.x, ptPopup.y);
				DWORD nSelection = mnuContext.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_TOPALIGN | TPM_VERTICAL | TPM_RETURNCMD,
					menuPt.x, menuPt.y, hwnd);
				if(nSelection != 0)
				{
					item->CallMenuItem(item, nSelection-1);
//					(*pvMenuItems)[nSelection-1].func();
					//((T)this)->PostMessage(WM_COMMAND, MAKEWPARAM(nSelection, 0));
				}
			//}
		//	else
		//	{
		//		mnuContext.TrackPopupMenuEx(TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_TOPALIGN | TPM_VERTICAL,
		//			ptPopup.x, ptPopup.y, m_hWnd, NULL);
		//	}
			return true;
		}
		else
			return false;
	}
};

#endif //VGMTRANSWINDOW_H_INCLUDED


