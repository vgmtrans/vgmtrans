/////////////////////////////////////////////////////////////////////////////
// TabbedMDISave.cpp - Classes related to "saving" MDI children in the
//   tabbed MDI environment.
//
// Classes:
//   CTabbedMDIChildModifiedItem - 
//      Implements ITabbedMDIChildModifiedItem.
//   CTabbedMDIChildModifiedList - 
//      Implements ITabbedMDIChildModifiedList.
//
// Written by Daniel Bowen (dbowen@es.com)
// Copyright (c) 2004 Daniel Bowen.
//
// This code may be used in compiled form in any way you desire. This
// file may be redistributed by any means PROVIDING it is 
// not sold for profit without the authors written consent, and 
// providing that this notice and the authors name is included.
//
// This file is provided "as is" with no expressed or implied warranty.
// The author accepts no liability if it causes any damage to you or your
// computer whatsoever.
//
// If you find bugs, have suggestions for improvements, etc.,
// please contact the author.
//
// History (Date/Author/Description):
// ----------------------------------
//
// 2005/07/13: Daniel Bowen
// - Namespace qualify the use of more ATL and WTL classes.
//
// 2005/04/14: Daniel Bowen
// - CSaveModifiedItemsDialog -
//   ImageUtil::CreateCheckboxImage now takes an HWND for the list control
//
// 2005/03/15: Daniel Bowen
// - CSaveModifiedItemsDialog -
//   Handle the case when no display name is set for an item - have it show "(New)".
//
// 2005/01/14: Daniel Bowen
// - Fix AutoHideUnusedColumns so that it doesn't try to hide the name column
//
// 2004/08/26: Daniel Bowen
// - Break out checkbox image creation
// - Have CDynamicDialogImpl automatically call ConstructDialogResource
//   after the constructor, but before the dialog is created
//
// 2004/06/28: Daniel Bowen
// - Support hiding the description and/or last modified columns
//   in the "save modified items" dialog.
// - Clean up warnings on level 4
//
// 2004/04/29: Daniel Bowen
// - Original implementation

#define WTL_TABBED_MDI_SAVE_IMPLEMENTATION
#include "TabbedMDISave.h"

#if (_WIN32_WINNT >= 0x0501)
	#include <atltheme.h>
#endif

#if (_ATL_VER < 0x0700)
	#error TabbedMDISave.cpp requires ATL 7.0 or higher
#endif

#include <atlcomtime.h>
#include "ImageUtil.h"

/////////////////////////////////////////////////////////////////////////////
// CTabbedMDIChildModifiedList
CTabbedMDIChildModifiedList::CTabbedMDIChildModifiedList() :
	m_parentItem(NULL)
{
}

HRESULT CTabbedMDIChildModifiedList::FinalConstruct()
{
	return S_OK;
}

void CTabbedMDIChildModifiedList::FinalRelease() 
{
	this->Clear();

	// Don't Release m_parentItem, because
	// we only have a weak reference to it
	m_parentItem = NULL;
}

STDMETHODIMP CTabbedMDIChildModifiedList::get_Item(long index, ITabbedMDIChildModifiedItem** item)
{
	if(item == NULL)
	{
		return E_POINTER;
	}
	HRESULT hr = E_INVALIDARG;

	*item = NULL;
	POSITION pos = this->FindIndex(index);
	if(pos != NULL)
	{
		hr = this->GetAt(pos)->QueryInterface(item);
	}
	return hr;
}

STDMETHODIMP CTabbedMDIChildModifiedList::get_Index(ITabbedMDIChildModifiedItem* item, long* index)
{
	if(item == NULL || index == NULL)
	{
		return E_POINTER;
	}

	*index = -1;

	ATL::CComPtr<IUnknown> punkItemToFind;
	item->QueryInterface(IID_IUnknown, (void**)&punkItemToFind);

	// If we tracked items by their identity IUnknown*, we could do
	// a "Find".  But we don't, so we'll just linearly search the list,
	// and depend on the comparing IUnknown* values for identity.
	// (It's not expected that this method would be called very frequently).
	int currentIndex = 0;
	POSITION pos = this->GetHeadPosition();
	while(pos != NULL)
	{
		ATL::CComPtr<IUnknown> punkItem;
		this->GetNext(pos)->QueryInterface(IID_IUnknown, (void**)&punkItem);
		if(punkItemToFind == punkItem)
		{
			*index = currentIndex;
			pos = NULL;
		}
		else
		{
			++currentIndex;
		}
	}

	return ((*index) >= 0) ? S_OK : E_INVALIDARG;
}

STDMETHODIMP CTabbedMDIChildModifiedList::get_Count(long* count)
{
	if(count == NULL)
	{
		return E_POINTER;
	}
	*count = (long)this->GetCount();
	return S_OK;
}

STDMETHODIMP CTabbedMDIChildModifiedList::AddNew(
	const wchar_t* name, const wchar_t* displayName, const wchar_t* description,
	DATE lastModified, HICON icon,
	ITabbedMDIChildModifiedItem** item)
{
	HRESULT hr = E_FAIL;

	ATL::CComObject<CTabbedMDIChildModifiedItem>* newItem = NULL;
	hr = ATL::CComObject<CTabbedMDIChildModifiedItem>::CreateInstance(&newItem);
	if(newItem != NULL)
	{
		newItem->AddRef();

		HWND window = NULL;
		if(m_parentItem)
		{
			m_parentItem->get_Window(&window);
		}

		hr = newItem->InitNew(window, name, displayName, description, lastModified, icon);
		if(SUCCEEDED(hr))
		{
			ATL::CComPtr<ITabbedMDIChildModifiedItem> modifiedItem;
			hr = newItem->QueryInterface(&modifiedItem);

			this->AddTail(modifiedItem);

			// Set the parent list as ourselves, but only after
			// we really have added it to our collection of items
			modifiedItem->putref_ParentList(this);

			if(item != NULL)
			{
				*item = modifiedItem.Detach();
			}
		}

		newItem->Release();
	}

	return hr;
}

STDMETHODIMP CTabbedMDIChildModifiedList::Insert(
	long index, ITabbedMDIChildModifiedItem* item)
{
	HRESULT hr = E_INVALIDARG;

	POSITION pos = this->FindIndex(index);
	if(pos != NULL)
	{
		this->InsertBefore(pos, item);
		hr = S_OK;
	}
	else
	{
		this->AddTail(item);
		hr = S_OK;
	}

	if(SUCCEEDED(hr))
	{
		// Set the parent list as ourselves, but only after
		// we really have added it to our collection of items
		item->putref_ParentList(this);
	}
	return hr;
}

STDMETHODIMP CTabbedMDIChildModifiedList::InsertList(
	long index, ITabbedMDIChildModifiedList* list)
{
	if(list == NULL)
	{
		return E_INVALIDARG;
	}

	HRESULT hr = S_OK;

	long count = 0;
	list->get_Count(&count);

	if(index < 0)
	{
		index = count;
	}

	for(long i=0; i<count; ++i)
	{
		ATL::CComPtr<ITabbedMDIChildModifiedItem> item;
		list->get_Item(i, &item);
		
		this->Insert(index + i, item);
	}

	return hr;
}

