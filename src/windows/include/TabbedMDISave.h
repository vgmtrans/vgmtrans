/////////////////////////////////////////////////////////////////////////////
// TabbedMDISave.h - Classes related to "saving" MDI children in the
//   tabbed MDI environment.
//
// Interfaces:
//   ITabbedMDIChildModifiedItem
//   ITabbedMDIChildModifiedList
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
// 2004/08/26: Daniel Bowen
// - Break out checkbox image creation
// - Have CDynamicDialogImpl automatically call ConstructDialogResource
//   after the constructor, but before the dialog is created
//
// 2004/06/28: Daniel Bowen
// - Support hiding the description and/or last modified columns
//   in the "save modified items" dialog.
//
// 2004/04/29: Daniel Bowen
// - Original implementation

#ifndef __TabbedMDISave_h__
#define __TabbedMDISave_h__

#pragma once

#if _WTL_VER < 0x0710
	#error TabbedMDISave.h requires WTL 7.1 or higher
#endif

// NOTE: You can #define WTL_TABBED_MDI_SAVE_INTERFACE_ONLY
//  before including this file if you only want to pick up the
//  interface definitions.

// Forward Declarations

#ifndef __ITabbedMDIChildModifiedList_FWD_DEFINED__
#define __ITabbedMDIChildModifiedList_FWD_DEFINED__
typedef interface ITabbedMDIChildModifiedList ITabbedMDIChildModifiedList;
#endif 	/* __ITabbedMDIChildModifiedList_FWD_DEFINED__ */

#ifndef __ITabbedMDIChildModifiedItem_FWD_DEFINED__
#define __ITabbedMDIChildModifiedItem_FWD_DEFINED__
typedef interface ITabbedMDIChildModifiedItem ITabbedMDIChildModifiedItem;
#endif 	/* __ITabbedMDIChildModifiedItem_FWD_DEFINED__ */


// Interfaces

MIDL_INTERFACE("A4284D7C-BBD2-4c1b-80E0-AFADA6AE5459")
ITabbedMDIChildModifiedList : public IUnknown
{
public:
	//[propget, id(DISPID_VALUE)]
	//	HRESULT Item([in] long index, [out, retval] ITabbedMDIChildModifiedItem** item);
	virtual HRESULT STDMETHODCALLTYPE get_Item(
		long index,
		ITabbedMDIChildModifiedItem** item) = 0;
	//[propget]
	//	HRESULT Index([in] ITabbedMDIChildModifiedItem* item, [out, retval] long* index);
	virtual HRESULT STDMETHODCALLTYPE get_Index(
		ITabbedMDIChildModifiedItem* item,
		long* index) = 0;
	//[propget]
	//	HRESULT Count([out, retval] long* count);
	virtual HRESULT STDMETHODCALLTYPE get_Count(
		long* count) = 0;
	//[]
	//	HRESULT AddNew(
	//		[in] const wchar_t* name, [in] const wchar_t* displayName, [in] const wchar_t* description,
	//		[in] DATE lastModified, [in] HICON icon,
	//		[out, retval,unique,defaultvalue(NULL)] ITabbedMDIChildModifiedItem** item);
	virtual HRESULT STDMETHODCALLTYPE AddNew(
		const wchar_t* name, const wchar_t* displayName, const wchar_t* description,
		DATE lastModified, HICON icon,
		ITabbedMDIChildModifiedItem** item = NULL) = 0;
	//[]
	//	HRESULT Insert([in] long index, [in] ITabbedMDIChildModifiedItem* item);
	virtual HRESULT STDMETHODCALLTYPE Insert(
		long index, ITabbedMDIChildModifiedItem* item) = 0;
	//[]
	//	HRESULT InsertList([in] long index, [in] ITabbedMDIChildModifiedList* list);
	virtual HRESULT STDMETHODCALLTYPE InsertList(
		long index, ITabbedMDIChildModifiedList* list) = 0;
	//[]
	//	HRESULT Remove([in] long index, [out,retval,unique,defaultvalue(NULL)] ITabbedMDIChildModifiedItem** item = NULL);
	virtual HRESULT STDMETHODCALLTYPE Remove(
		long index, ITabbedMDIChildModifiedItem** item = NULL) = 0;
	//[]
	//	HRESULT Clear();
	virtual HRESULT STDMETHODCALLTYPE Clear() = 0;
	//[propget]
	//	HRESULT ParentItem([out,retval] ITabbedMDIChildModifiedItem** item);
	virtual HRESULT STDMETHODCALLTYPE get_ParentItem(
		ITabbedMDIChildModifiedItem** item) = 0;
};