STDMETHODIMP CTabbedMDIChildModifiedList::Remove(
	long index, ITabbedMDIChildModifiedItem** item)
{
	HRESULT hr = E_INVALIDARG;

	POSITION pos = this->FindIndex(index);
	if(pos != NULL)
	{
		ATL::CComPtr<ITabbedMDIChildModifiedItem> oldItem(this->GetAt(pos));
		this->RemoveAt(pos);
		if(oldItem)
		{
			oldItem->putref_ParentList(NULL);

			if(item != NULL)
			{
				*item = oldItem.Detach();
			}
		}
	}
	return hr;
}

STDMETHODIMP CTabbedMDIChildModifiedList::Clear()
{
	POSITION pos = this->GetHeadPosition();
	while(pos != NULL)
	{
		ATL::CComPtr<ITabbedMDIChildModifiedItem> item(this->GetNext(pos));
		if(item)
		{
			item->putref_ParentList(NULL);
		}
	}

	this->RemoveAll();
	return S_OK;
}

STDMETHODIMP CTabbedMDIChildModifiedList::get_ParentItem(ITabbedMDIChildModifiedItem** item)
{
	ATLASSERT(item != NULL);
	if(item == NULL)
	{
		return E_POINTER;
	}
	*item = m_parentItem;
	if(m_parentItem)
	{
		m_parentItem->AddRef();
	}
	return S_OK;
}

// Methods not exposed over iterface:
STDMETHODIMP CTabbedMDIChildModifiedList::putref_ParentItem(ITabbedMDIChildModifiedItem* item)
{
	m_parentItem = item;
	return S_OK;
}


/////////////////////////////////////////////////////////////////////////////
// CTabbedMDIChildModifiedItem
CTabbedMDIChildModifiedItem::CTabbedMDIChildModifiedItem() :
	m_window(NULL),
	m_lastModified(0),
	m_icon(NULL),
	m_parentList(NULL)
{
}

HRESULT CTabbedMDIChildModifiedItem::FinalConstruct()
{
	return S_OK;
}

void CTabbedMDIChildModifiedItem::FinalRelease() 
{
	if(m_icon)
	{
		::DestroyIcon(m_icon);
		m_icon = NULL;
	}

	// Don't Release m_parentList, because
	// we only have a weak reference to it
	m_parentList = NULL;
	m_window = NULL;

	m_name.Empty();
	m_displayName.Empty();
	m_description.Empty();
	m_userData.Release();
	m_subItems.Release();
}

STDMETHODIMP CTabbedMDIChildModifiedItem::get_Window(
	HWND* window)
{
	ATLASSERT(window != NULL);
	if (window == NULL)
	{
		return E_POINTER;
	}
	*window = m_window;
	return S_OK;
}
STDMETHODIMP CTabbedMDIChildModifiedItem::put_Window(
	HWND window)
{
	m_window = window;
	return S_OK;
}

STDMETHODIMP CTabbedMDIChildModifiedItem::get_Name(
	BSTR* name)
{
	return m_name.CopyTo(name);
}
STDMETHODIMP CTabbedMDIChildModifiedItem::put_Name(
	const wchar_t* name)
{
	m_name = name;
	return S_OK;
}

STDMETHODIMP CTabbedMDIChildModifiedItem::get_DisplayName(
	BSTR* displayName)
{
	return m_displayName.CopyTo(displayName);
}
STDMETHODIMP CTabbedMDIChildModifiedItem::put_DisplayName(
	const wchar_t* displayName)
{
	m_displayName = displayName;
	return S_OK;
}

STDMETHODIMP CTabbedMDIChildModifiedItem::get_Description(
	BSTR* description)
{
	return m_description.CopyTo(description);
}
STDMETHODIMP CTabbedMDIChildModifiedItem::put_Description(
	const wchar_t* description)
{
	m_description = description;
	return S_OK;
}

STDMETHODIMP CTabbedMDIChildModifiedItem::get_LastModifiedUTC(
	DATE* lastModified)
{
	ATLASSERT(lastModified != NULL);
	if (lastModified == NULL)
	{
		return E_POINTER;
	}
	*lastModified = m_lastModified;
	return S_OK;
}
STDMETHODIMP CTabbedMDIChildModifiedItem::put_LastModifiedUTC(
	DATE lastModified)
{
	m_lastModified = lastModified;
	return S_OK;
}

STDMETHODIMP CTabbedMDIChildModifiedItem::get_Icon(
	HICON* icon)
{
	ATLASSERT(icon != NULL);
	if (icon == NULL)
	{
		return E_POINTER;
	}
	*icon = m_icon;
	return S_OK;
}
STDMETHODIMP CTabbedMDIChildModifiedItem::put_Icon(
	HICON icon)
{
	if(m_icon)
	{
		::DestroyIcon(m_icon);
		m_icon = NULL;
	}
	m_icon = ::CopyIcon(icon);
	return S_OK;
}

STDMETHODIMP CTabbedMDIChildModifiedItem::get_UserData(
	IUnknown** userData)
{
	return m_userData.CopyTo(userData);
}
STDMETHODIMP CTabbedMDIChildModifiedItem::putref_UserData(
	IUnknown* userData)
{
	m_userData = userData;
	return S_OK;
}

STDMETHODIMP CTabbedMDIChildModifiedItem::get_ParentList(
	ITabbedMDIChildModifiedList** parentList)
{
	ATLASSERT(parentList != NULL);
	if(parentList == NULL)
	{
		return E_POINTER;
	}
	*parentList = m_parentList;
	if(m_parentList)
	{
		m_parentList->AddRef();
	}
	return S_OK;
}
STDMETHODIMP CTabbedMDIChildModifiedItem::putref_ParentList(
	ITabbedMDIChildModifiedList* parentList)
{
	if(	m_parentList != NULL && parentList != NULL &&
		m_parentList != parentList)
	{
		// Neither the current nor the new parent list is NULL.
		// Remove ourselves from the old parent (?)
		// (doing so would mess up the current InsertList)
		//long index = -1;
		//m_parentList->get_Index(this, &index);
		//if(index >= 0)
		//{
		//	m_parentList->Remove(index);
		//}
	}

	m_parentList = parentList;
	return S_OK;
}

STDMETHODIMP CTabbedMDIChildModifiedItem::get_SubItems(
	ITabbedMDIChildModifiedList** subItems)
{
	// The first time they ask for the sub items, we'll create it.
	HRESULT hr = S_OK;

	if(m_subItems == NULL)
	{
		ATL::CComObject<CTabbedMDIChildModifiedList>* newSubItems = NULL;
		hr = ATL::CComObject<CTabbedMDIChildModifiedList>::CreateInstance(&newSubItems);
		if(newSubItems != NULL)
		{
			newSubItems->AddRef();

			newSubItems->putref_ParentItem(this);

			m_subItems = newSubItems;

			newSubItems->Release();
		}
	}

	return m_subItems.CopyTo(subItems);
}

STDMETHODIMP CTabbedMDIChildModifiedItem::CopyTo(
	ITabbedMDIChildModifiedItem* destination)
{
	if(destination == NULL)
	{
		return E_INVALIDARG;
	}

	destination->put_Window(m_window);
	destination->put_Name(m_name);
	destination->put_DisplayName(m_displayName);
	destination->put_Description(m_description);
	destination->put_LastModifiedUTC(m_lastModified);
	destination->put_Icon(m_icon);
	destination->putref_UserData(m_userData);

	// The destination keeps its current parent
	//destination->putref_ParentList(m_parentList);

	ATL::CComPtr<ITabbedMDIChildModifiedList> subItems;
	destination->get_SubItems(&subItems);
	if(subItems)
	{
		subItems->Clear();

		if(m_subItems)
		{
			subItems->InsertList(-1, m_subItems);
		}
	}

	return S_OK;
}

// Methods not exposed over iterface

STDMETHODIMP CTabbedMDIChildModifiedItem::InitNew(HWND window,
	const wchar_t* name, const wchar_t* displayName, const wchar_t* description,
	DATE lastModified, HICON icon)
{
	this->put_Window(window);
	this->put_Name(name);
	this->put_DisplayName(displayName);
	this->put_Description(description);
	this->put_LastModifiedUTC(lastModified);
	this->put_Icon(icon);

	return S_OK;
}


/////////////////////////////////////////////////////////////////////////////
// Global functions
HRESULT CreateTabbedMDIChildModifiedList(ITabbedMDIChildModifiedList** list)
{
	HRESULT hr = E_NOINTERFACE;

	ATL::CComObject<CTabbedMDIChildModifiedList>* newItemList = NULL;
	hr = ATL::CComObject<CTabbedMDIChildModifiedList>::CreateInstance(&newItemList);
	if(newItemList != NULL)
	{
		hr = newItemList->QueryInterface(list);
	}

	return hr;
}

HRESULT CreateTabbedMDIChildModifiedItem(HWND window,
	const wchar_t* name, const wchar_t* displayName, const wchar_t* description,
	DATE lastModified, HICON icon,
	ITabbedMDIChildModifiedItem** item)
{
	HRESULT hr = E_NOINTERFACE;

	ATL::CComObject<CTabbedMDIChildModifiedItem>* newItem = NULL;
	hr = ATL::CComObject<CTabbedMDIChildModifiedItem>::CreateInstance(&newItem);
	if(newItem != NULL)
	{
		newItem->AddRef();

		hr = newItem->InitNew(window, name, displayName, description, lastModified, icon);
		if(SUCCEEDED(hr))
		{
			hr = newItem->QueryInterface(item);
		}

		newItem->Release();
	}

	return hr;
}

HRESULT CreateEmptyTabbedMDIChildModifiedItem(ITabbedMDIChildModifiedItem** item)
{
	HRESULT hr = E_NOINTERFACE;

	ATL::CComObject<CTabbedMDIChildModifiedItem>* newItem = NULL;
	hr = ATL::CComObject<CTabbedMDIChildModifiedItem>::CreateInstance(&newItem);
	if(newItem != NULL)
	{
		hr = newItem->QueryInterface(item);
	}

	return hr;
}

/////////////////////////////////////////////////////////////////////////////
// CSaveModifiedItemsDialog
#include "commctrl.h"

CSaveModifiedItemsDialog::CSaveModifiedItemsDialog(ITabbedMDIChildModifiedList* list, bool canCancel) :
	m_modifiedList(list),
	m_canCancel(canCancel),
	m_haveAtLeastOneModifiedDate(false),
	m_list(this, 1),
	m_dialogIcon(NULL),
	m_imageUnchecked(-1),
	m_imageChecked(-1),
	m_imageIndeterminate(-1),
	m_trackColumnWidth(0),
	m_trackColumnIndex(-1),
	m_lastVisibleColumn(eColumn_Last)
{
	for(int i=0; i<eColumn_Count; ++i)
	{
		m_showColumn[i] = true;
	}
}

CSaveModifiedItemsDialog::~CSaveModifiedItemsDialog()
{
}

bool CSaveModifiedItemsDialog::HideColumn(ColumnIndex column)
{
	ATLASSERT(((!m_header.IsWindow()) || (m_header.IsWindow() && m_header.GetItemCount() < 1)) &&
		"Please call this before InitializeColumns().");
	if(column < 0 || column > eColumn_Last || column == eColumn_Name)
	{
		ATLASSERT(0 && "Invalid column index");
		return false;
	}

	m_showColumn[column] = false;

	if(column == m_lastVisibleColumn)
	{
		// Find the new last visible column
		while((m_lastVisibleColumn > 0) && !m_showColumn[m_lastVisibleColumn])
		{
			m_lastVisibleColumn = (ColumnIndex)((int)m_lastVisibleColumn-1);
		}
	}
	return true;
}

LRESULT CSaveModifiedItemsDialog::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
	CenterWindow(GetParent());

	this->InitializeControls();
	this->InitializeValues();

	// NOTE: We need to do this init after InitializeValues, in
	//  case the default size of any control changes
	this->DlgResize_Init(true, true, WS_THICKFRAME | WS_CLIPCHILDREN);

	return 1;		// Let the dialog manager set initial focus
}

LRESULT CSaveModifiedItemsDialog::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{
	LONG dwListStyle = m_list.GetWindowLong(GWL_STYLE);
	if((dwListStyle & LVS_SHAREIMAGELISTS) == LVS_SHAREIMAGELISTS)
	{
		// We're responsible for cleaning up the list view's image list
		if(!m_images.IsNull())
		{
			m_images.Destroy();
		}
	}

	if(!m_stateImages.IsNull())
	{
		m_stateImages.Destroy();
	}

	if(m_dialogIcon != NULL)
	{
		::DestroyIcon(m_dialogIcon);
		m_dialogIcon = NULL;
	}

	if(m_list.IsWindow())
	{
		m_list.UnsubclassWindow();
	}

	// Be sure others see the message (especially DefWindowProc)
	bHandled = FALSE;

	return 0;
}

LRESULT CSaveModifiedItemsDialog::OnYes(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	if(m_modifiedList)
	{
		int count = m_list.GetItemCount();
		for(int i=0; i<count; ++i)
		{
			CheckState checkState = this->GetTristateCheckState(i);
			if(checkState == eCheckState_Unchecked)
			{
				ATL::CComQIPtr<ITabbedMDIChildModifiedItem> item(this->GetIUnknownForItem(i));
				if(item)
				{
					ATL::CComPtr<ITabbedMDIChildModifiedList> parentList;
					item->get_ParentList(&parentList);
					if(parentList)
					{
						long index = -1;
						parentList->get_Index(item, &index);
						if(index >= 0)
						{
							parentList->Remove(index);
						}
					}
				}
			}
		}
	}

	this->EndDialog(wID);
	return 0;
}

LRESULT CSaveModifiedItemsDialog::OnEndDialog(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	this->EndDialog(wID);
	return 0;
}

LRESULT CSaveModifiedItemsDialog::OnListViewInsertItem(int /*idCtrl*/, LPNMHDR pnmh, BOOL& bHandled)
{
	bHandled = TRUE;
	LPNMLISTVIEW pnmLV = (LPNMLISTVIEW)pnmh;
	if(pnmLV != NULL)
	{
		// It'd be nice if they set the lParam of NMLISTVIEW, but they don't.
		// Only iItem is valid
		//LPARAM lParam = pnmLV->lParam;
		//if(lParam != NULL)
		//{
		//	// Keep an AddRef around for the item, and Release in OnDeleteItem
		//	((IUnknown*)lParam)->AddRef();
		//}
		IUnknown* punk = this->GetIUnknownForItem(pnmLV->iItem);
		if(punk)
		{
			// Keep an AddRef around for the item, and Release in OnDeleteItem
			punk->AddRef();
		}
	}
	return 0;
}