MIDL_INTERFACE("2CB3E36B-1646-4f4b-ABA7-F42DDD3DF64D")
ITabbedMDIChildModifiedItem : public IUnknown
{
public:
	//[propget]
	//	HRESULT Window([out,retval] HWND* window);
	virtual HRESULT STDMETHODCALLTYPE get_Window(
		HWND* window) = 0;
	//[propput]
	//	HRESULT Window([in] HWND window);
	virtual HRESULT STDMETHODCALLTYPE put_Window(
		HWND window) = 0;
	//[propget]
	//	HRESULT Name([out,retval] BSTR* name);
	virtual HRESULT STDMETHODCALLTYPE get_Name(
		BSTR* name) = 0;
	//[propput]
	//	HRESULT Name([in] const wchar_t* name);
	virtual HRESULT STDMETHODCALLTYPE put_Name(
		const wchar_t* name) = 0;
	//[propget]
	//	HRESULT DisplayName([out,retval] BSTR* displayName);
	virtual HRESULT STDMETHODCALLTYPE get_DisplayName(
		BSTR* displayName) = 0;
	//[propput]
	//	HRESULT DisplayName([in] const wchar_t* displayName);
	virtual HRESULT STDMETHODCALLTYPE put_DisplayName(
		const wchar_t* displayName) = 0;
	//[propget]
	//	HRESULT Description([out,retval] BSTR* description);
	virtual HRESULT STDMETHODCALLTYPE get_Description(
		BSTR* description) = 0;
	//[propput]
	//	HRESULT Description([in] const wchar_t* description);
	virtual HRESULT STDMETHODCALLTYPE put_Description(
		const wchar_t* description) = 0;
	//[propget]
	//	HRESULT LastModifiedUTC([out,retval] DATE* lastModified);
	virtual HRESULT STDMETHODCALLTYPE get_LastModifiedUTC(
		DATE* lastModified) = 0;
	//[propput]
	//	HRESULT LastModifiedUTC([in] DATE lastModified);
	virtual HRESULT STDMETHODCALLTYPE put_LastModifiedUTC(
		DATE lastModified) = 0;
	//[propget]
	//	HRESULT Icon([out,retval] HICON* icon);
	virtual HRESULT STDMETHODCALLTYPE get_Icon(
		HICON* icon) = 0;
	//[propput]
	//	HRESULT Icon([in] HICON icon);
	virtual HRESULT STDMETHODCALLTYPE put_Icon(
		HICON icon) = 0;
	//[propget]
	//	HRESULT UserData([out,retval] IUnknown** userData);
	virtual HRESULT STDMETHODCALLTYPE get_UserData(
		IUnknown** userData) = 0;
	//[propputref]
	//	HRESULT UserData([in] IUnknown* userData);
	virtual HRESULT STDMETHODCALLTYPE putref_UserData(
		IUnknown* userData) = 0;
	//[propget]
	//	HRESULT ParentList([out,retval] ITabbedMDIChildModifiedList** parentList);
	virtual HRESULT STDMETHODCALLTYPE get_ParentList(
		ITabbedMDIChildModifiedList** parentList) = 0;
	//[propputref]
	//	HRESULT ParentList([in] ITabbedMDIChildModifiedList* parentList);
	virtual HRESULT STDMETHODCALLTYPE putref_ParentList(
		ITabbedMDIChildModifiedList* parentList) = 0;
	//[propget]
	//	HRESULT SubItems([out,retval] ITabbedMDIChildModifiedList** subItems);
	virtual HRESULT STDMETHODCALLTYPE get_SubItems(
		ITabbedMDIChildModifiedList** subItems) = 0;
	//[]
	//	HRESULT CopyTo([in] ITabbedMDIChildModifiedItem* destination);
	virtual HRESULT STDMETHODCALLTYPE CopyTo(
		ITabbedMDIChildModifiedItem* destination) = 0;
};

#ifdef WTL_TABBED_MDI_SAVE_IMPLEMENTATION

#ifndef __ATLFRAME_H__
	#error TabbedMDISave.h requires atlframe.h to be included first
#endif
#if (_ATL_VER < 0x0700)
	#error TabbedMDISave.h requires ATL 7.0 or higher
#endif
#if (_WIN32_IE < 0x0501)
	#error TabbedMDISave.h requires _WIN32_IE to be 0x0501 or higher
#endif

#include "DynamicDialogTemplate.h"

// Implementation