LRESULT CSaveModifiedItemsDialog::OnListViewDeleteItem(int /*idCtrl*/, LPNMHDR pnmh, BOOL& bHandled)
{
	bHandled = TRUE;
	LPNMLISTVIEW pnmLV = (LPNMLISTVIEW)pnmh;
	if(pnmLV != NULL)
	{
		LPARAM lParam = pnmLV->lParam;
		if(lParam != NULL)
		{
			((IUnknown*)lParam)->Release();
			pnmLV->lParam = 0;
		}
	}
	return 0;
}

LRESULT CSaveModifiedItemsDialog::OnListViewKeyDownToToggleCheck(int /*idCtrl*/, LPNMHDR pnmh, BOOL& bHandled)
{
	NMLVKEYDOWN* keyDown = (NMLVKEYDOWN*)pnmh;
	if(keyDown && keyDown->wVKey == VK_SPACE)
	{
		int item = m_list.GetNextItem(-1, LVNI_FOCUSED);
		if(item != -1  && ::GetKeyState(VK_CONTROL) >= 0)
		{
			this->ToggleCheckState(item);
		}
	}
	bHandled = FALSE;
	return 0;
}

LRESULT CSaveModifiedItemsDialog::OnListViewClickToToggleCheck(int /*idCtrl*/, LPNMHDR pnmh, BOOL& bHandled)
{
	NMITEMACTIVATE* itemActivate = (NMITEMACTIVATE*)pnmh;
	if(itemActivate)
	{
		LVHITTESTINFO lvh = { 0 };
		lvh.pt = itemActivate->ptAction;
		if(m_list.HitTest(&lvh) != -1 && lvh.flags == LVHT_ONITEMSTATEICON && ::GetKeyState(VK_CONTROL) >= 0)
		{
			this->ToggleCheckState(lvh.iItem);
		}
	}
	bHandled = FALSE;
	return 0;
}

LRESULT CSaveModifiedItemsDialog::OnListViewEraseBackground(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
	// Erase the background in OnListViewPaint
	return 1;
}

LRESULT CSaveModifiedItemsDialog::OnListViewPaint(UINT uMsg, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
	if(wParam != NULL)
	{
		WTL::CMemDC memdc((HDC)wParam, NULL);

		//memdc.FillSolidRect(&memdc.m_rc, ::GetSysColor(COLOR_WINDOW));
		m_list.DefWindowProc(WM_ERASEBKGND, (WPARAM)memdc.m_hDC, 0);
		m_list.DefWindowProc(uMsg, (WPARAM)memdc.m_hDC, 0);

		if(m_header.IsWindow())
		{
			m_header.SendMessage(WM_PAINT, (WPARAM)memdc.m_hDC, 0);
			m_header.ValidateRect(&memdc.m_rc);
		}
	}
	else
	{
		WTL::CPaintDC dc(m_list);
		WTL::CMemDC memdc(dc.m_hDC, &dc.m_ps.rcPaint);

		//memdc.FillSolidRect(&dc.m_ps.rcPaint, ::GetSysColor(COLOR_WINDOW));
		m_list.DefWindowProc(WM_ERASEBKGND, (WPARAM)memdc.m_hDC, 0);
		m_list.DefWindowProc(uMsg, (WPARAM)memdc.m_hDC, 0);

		if(m_header.IsWindow())
		{
			m_header.SendMessage(WM_PAINT, (WPARAM)memdc.m_hDC, 0);
			m_header.ValidateRect(&dc.m_ps.rcPaint);
		}
	}
	return 0;
}

//LRESULT CSaveModifiedItemsDialog::OnHeaderPaint(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
//{
//	if( wParam != NULL )
//	{
//		WTL::CMemDC memdc((HDC)wParam, NULL);

//		memdc.FillSolidRect(&memdc.m_rc, ::GetSysColor(COLOR_BTNFACE));
//		m_header.DefWindowProc( uMsg, (WPARAM)memdc.m_hDC, 0);
//	}
//	else
//	{
//		WTL::CPaintDC dc(m_header);
//		WTL::CMemDC memdc(dc.m_hDC, &dc.m_ps.rcPaint);

//		memdc.FillSolidRect(&dc.m_ps.rcPaint, ::GetSysColor(COLOR_BTNFACE));
//		m_header.DefWindowProc( uMsg, (WPARAM)memdc.m_hDC, 0);
//	}
//	return 0;
//}

LRESULT CSaveModifiedItemsDialog::OnHeaderBeginTrack(int /*idCtrl*/, LPNMHDR pnmh, BOOL& bHandled)
{
	NMHEADER* headerInfo = (NMHEADER*)pnmh;
	if(headerInfo)
	{
		m_trackColumnIndex = headerInfo->iItem;
		m_trackColumnWidth = m_list.GetColumnWidth(m_trackColumnIndex);

		if(	(m_trackColumnIndex < 0) ||
			(m_trackColumnIndex >= m_lastVisibleColumn) ||
			!m_showColumn[m_trackColumnIndex])
		{
			// Don't allow resizing on the last column,
			// or on a column we are hiding
			m_trackColumnIndex = -1;
			bHandled = TRUE;
			return TRUE;
		}
	}
	bHandled = FALSE;
	return 0;
}

LRESULT CSaveModifiedItemsDialog::OnHeaderTrack(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& bHandled)
{
	// NOTE: See http://support.microsoft.com/?kbid=183258
	//  "INFO: HDN_TRACK Notifications and Full Window Drag Style"
	//  We only get this if the HDS_FULLDRAG style is not set
	//  (in which case, we won't get HDN_ITEMCHANGING).

	// If HDS_FULLDRAG is NOT set, the list view won't show the contents
	// as the user is dragging the divider to resize a column.
	// So we won't update the column widths as they drag,
	// but will in "End Track" when they're done

	bHandled = FALSE;
	return 0;
}

LRESULT CSaveModifiedItemsDialog::OnHeaderEndTrack(int /*idCtrl*/, LPNMHDR pnmh, BOOL& bHandled)
{
	if((m_header.GetStyle() & HDS_FULLDRAG) == 0)
	{
		// HDS_FULLDRAG is not set, so the contents aren't updated as they
		// drag the column header divider until they let up on the mouse.
		NMHEADER* headerInfo = (NMHEADER*)pnmh;
		if(	headerInfo && 
			headerInfo->pitem &&
			(headerInfo->pitem->mask & HDI_WIDTH) == HDI_WIDTH)
		{
			if(m_trackColumnIndex == headerInfo->iItem)
			{
				int columnRight = headerInfo->iItem + 1;
				// Find the first column to the right that isn't being hidden.
				while((columnRight < m_lastVisibleColumn) && !m_showColumn[columnRight])
				{
					++columnRight;
				}

				if(columnRight <= m_lastVisibleColumn)
				{
					int widthLeft = headerInfo->pitem->cxy;
					int widthRight = m_list.GetColumnWidth(columnRight);
					int newWidth = widthRight - (widthLeft - m_trackColumnWidth);
					if((widthLeft > eMinimumColumnWidth) && (newWidth > eMinimumColumnWidth))
					{
						// TODO: When making the right column bigger before the
						//  left column has gotten smaller, the scroll bars flash
						//  visible.  Do something to prevent that.
						m_list.SetColumnWidth(columnRight, newWidth);

						m_trackColumnWidth = widthLeft;
					}
					else
					{
						headerInfo->pitem->cxy = m_trackColumnWidth;
					}
				}
			}
		}
	}

	m_list.Invalidate();
	m_trackColumnIndex = -1;
	m_trackColumnWidth = 0;

	bHandled = FALSE;
	return 0;
}

LRESULT CSaveModifiedItemsDialog::OnHeaderItemChanging(int /*idCtrl*/, LPNMHDR pnmh, BOOL& bHandled)
{
	if((m_header.GetStyle() & HDS_FULLDRAG) == HDS_FULLDRAG)
	{
		// HDS_FULLDRAG is set, so the contents are updated as they
		// drag the column header divider.
		NMHEADER* headerInfo = (NMHEADER*)pnmh;
		if(	headerInfo && 
			headerInfo->pitem &&
			(headerInfo->pitem->mask & HDI_WIDTH) == HDI_WIDTH)
		{
			if(m_trackColumnIndex == headerInfo->iItem)
			{
				int columnRight = headerInfo->iItem + 1;
				// Find the first column to the right that isn't being hidden.
				while((columnRight < m_lastVisibleColumn) && !m_showColumn[columnRight])
				{
					++columnRight;
				}

				if(columnRight <= m_lastVisibleColumn)
				{
					int widthLeft = headerInfo->pitem->cxy;
					int widthRight = m_list.GetColumnWidth(columnRight);
					int newWidth = widthRight - (widthLeft - m_trackColumnWidth);
					if((widthLeft > eMinimumColumnWidth) && (newWidth > eMinimumColumnWidth))
					{
						// TODO: When making the right column bigger before the
						//  left column has gotten smaller, the scroll bars flash
						//  visible.  Do something to prevent that.
						m_list.SetColumnWidth(columnRight, newWidth);

						m_trackColumnWidth = widthLeft;
					}
					else
					{
						// Don't allow the change
						bHandled = TRUE;
						return TRUE;
					}
				}
			}
		}
	}
	bHandled = FALSE;
	return 0;
}

LRESULT CSaveModifiedItemsDialog::OnHeaderDividerDoubleClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& bHandled)
{
	// TODO: Deal with this (and with Ctrl + NumPad+)
	bHandled = FALSE;
	return 0;
}

// CDialogResize overrides
void CSaveModifiedItemsDialog::DlgResize_UpdateLayout(int cxWidth, int cyHeight)
{
	//m_list.SetRedraw(FALSE);
	resizeClass::DlgResize_UpdateLayout(cxWidth, cyHeight);
	if(m_list.IsWindow())
	{
		// Adjust width of the description column to account for resizing the dialog
		CRect rcList;
		m_list.GetClientRect(&rcList);

		int headerCount = m_header.GetItemCount();
		int columnWidths = 0;
		for(int i=0; i<headerCount; ++i)
		{
			columnWidths += m_list.GetColumnWidth(i);
		}

		int difference = (rcList.Width() - columnWidths);
		if(difference != 0)
		{
			// Make up the difference by resizing the description column (if its visible).
			// If the description column isn't visible, make up the difference with the name column.
			int columnIndexToMakeUpDifference = eColumn_Description;
			if(m_showColumn[eColumn_Description])
			{
				columnIndexToMakeUpDifference = eColumn_Description;
			}
			else
			{
				columnIndexToMakeUpDifference = eColumn_Name;
			}

			int columnWidth = m_list.GetColumnWidth(columnIndexToMakeUpDifference);
			columnWidth += difference;
			if(columnWidth > eMinimumColumnWidth)
			{
				m_list.SetColumnWidth(columnIndexToMakeUpDifference, columnWidth);
			}
			else
			{
				m_list.SetColumnWidth(columnIndexToMakeUpDifference, eMinimumColumnWidth);
			}

			// TODO: When resizing the dialog smaller, the scroll bars
			//  flash visible.  Do something about that.
		}
	}
	//m_list.SetRedraw(TRUE);
	//m_list.Invalidate();
}

bool CSaveModifiedItemsDialog::ConstructDialogResource(void)
{
// For a smaller dialog:
//IDD_SAVEMODIFIEDFILES DIALOGEX 0, 0, 262, 114
//STYLE DS_SETFONT | DS_FIXEDSYS | WS_POPUP | WS_CLIPCHILDREN | WS_CAPTION | 
//    WS_SYSMENU | WS_THICKFRAME
//EXSTYLE WS_EX_CONTROLPARENT
//CAPTION "Save Modified Files"
//FONT 8, "MS Shell Dlg", 400, 0, 0x1
//BEGIN
//    LTEXT           "Do you want to save changes to the modified files?",
//                    IDC_STATIC,7,7,180,8
//    CONTROL         "",IDC_LIST_FILES,"SysListView32",LVS_REPORT | 
//                    LVS_ALIGNLEFT | LVS_NOSORTHEADER | WS_BORDER | 
//                    WS_TABSTOP,7,20,248,66
//    DEFPUSHBUTTON   "&Yes",IDYES,95,93,50,14
//    PUSHBUTTON      "&No",IDNO,150,93,50,14
//    PUSHBUTTON      "Cancel",IDCANCEL,205,93,50,14
//END

// The dialog template we'll use:
//IDD_SAVEMODIFIEDFILES DIALOGEX 0, 0, 300, 150
//STYLE DS_SETFONT | DS_FIXEDSYS | WS_POPUP | WS_CLIPCHILDREN | WS_CAPTION | 
//    WS_SYSMENU | WS_THICKFRAME
//EXSTYLE WS_EX_CONTROLPARENT
//CAPTION "Save Modified Files"
//FONT 8, "MS Shell Dlg", 400, 0, 0x1
//BEGIN
//    LTEXT           "Do you want to save changes to the modified files?",
//                    IDC_STATIC,7,7,180,8
//    CONTROL         "",IDC_LIST_FILES,"SysListView32",LVS_REPORT | 
//                    LVS_ALIGNLEFT | LVS_NOSORTHEADER | WS_BORDER | 
//                    WS_TABSTOP,7,20,286,102
//    DEFPUSHBUTTON   "&Yes",IDYES,133,129,50,14
//    PUSHBUTTON      "&No",IDNO,188,129,50,14
//    PUSHBUTTON      "Cancel",IDCANCEL,243,129,50,14
//END

	DynamicDialog::DynamicDialogItemSize itemSizeYes, itemSizeNo, itemSizeCancel;
	DWORD visibleStyle_Cancel = 0;
	if(m_canCancel)
	{
		itemSizeYes.Set(133,129,50,14);
		itemSizeNo.Set(188,129,50,14);
		itemSizeCancel.Set(243,129,50,14);

		visibleStyle_Cancel = WS_VISIBLE;
	}
	else
	{
		itemSizeYes.Set(188,129,50,14);
		itemSizeNo.Set(243,129,50,14);
		itemSizeCancel.Set(243,129,50,14);
	}

	bool success = m_dynamicDialogTemplate.Create(
		300, 150,
		(WS_POPUP | WS_CAPTION | WS_CLIPCHILDREN | WS_SYSMENU | WS_THICKFRAME | DS_3DLOOK | DS_SETFONT),
		WS_EX_CONTROLPARENT,
		L"Save Modified Items", L"MS Shell Dlg", 8);

	if(success)
	{
		// Header text
		m_dynamicDialogTemplate.AddStaticControl(
			(WS_CHILD | WS_VISIBLE | WS_GROUP | SS_LEFT), 0,
			7,7,180,8, (WORD)IDC_STATIC, L"Do you want to save changes to these modified items?");

		// List of items
		m_dynamicDialogTemplate.AddControl(
			(WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER |
			LVS_REPORT | LVS_ALIGNLEFT | LVS_NOSORTHEADER), 0,
			7,20,286,102, _IDC_LIST, L"", L"SysListView32");

		// Yes, No, Cancel buttons
		m_dynamicDialogTemplate.AddButtonControl(
			(WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON), 0,
			itemSizeYes, IDYES, L"&Yes");
		m_dynamicDialogTemplate.AddButtonControl(
			(WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON), 0,
			itemSizeNo, IDNO, L"&No");
		m_dynamicDialogTemplate.AddButtonControl(
			(WS_CHILD | visibleStyle_Cancel | WS_TABSTOP | BS_PUSHBUTTON), 0,
			itemSizeCancel, IDCANCEL, L"Cancel");
	}

	return success;
}