/////////////////////////////////////////////////////////////////////////////
// CTabbedMDIChildModifiedList
class CTabbedMDIChildModifiedList :
	public ATL::CComObjectRootEx<ATL::CComMultiThreadModel>,
	public ITabbedMDIChildModifiedList,
	public ATL::CInterfaceList<ITabbedMDIChildModifiedItem>
{
public:
	CTabbedMDIChildModifiedList();

	BEGIN_COM_MAP(CTabbedMDIChildModifiedList)
		COM_INTERFACE_ENTRY(ITabbedMDIChildModifiedList)
	END_COM_MAP()

	DECLARE_PROTECT_FINAL_CONSTRUCT()

	HRESULT FinalConstruct();
	void FinalRelease();

// ITabbedMDIChildModifiedList
public:
	STDMETHOD(get_Item)(long index, ITabbedMDIChildModifiedItem** item);
	STDMETHOD(get_Index)(ITabbedMDIChildModifiedItem* item, long* index);
	STDMETHOD(get_Count)(long* count);
	STDMETHOD(AddNew)(
		const wchar_t* name, const wchar_t* displayName, const wchar_t* description,
		DATE lastModified, HICON icon,
		ITabbedMDIChildModifiedItem** item = NULL);
	STDMETHOD(Insert)(
		long index, ITabbedMDIChildModifiedItem* item);
	STDMETHOD(InsertList)(
		long index, ITabbedMDIChildModifiedList* list);
	STDMETHOD(Remove)(
		long index, ITabbedMDIChildModifiedItem** item = NULL);
	STDMETHOD(Clear)();
	STDMETHOD(get_ParentItem)(ITabbedMDIChildModifiedItem** item);

// Methods not exposed over iterface:
public:
	STDMETHOD(putref_ParentItem)(ITabbedMDIChildModifiedItem* item);

protected:
	// We keep a weak reference to the parent item,
	// and the item keeps a strong reference to us
	// (to avoid circular reference)
	ITabbedMDIChildModifiedItem* m_parentItem;

};

/////////////////////////////////////////////////////////////////////////////
// CTabbedMDIChildModifiedItem
class CTabbedMDIChildModifiedItem :
	public ATL::CComObjectRootEx<ATL::CComMultiThreadModel>,
	public ITabbedMDIChildModifiedItem
{
public:
	CTabbedMDIChildModifiedItem();

	BEGIN_COM_MAP(CTabbedMDIChildModifiedItem)
		COM_INTERFACE_ENTRY(ITabbedMDIChildModifiedItem)
	END_COM_MAP()

	DECLARE_PROTECT_FINAL_CONSTRUCT()

	HRESULT FinalConstruct();
	void FinalRelease();

// ITabbedMDIChildModifiedItem
public:
	STDMETHOD(get_Window)(
		HWND* window);
	STDMETHOD(put_Window)(
		HWND window);

	STDMETHOD(get_Name)(
		BSTR* name);
	STDMETHOD(put_Name)(
		const wchar_t* name);

	STDMETHOD(get_DisplayName)(
		BSTR* displayName);
	STDMETHOD(put_DisplayName)(
		const wchar_t* displayName);

	STDMETHOD(get_Description)(
		BSTR* description);
	STDMETHOD(put_Description)(
		const wchar_t* description);

	STDMETHOD(get_LastModifiedUTC)(
		DATE* lastModified);
	STDMETHOD(put_LastModifiedUTC)(
		DATE lastModified);

	STDMETHOD(get_Icon)(
		HICON* icon);
	STDMETHOD(put_Icon)(
		HICON icon);

	STDMETHOD(get_UserData)(
		IUnknown** userData);
	STDMETHOD(putref_UserData)(
		IUnknown* userData);

	STDMETHOD(get_ParentList)(
		ITabbedMDIChildModifiedList** parentList);
	STDMETHOD(putref_ParentList)(
		ITabbedMDIChildModifiedList* parentList);

	STDMETHOD(get_SubItems)(
		ITabbedMDIChildModifiedList** subItems);

	STDMETHOD(CopyTo)(
		ITabbedMDIChildModifiedItem* destination);

// Methods not exposed over iterface:
public:
	STDMETHOD(InitNew)(HWND window,
		const wchar_t* name, const wchar_t* displayName, const wchar_t* description,
		DATE lastModified, HICON icon);

protected:
	HWND     m_window;
	ATL::CComBSTR m_name;
	ATL::CComBSTR m_displayName;
	ATL::CComBSTR m_description;
	DATE     m_lastModified;
	HICON    m_icon;
	ATL::CComPtr<IUnknown> m_userData;

	// We keep a strong reference to the sub item list,
	// and the sub item list keeps a weak reference to us
	// (to avoid circular reference)
	ATL::CComPtr<ITabbedMDIChildModifiedList> m_subItems;

	// We keep a weak reference to our parent list,
	// and the parent list keeps a strong reference to us.
	// One implication is that an item can only belong
	// to one parent.  To allow an item to belong to multiple
	// parents, update the code related to setting the parent.
	ITabbedMDIChildModifiedList* m_parentList;

};