bool CSaveModifiedItemsDialog::InitializeControls(void)
{
	//m_list = this->GetDlgItem(_IDC_LIST);
	m_list.SubclassWindow(this->GetDlgItem(_IDC_LIST));
	m_header = m_list.GetHeader();
	// For now, don't allow full drag or drag drop
	m_header.ModifyStyle((HDS_FULLDRAG | HDS_DRAGDROP), 0);

	// NOTE: See MSDN about custom drawing as to this message.
	//  This avoids getting the text clipped if we change the font
	//  during custom drawing.
	m_list.SendMessage(CCM_SETVERSION, 5, 0);

	// NOTE: Instead of using LVS_EX_CHECKBOXES,
	//  we implement the logic ourselves, because we want
	//  to allow for a partial/indeterminate checkbox.
	//m_list.SetExtendedListViewStyle(
	//	(LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT),
	//	(LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT));
	m_list.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT, LVS_EX_FULLROWSELECT);

	m_images.Create(
		::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON),
		ILC_COLOR32 | ILC_MASK, 4, 4);
	m_list.SetImageList(m_images, LVSIL_SMALL);
	this->CreateDefaultImages();

	m_stateImages.Create(
		::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON),
		ILC_COLOR32 | ILC_MASK, 4, 4);
	m_list.SetImageList(m_stateImages, LVSIL_STATE);
	this->CreateDefaultStateImages();

	this->AutoHideUnusedColumns();

	this->InitializeColumns();

	return true;
}

bool CSaveModifiedItemsDialog::InitializeValues(void)
{
	bool success = true;

	success = this->AddItems(m_modifiedList, 0);

	return success;
}

bool CSaveModifiedItemsDialog::InitializeColumns(void)
{
	CRect rcList;
	m_list.GetClientRect(&rcList);

	int nameWidth = 0;
	int descriptionWidth = 0;
	int lastModifiedWidth = 0;

	if(!m_showColumn[eColumn_Description] && !m_showColumn[eColumn_LastModified])
	{
		nameWidth = rcList.Width();
	}
	else if(!m_showColumn[eColumn_Description])
	{
		nameWidth = ::MulDiv(rcList.Width(), 75, 100);
		lastModifiedWidth = rcList.Width() - nameWidth;
	}
	else if(!m_showColumn[eColumn_LastModified])
	{
		nameWidth = ::MulDiv(rcList.Width(), 35, 100);
		descriptionWidth = rcList.Width() - nameWidth;
	}
	else
	{
		nameWidth = ::MulDiv(rcList.Width(), 30, 100);
		descriptionWidth = ::MulDiv(rcList.Width(), 45, 100);
		lastModifiedWidth = rcList.Width() - (nameWidth + descriptionWidth);
	}


	LVCOLUMN lvColumn = {0};
	lvColumn.mask = (LVCF_FMT | LVCF_WIDTH | LVCF_TEXT);
	lvColumn.fmt = LVCFMT_LEFT;
	lvColumn.cx = nameWidth;
	lvColumn.pszText = _T("Name");
	m_list.InsertColumn(eColumn_Name, &lvColumn);

	lvColumn.mask = (LVCF_FMT | LVCF_WIDTH | LVCF_TEXT);
	lvColumn.fmt = LVCFMT_LEFT;
	lvColumn.cx = descriptionWidth;
	lvColumn.pszText = _T("Description");
	m_list.InsertColumn(eColumn_Description, &lvColumn);

	lvColumn.mask = (LVCF_FMT | LVCF_WIDTH | LVCF_TEXT);
	lvColumn.fmt = LVCFMT_LEFT;
	lvColumn.cx = lastModifiedWidth;
	lvColumn.pszText = _T("Last Modified");
	m_list.InsertColumn(eColumn_LastModified, &lvColumn);

	return true;
}

int CSaveModifiedItemsDialog::AutoHideUnusedColumns(void)
{
	int columnsHidden = 0;
	int columnUseCount[eColumn_Count] = {0};

	this->FindUsedColumns(m_modifiedList, columnUseCount);
	for(int i=0; i<eColumn_Count; ++i)
	{
		if(columnUseCount[i] == 0 && i != (int)eColumn_Name)
		{
			++columnsHidden;
			this->HideColumn((ColumnIndex)i);
		}
	}

	return columnsHidden;
}

bool CSaveModifiedItemsDialog::FindUsedColumns(ITabbedMDIChildModifiedList* list, int columnUseCount[eColumn_Count])
{
	if(list == NULL)
	{
		return false;
	}

	bool success = true;

	long count = 0;
	list->get_Count(&count);
	for(long i=0; i<count; ++i)
	{
		ATL::CComPtr<ITabbedMDIChildModifiedItem> item;
		list->get_Item(i, &item);
		if(item)
		{
			if(m_showColumn[eColumn_Name])
			{
				ATL::CComBSTR displayName;
				item->get_DisplayName(&displayName);
				if(displayName.Length() > 0)
				{
					columnUseCount[eColumn_Name] += 1;
				}
			}
			if(m_showColumn[eColumn_Description])
			{
				ATL::CComBSTR description;
				item->get_Description(&description);
				if(description.Length() > 0)
				{
					columnUseCount[eColumn_Description] += 1;
				}
			}
			if(m_showColumn[eColumn_LastModified])
			{
				DATE lastModified = 0;
				item->get_LastModifiedUTC(&lastModified);
				if(lastModified != 0)
				{
					columnUseCount[eColumn_LastModified] += 1;
				}
			}

			ATL::CComPtr<ITabbedMDIChildModifiedList> subItems;
			item->get_SubItems(&subItems);
			if(subItems)
			{
				this->FindUsedColumns(subItems, columnUseCount);
			}
		}
	}

	return success;
}

bool CSaveModifiedItemsDialog::AddItems(ITabbedMDIChildModifiedList* list, int indent)
{
	if(list == NULL)
	{
		return false;
	}

	bool success = true;

	long count = 0;
	list->get_Count(&count);
	for(long i=0; i<count; ++i)
	{
		ATL::CComPtr<ITabbedMDIChildModifiedItem> item;
		list->get_Item(i, &item);
		if(item)
		{
			ATL::CComBSTR displayName, description;
			item->get_DisplayName(&displayName);
			item->get_Description(&description);

			DATE lastModified = 0;
			item->get_LastModifiedUTC(&lastModified);

			_CSTRING_NS::CString displayNameForItem(displayName);
			_CSTRING_NS::CString descriptionForItem(description);
			_CSTRING_NS::CString lastModifiedForItem = this->FormatLastModifiedDateString(lastModified);

			if(displayNameForItem.GetLength() < 1)
			{
				displayNameForItem = _T("(New)");
			}

			int imageIndex = 0;
			HICON hIcon = NULL;
			item->get_Icon(&hIcon);
			if(hIcon != NULL)
			{
				imageIndex = m_images.AddIcon(hIcon);
			}

			// NOTE: The handler of LVN_INSERTITEM will AddRef,
			// and the handler of LVN_DELETEITEM will Release
			// to be sure we keep the item reference counted appropriately
			// no matter where the InsertItem comes from.
			//IUnknown* punkItem = NULL;
			ATL::CComPtr<IUnknown> punkItem;
			item->QueryInterface(IID_IUnknown, (void**)&punkItem);

			LVITEM lvItem = {0};
			lvItem.mask = (LVIF_TEXT | LVIF_INDENT | LVIF_IMAGE | LVIF_PARAM);
			lvItem.iItem = m_list.GetItemCount();
			lvItem.iSubItem = 0;
			lvItem.pszText = (LPTSTR)(LPCTSTR)displayNameForItem;
			lvItem.iIndent = indent;
			lvItem.iImage = imageIndex;
			lvItem.lParam = (LPARAM)punkItem.p;

			int index = m_list.InsertItem(&lvItem);
			if(index >= 0)
			{
				m_list.SetCheckState(index, TRUE);

				m_list.SetItemText(index, eColumn_Description, descriptionForItem);
				if(lastModifiedForItem.GetLength() > 0)
				{
					m_haveAtLeastOneModifiedDate = true;
					m_list.SetItemText(index, eColumn_LastModified, lastModifiedForItem);
				}
			}

			ATL::CComPtr<ITabbedMDIChildModifiedList> subItems;
			item->get_SubItems(&subItems);
			if(subItems)
			{
				this->AddItems(subItems, (indent + 1));
			}
		}
	}

	return success;
}

_CSTRING_NS::CString CSaveModifiedItemsDialog::FormatLastModifiedDateString(DATE lastModifiedUTC)
{
	_CSTRING_NS::CString lastModifiedString;

	if(lastModifiedUTC != 0)
	{
		SYSTEMTIME lastModifiedUTCSystem = {0};
		::VariantTimeToSystemTime(lastModifiedUTC, &lastModifiedUTCSystem);
		FILETIME lastModifiedUTCFileTime = {0};
		::SystemTimeToFileTime(&lastModifiedUTCSystem, &lastModifiedUTCFileTime);
		FILETIME lastModifiedLocalFileTime = {0};
		::FileTimeToLocalFileTime(&lastModifiedUTCFileTime, &lastModifiedLocalFileTime);

		COleDateTime nowLocal(COleDateTime::GetCurrentTime());
		COleDateTime dateTimeModifiedLocal(lastModifiedLocalFileTime);

		_CSTRING_NS::CString fullDateTimeString = dateTimeModifiedLocal.Format(_T("%#m/%#d/%Y %#I:%M %p"));;
		_CSTRING_NS::CString timeString = dateTimeModifiedLocal.Format(_T("%#I:%M %p"));;

		int yearDifference = (nowLocal.GetYear() - dateTimeModifiedLocal.GetYear());
		int dayOfYearDifference = (nowLocal.GetDayOfYear() - dateTimeModifiedLocal.GetDayOfYear());
		int hourDifference = (nowLocal.GetHour() - dateTimeModifiedLocal.GetHour());
		int minuteDifference = (nowLocal.GetMinute() - dateTimeModifiedLocal.GetMinute());
		int secondDifference = (nowLocal.GetSecond() - dateTimeModifiedLocal.GetSecond());
		if(	yearDifference >= 1 ||
			dayOfYearDifference > 1 ||
			nowLocal < dateTimeModifiedLocal)
		{
			lastModifiedString = fullDateTimeString;
		}
		else if(dayOfYearDifference == 1)
		{
			lastModifiedString.Format(_T("Yesterday, %s"), timeString);
		}
		else if(hourDifference >= 1)
		{
			lastModifiedString.Format(_T("%d hour%s ago"), hourDifference,
				(hourDifference!=1) ? _T("s") : _T(""));
		}
		else if(minuteDifference >= 1)
		{
			lastModifiedString.Format(_T("%d minute%s ago"), minuteDifference,
				(minuteDifference!=1) ? _T("s") : _T(""));
		}
		else if(secondDifference >= 1)
		{
			lastModifiedString.Format(_T("%d second%s ago"), secondDifference,
				(secondDifference!=1) ? _T("s") : _T(""));
		}
		else
		{
			lastModifiedString = fullDateTimeString;
		}
	}

	return lastModifiedString;
}

IUnknown* CSaveModifiedItemsDialog::GetIUnknownForItem(int index)
{
	LVITEM lvi={0};
	lvi.mask = LVIF_PARAM;
	lvi.iItem = index;
	lvi.iSubItem = 0;
	lvi.lParam = 0;
	if(m_list.GetItem(&lvi))
	{
		return (IUnknown*) lvi.lParam;
	}

	return NULL;
}

int CSaveModifiedItemsDialog::FindItemIndex(ITabbedMDIChildModifiedItem* item)
{
	if(item == NULL)
	{
		return -1;
	}

	ATL::CComPtr<IUnknown> punkNode;
	item->QueryInterface(IID_IUnknown, (void**)&punkNode);
	if(punkNode)
	{
		LVFINDINFO findInfo = {0};
		findInfo.flags = LVFI_PARAM;
		findInfo.lParam = (LPARAM)(IUnknown*)punkNode.p;

		return m_list.FindItem(&findInfo, -1);
	}

	return -1;
}

int CSaveModifiedItemsDialog::FindParentIndex(int item)
{
	// Find the index of the "parent" based on the indent
	LVITEM lvi = { 0 };
	lvi.iItem = item;
	lvi.iSubItem = 0;
	lvi.mask = LVIF_INDENT;
	m_list.GetItem(&lvi);

	if(lvi.iIndent == 0)
	{
		return -1;
	}

	int parentIndex = -1;
	for(int i=item; (i >= 0) && (parentIndex < 0); --i)
	{
		LVITEM lviParent = { 0 };
		lviParent.iItem = i;
		lviParent.iSubItem = 0;
		lviParent.mask = LVIF_INDENT;
		m_list.GetItem(&lviParent);
		if(lviParent.iIndent < lvi.iIndent)
		{
			// We've hit the first item before the
			// item in question where the indent is less.
			// Treat this as the parent
			parentIndex = i;
		}
	}

	return parentIndex;
}

void CSaveModifiedItemsDialog::ToggleCheckState(int item)
{
	LVITEM lvi = { 0 };
	lvi.iItem = item;
	lvi.iSubItem = 0;
	lvi.mask = (LVIF_INDENT | LVIF_STATE);
	lvi.stateMask = (LVIS_STATEIMAGEMASK | LVIS_SELECTED);
	m_list.GetItem(&lvi);

	CheckState checkState = (CheckState)(lvi.state & LVIS_STATEIMAGEMASK);

	CheckState newCheckState = eCheckState_Checked;
	switch(checkState)
	{
	case eCheckState_Unchecked:
		newCheckState = eCheckState_Checked;
		break;
	case eCheckState_Checked:
		newCheckState = eCheckState_Unchecked;
		break;
	case eCheckState_Indeterminate:
		newCheckState = eCheckState_Checked;
		break;
	}

	if((lvi.state & LVIS_SELECTED) != LVIS_SELECTED)
	{
		// If the item isn't selected, toggle the checkmark on just the item
		this->SetTristateCheckState(item, newCheckState);
	}
	else
	{
		// Otherwise, set the checkmark appropriately on all selected items.
		int itemToCheck = -1;
		while((itemToCheck = m_list.GetNextItem(itemToCheck, LVNI_SELECTED)) != -1)
		{
			this->SetTristateCheckState(itemToCheck, newCheckState);
		}
	}
}