/////////////////////////////////////////////////////////////////////////////
// Global functions
HRESULT CreateTabbedMDIChildModifiedList(ITabbedMDIChildModifiedList** list);
HRESULT CreateTabbedMDIChildModifiedItem(HWND window,
	const wchar_t* name, const wchar_t* displayName, const wchar_t* description,
	DATE lastModified, HICON icon,
	ITabbedMDIChildModifiedItem** item);
HRESULT CreateEmptyTabbedMDIChildModifiedItem(ITabbedMDIChildModifiedItem** item);


/////////////////////////////////////////////////////////////////////////////
// CSaveModifiedItemsDialog
class CSaveModifiedItemsDialog :
	//public ATL::CDialogImpl<CSaveModifiedItemsDialog>,
	public DynamicDialog::CDynamicDialogImpl<CSaveModifiedItemsDialog>,
	public WTL::CDialogResize<CSaveModifiedItemsDialog>
{
protected:
	//typedef ATL::CDialogImpl<CSaveModifiedItemsDialog> baseClass;
	typedef DynamicDialog::CDynamicDialogImpl<CSaveModifiedItemsDialog> baseClass;
	typedef WTL::CDialogResize<CSaveModifiedItemsDialog> resizeClass;

// Public enumarations
public:
	enum DialogControlIds
	{
		_IDC_LIST = 1000,
	};

	enum CheckState
	{
		eCheckState_Unchecked = INDEXTOSTATEIMAGEMASK(1),
		eCheckState_Checked = INDEXTOSTATEIMAGEMASK(2),
		eCheckState_Indeterminate = INDEXTOSTATEIMAGEMASK(3),
	};

	enum ColumnIndex
	{
		eColumn_Name         = 0,
		eColumn_Description  = 1,
		eColumn_LastModified = 2,

		eColumn_Last         = eColumn_LastModified,
		eColumn_Count        = eColumn_Last + 1,
	};

	enum Constants
	{
		eMinimumColumnWidth  = 40
	};

// Constructors
public:
	CSaveModifiedItemsDialog(ITabbedMDIChildModifiedList* list = NULL, bool canCancel = true);
	virtual ~CSaveModifiedItemsDialog();

// Public Methods (Call before DoModal)
public:
	bool HideColumn(ColumnIndex column);

// Message Handling
public:
	//enum { IDD = IDD_SAVEMODIFIEDFILES };

	BEGIN_MSG_MAP(CSaveModifiedItemsDialog)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)

		COMMAND_ID_HANDLER(IDYES, OnYes)
		COMMAND_ID_HANDLER(IDNO, OnEndDialog)
		COMMAND_ID_HANDLER(IDCANCEL, OnEndDialog)

		NOTIFY_CODE_HANDLER(LVN_INSERTITEM, OnListViewInsertItem)
		NOTIFY_CODE_HANDLER(LVN_DELETEITEM, OnListViewDeleteItem)
		NOTIFY_CODE_HANDLER(LVN_KEYDOWN, OnListViewKeyDownToToggleCheck)
		NOTIFY_CODE_HANDLER(NM_CLICK, OnListViewClickToToggleCheck)
		NOTIFY_CODE_HANDLER(NM_DBLCLK, OnListViewClickToToggleCheck)

		CHAIN_MSG_MAP(resizeClass)
	ALT_MSG_MAP(1)
		// List View Control Messages
		MESSAGE_HANDLER(WM_ERASEBKGND, OnListViewEraseBackground)
		MESSAGE_HANDLER(WM_PAINT, OnListViewPaint)
		MESSAGE_HANDLER(WM_PRINTCLIENT, OnListViewPaint)

		NOTIFY_CODE_HANDLER(HDN_BEGINTRACKA, OnHeaderBeginTrack)
		NOTIFY_CODE_HANDLER(HDN_BEGINTRACKW, OnHeaderBeginTrack)
		NOTIFY_CODE_HANDLER(HDN_TRACKA, OnHeaderTrack)
		NOTIFY_CODE_HANDLER(HDN_TRACKW, OnHeaderTrack)
		NOTIFY_CODE_HANDLER(HDN_ENDTRACKA, OnHeaderEndTrack)
		NOTIFY_CODE_HANDLER(HDN_ENDTRACKW, OnHeaderEndTrack)
		NOTIFY_CODE_HANDLER(HDN_ITEMCHANGINGA, OnHeaderItemChanging)
		NOTIFY_CODE_HANDLER(HDN_ITEMCHANGINGW, OnHeaderItemChanging)
		NOTIFY_CODE_HANDLER(HDN_DIVIDERDBLCLICKA, OnHeaderDividerDoubleClick)
		NOTIFY_CODE_HANDLER(HDN_DIVIDERDBLCLICKW, OnHeaderDividerDoubleClick)
	END_MSG_MAP()

	BEGIN_DLGRESIZE_MAP(CSaveModifiedItemsDialog)
		DLGRESIZE_CONTROL(_IDC_LIST, (DLSZ_SIZE_X | DLSZ_SIZE_Y))

		DLGRESIZE_CONTROL(IDYES, (DLSZ_MOVE_X | DLSZ_MOVE_Y))
		DLGRESIZE_CONTROL(IDNO, (DLSZ_MOVE_X | DLSZ_MOVE_Y))
		DLGRESIZE_CONTROL(IDCANCEL, (DLSZ_MOVE_X | DLSZ_MOVE_Y))
	END_DLGRESIZE_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

	LRESULT OnInitDialog(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
	LRESULT OnDestroy(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

	LRESULT OnYes(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
	LRESULT OnEndDialog(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);

	LRESULT OnListViewInsertItem(int idCtrl, LPNMHDR pnmh, BOOL& bHandled);
	LRESULT OnListViewDeleteItem(int idCtrl, LPNMHDR pnmh, BOOL& bHandled);
	LRESULT OnListViewKeyDownToToggleCheck(int idCtrl, LPNMHDR pnmh, BOOL& bHandled);
	LRESULT OnListViewClickToToggleCheck(int idCtrl, LPNMHDR pnmh, BOOL& bHandled);

	LRESULT OnListViewEraseBackground(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
	LRESULT OnListViewPaint(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

	LRESULT OnHeaderBeginTrack(int idCtrl, LPNMHDR pnmh, BOOL& bHandled);
	LRESULT OnHeaderTrack(int idCtrl, LPNMHDR pnmh, BOOL& bHandled);
	LRESULT OnHeaderEndTrack(int idCtrl, LPNMHDR pnmh, BOOL& bHandled);
	LRESULT OnHeaderItemChanging(int idCtrl, LPNMHDR pnmh, BOOL& bHandled);
	LRESULT OnHeaderDividerDoubleClick(int idCtrl, LPNMHDR pnmh, BOOL& bHandled);

// CDialogResize overrides
public:
	void DlgResize_UpdateLayout(int cxWidth, int cyHeight);

// DynamicDialog::CDynamicDialogImpl overrides
public:
	bool ConstructDialogResource(void);

protected:
	bool InitializeControls(void);
	bool InitializeValues(void);
	bool InitializeColumns(void);
	int AutoHideUnusedColumns(void);
	bool FindUsedColumns(ITabbedMDIChildModifiedList* list, int columnUseCount[eColumn_Count]);
	bool AddItems(ITabbedMDIChildModifiedList* list, int indent);
	_CSTRING_NS::CString FormatLastModifiedDateString(DATE lastModifiedUTC);
	IUnknown* GetIUnknownForItem(int index);
	int FindItemIndex(ITabbedMDIChildModifiedItem* item);
	int FindParentIndex(int item);
	void ToggleCheckState(int item);
	void SetTristateCheckState(int item, CheckState checkState);
	CheckState GetTristateCheckState(int item);
	void UpdateParentCheckState(int item, CheckState checkState);
	void CreateDefaultImages(void);
	void CreateDefaultStateImages(void);
	int AddCheckStateImage(HDC dcScreen, int cx, int cy, enum CheckState checkState);

// Members
protected:
	ATL::CComPtr<ITabbedMDIChildModifiedList> m_modifiedList;
	bool m_canCancel;
	bool m_haveAtLeastOneModifiedDate;

	ATL::CContainedWindowT<WTL::CListViewCtrl> m_list;
	WTL::CHeaderCtrl m_header;
	WTL::CImageList m_images;
	WTL::CImageList m_stateImages;
	HICON m_dialogIcon;

	int m_imageUnchecked;
	int m_imageChecked;
	int m_imageIndeterminate;

	int m_trackColumnWidth;
	int m_trackColumnIndex;
	ColumnIndex m_lastVisibleColumn;

	bool m_showColumn[eColumn_Count];

};

#endif // WTL_TABBED_MDI_SAVE_IMPLEMENTATION

#endif // __TabbedMDISave_h__