void CSaveModifiedItemsDialog::SetTristateCheckState(int item, CheckState checkState)
{
	LVITEM lvi = { 0 };
	lvi.iItem = item;
	lvi.iSubItem = 0;
	lvi.mask = LVIF_INDENT;
	m_list.GetItem(&lvi);

	int itemToCheck = item;
	bool done = false;

	// Set the check of the item as well as all of the "child" items
	while(!done)
	{
		m_list.SetItemState(itemToCheck, checkState, LVIS_STATEIMAGEMASK);

		itemToCheck = m_list.GetNextItem(itemToCheck, LVNI_ALL);
		if(itemToCheck < 0)
		{
			done = true;
		}
		else
		{
			LVITEM lviToCheck = { 0 };
			lviToCheck.iItem = itemToCheck;
			lviToCheck.iSubItem = 0;
			lviToCheck.mask = LVIF_INDENT;
			m_list.GetItem(&lviToCheck);
			done = (lviToCheck.iIndent <= lvi.iIndent);
		}
	}

	this->UpdateParentCheckState(item, checkState);
}

CSaveModifiedItemsDialog::CheckState CSaveModifiedItemsDialog::GetTristateCheckState(int item)
{
	return (CheckState)m_list.GetItemState(item, LVIS_STATEIMAGEMASK);
}

void CSaveModifiedItemsDialog::UpdateParentCheckState(int item, CheckState checkState)
{
	// Update the "parent" item's checkbox to reflect the state of the descendants.
	// If all of the descendants are checked, then check the parent.
	// If all of the descendants are unchecked, then uncheck the parent.
	// If some of the descendants are checked, and some are unchecked, have
	// the parent's checkbox be indeterminate.
	// If the parent isn't a "root" parent, recursively give the same
	// treatment to all of the ancestors.

	int parentIndex = this->FindParentIndex(item);
	if(parentIndex >= 0)
	{
		LVITEM lviParent = { 0 };
		lviParent.iItem = parentIndex;
		lviParent.iSubItem = 0;
		lviParent.mask = LVIF_INDENT;
		m_list.GetItem(&lviParent);

		int itemToCheck = parentIndex + 1;

		bool checkStateMixed = false;
		bool done = false;
		while(!done)
		{
			LVITEM lviDescendant = { 0 };
			lviDescendant.iItem = itemToCheck;
			lviDescendant.iSubItem = 0;
			lviDescendant.mask = (LVIF_INDENT | LVIF_STATE);
			lviDescendant.stateMask = LVIS_STATEIMAGEMASK;
			BOOL validItem = m_list.GetItem(&lviDescendant);
			if(!validItem)
			{
				done = true;
			}
			else if(lviDescendant.iIndent <= lviParent.iIndent)
			{
				// There's no more possible siblings or descendants,
				// because we've hit our parent's sibling
				done = true;
			}
			else
			{
				CheckState descendantCheckState = (CheckState)(lviDescendant.state & LVIS_STATEIMAGEMASK);
				if(descendantCheckState != checkState)
				{
					checkStateMixed = true;
					done = true;
				}
			}

			if(!done)
			{
				itemToCheck = m_list.GetNextItem(itemToCheck, LVNI_ALL);
				done = (itemToCheck < 0);
			}
		}

		// Now we've visited all of the descendants of the parent.
		// If all of them have the same check state as the item,
		// set the parent's check state to the same.  If the descendants
		// have mixed check states, then set the parent's check
		// state to indeterminate.
		if(checkStateMixed)
		{
			m_list.SetItemState(parentIndex, eCheckState_Indeterminate, LVIS_STATEIMAGEMASK);
		}
		else
		{
			m_list.SetItemState(parentIndex, checkState, LVIS_STATEIMAGEMASK);
		}
		this->UpdateParentCheckState(parentIndex, checkState);
	}
}

void CSaveModifiedItemsDialog::CreateDefaultImages(void)
{
	// IMPORTANT! Win2K and WinXP have the same index for this bitmap.
	//  There's even a standard TB_LOADIMAGES message for toolbar that loads it,
	//  along with standard image indexes into the bitmap.
	//  However, instead of creating a toolbar and issuing TB_LOADIMAGES,
	//  we'll load the bitmap directly by its index (120) and
	//  the color mask (192,192,192).
	//  Double check future versions of Windows to make sure
	//  this all works correctly.
	HMODULE hComCtl32 = GetModuleHandle(_T("comctl32.dll"));
	if(hComCtl32)
	{
		HIMAGELIST hCommonToolbar = ImageList_LoadBitmap(hComCtl32, MAKEINTRESOURCE(120), 16, 0, RGB(192,192,192));
		if(hCommonToolbar)
		{
			HICON hFileNew = ImageList_ExtractIcon(NULL, hCommonToolbar, STD_FILENEW);
			HICON hFileSave = ImageList_ExtractIcon(NULL, hCommonToolbar, STD_FILESAVE);

			if(hFileNew)
			{
				int fileNewIndex = m_images.AddIcon(hFileNew);
				ATLASSERT(fileNewIndex == 0);

				::DestroyIcon(hFileNew);
				hFileNew = NULL;
			}

			if(hFileSave)
			{
				int fileSaveIndex = m_images.AddIcon(hFileSave);
				ATLASSERT(fileSaveIndex == 1);

				m_dialogIcon = hFileSave;

				this->SetIcon(m_dialogIcon, ICON_SMALL);
			}

			ImageList_Destroy(hCommonToolbar);
			hCommonToolbar = NULL;
		}
	}
}

void CSaveModifiedItemsDialog::CreateDefaultStateImages(void)
{
	if(!m_stateImages.IsNull())
	{
		WTL::CWindowDC dcScreen(NULL);

		int cx = ::GetSystemMetrics(SM_CXSMICON);
		int cy = ::GetSystemMetrics(SM_CYSMICON);

		m_imageUnchecked     = this->AddCheckStateImage(dcScreen, cx, cy, eCheckState_Unchecked);
		m_imageChecked       = this->AddCheckStateImage(dcScreen, cx, cy, eCheckState_Checked);
		m_imageIndeterminate = this->AddCheckStateImage(dcScreen, cx, cy, eCheckState_Indeterminate);
	}
}

int CSaveModifiedItemsDialog::AddCheckStateImage(HDC dcScreen, int cx, int cy, enum CheckState checkState)
{
	ImageUtil::eCheckbox type = ImageUtil::eCheckboxChecked;
	switch(checkState)
	{
	case eCheckState_Unchecked:
		type = ImageUtil::eCheckboxUnchecked;
		break;
	case eCheckState_Checked:
		type = ImageUtil::eCheckboxChecked;
		break;
	case eCheckState_Indeterminate:
		type = ImageUtil::eCheckboxIndeterminate;
		break;
	default:
		ATLASSERT(0 && "Invalid checkbox type!");
		break;
	}

	int index = -1;

	WTL::CBitmap bitmap = ImageUtil::CreateCheckboxImage(dcScreen, type, cx, cy, RGB(255,0,0), m_list);
	if(!bitmap.IsNull())
	{
		index = m_stateImages.Add(bitmap, RGB(255,0,0));
	}

	return index;
}
