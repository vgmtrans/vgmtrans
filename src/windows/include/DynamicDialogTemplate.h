/////////////////////////////////////////////////////////////////////////////
// DynamicDialogTemplate.h - Class used to construct an in-memory dialog
//   template, along with controls to go on the dialog
//
// Written by Daniel Bowen (dbowen@es.com)
// Copyright (c) 2003-2004 Daniel Bowen.
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
//
//  CDynamicDialogTemplate - Used to construct an in-memory dialog template
//    based on DLGTEMPLATE and DLGITEMTEMPLATE
//
//  CDynamicDialogExTemplate - Used to construct an in-memory dialog template
//    based on DLGTEMPLATEEX and DLGITEMTEMPLATEEX

#ifndef __DynamicDialogTemplate_h__
#define __DynamicDialogTemplate_h__

// To find out more about in-memory dialog templates, see the MSDN library
// for a description of DLGTEMPLATE and DLGITEMTEMPLATE

namespace DynamicDialog {

struct DynamicDialogItemSize
{
public:
	short x, y, cx, cy;

	DynamicDialogItemSize(short left = 0, short top = 0, short width = 0, short height = 0) :
		x(left), y(top), cx(width), cy(height)
	{
	}

	void Set(short left, short top, short width, short height)
	{
		x=left, y=top, cx=width, cy=height;
	}
	void Set(RECT rect)
	{
		x=(short)rect.left;
		y=(short)rect.top;
		cx=(short)(rect.right - rect.left);
		cy=(short)(rect.bottom - rect.top);
	}
};

class CDynamicDialogTemplate
{

protected:
	HGLOBAL m_hDialogTemplateMemory;
	size_t m_bytesAllocated;
	size_t m_bytesUsed;

protected:
	enum { byteAllocationChunk = 256 };

	// See help on DLGITEMTEMPLATE
	enum ClassAtom
	{
		eClassAtom_Button    = 0x0080,
		eClassAtom_Edit      = 0x0081,
		eClassAtom_Static    = 0x0082,
		eClassAtom_ListBox   = 0x0083,
		eClassAtom_ScrollBar = 0x0084,
		eClassAtom_ComboBox  = 0x0085,
	};

// Constructor/Destructor
public:
	CDynamicDialogTemplate() :
		m_hDialogTemplateMemory(NULL),
		m_bytesAllocated(0),
		m_bytesUsed(0)
	{
	}

	virtual ~CDynamicDialogTemplate()
	{
		this->Destroy();
	}

// Operators
public:
	operator bool() { return (m_hDialogTemplateMemory != NULL); }
	operator HGLOBAL() { return m_hDialogTemplateMemory; }
	operator DLGTEMPLATE*()	{ return this->GetDLGTEMPLATE(); }

	HGLOBAL GetHGLOBAL(void)
	{
		return m_hDialogTemplateMemory;
	}

	DLGTEMPLATE* GetDLGTEMPLATE(void)
	{
		DLGTEMPLATE* dialogTemplate = (DLGTEMPLATE*)::GlobalLock(m_hDialogTemplateMemory);
		::GlobalUnlock(m_hDialogTemplateMemory);

		// This should be valid at least until something causes a GlobalReAlloc.
		// No one should call this (explicitly or implictly) until the dialog
		// resource has been fully constructed
		return dialogTemplate;
	}

public:
	bool Create(
			UINT cxDLU, UINT cyDLU,
			UINT style, UINT styleEx,
			const wchar_t* dialogTitle,
			const wchar_t* fontName = L"MS Sans Serif",
			UINT fontSize = 8)
	{
		if(m_hDialogTemplateMemory)
		{
			return false;
		}

		m_hDialogTemplateMemory = ::GlobalAlloc((GMEM_MOVEABLE | GMEM_ZEROINIT), byteAllocationChunk);
		if(m_hDialogTemplateMemory)
		{
			m_bytesAllocated = byteAllocationChunk;

			DLGTEMPLATE* pDialogTemplate = (DLGTEMPLATE*) ::GlobalLock(m_hDialogTemplateMemory);
			if(pDialogTemplate)
			{
				// Define dialog template
				pDialogTemplate->style = style;
				pDialogTemplate->dwExtendedStyle = styleEx;
				pDialogTemplate->cdit = 0;
				pDialogTemplate->x = 0;
				pDialogTemplate->y = 0;
				pDialogTemplate->cx = (short)cxDLU;
				pDialogTemplate->cy = (short)cyDLU;

				// Get a pointer to the WORD after the structure
				WORD* pWordPtr = (WORD*)(pDialogTemplate + 1);

				// no menu
				*pWordPtr++ = 0;
				// default dialog box class
				*pWordPtr++ = 0;

				// Dialog title
				if(dialogTitle)
				{
					wchar_t* unicodeString = (wchar_t*) pWordPtr;
					// dialog caption
					lstrcpyW(unicodeString, dialogTitle);
					pWordPtr += ::lstrlenW(unicodeString);
				}
				// NUL termination for string
				*pWordPtr++ = 0;
			    
				if((style & DS_SETFONT) == DS_SETFONT)
				{
					// font size
					*pWordPtr++ = (WORD)fontSize;

					if(fontName)
					{
						wchar_t* unicodeString = (wchar_t*) pWordPtr;
						// font name
						lstrcpyW(unicodeString, fontName);
						pWordPtr += ::lstrlenW(unicodeString);
					}

					// NUL termination for string
					*pWordPtr++ = 0;
				}

				m_bytesUsed = (size_t)((BYTE*)pWordPtr - (BYTE*)pDialogTemplate);

				::GlobalUnlock(m_hDialogTemplateMemory);
			}
		}

		return m_hDialogTemplateMemory ? true : false;
	}

	void Destroy(void)
	{
		if(m_hDialogTemplateMemory)
		{
			::GlobalFree(m_hDialogTemplateMemory);
			m_hDialogTemplateMemory = NULL;
			m_bytesAllocated = 0;
			m_bytesUsed = 0;
		}
	}

	bool AddControl(
		    DWORD style, DWORD dwExtendedStyle,
			short x, short y, short cx, short cy,
			WORD id,
			const wchar_t* text, size_t textWordCount,
			const wchar_t* classStringOrAtom, size_t classStringOrAtomWordCount)
	{
		if(m_hDialogTemplateMemory == NULL)
		{
			return false;
		}

		// padding (class atom, NULL termination, etc.)
		const size_t padding = 16;

		size_t estimatedBytesNeeded = (size_t)
			sizeof(DLGITEMTEMPLATE) +
			(textWordCount*sizeof(WORD)) +
			(classStringOrAtomWordCount*sizeof(WORD)) +
			padding;

		if((m_bytesAllocated - m_bytesUsed) < estimatedBytesNeeded)
		{
			// Align the bytesToAllocate to be a multiple of the chunk size
			size_t bytesToAllocate = (((m_bytesUsed + estimatedBytesNeeded) / byteAllocationChunk) * byteAllocationChunk) + byteAllocationChunk;

			HGLOBAL hDialogTemplateReallocatedMemory = ::GlobalReAlloc(m_hDialogTemplateMemory, bytesToAllocate, (GMEM_MOVEABLE | GMEM_ZEROINIT));
			if(hDialogTemplateReallocatedMemory == NULL)
			{
				return false;
			}
			else
			{
				m_bytesAllocated = bytesToAllocate;
				m_hDialogTemplateMemory = hDialogTemplateReallocatedMemory;
			}
		}

		DLGTEMPLATE* pDialogTemplate = (DLGTEMPLATE*) ::GlobalLock(m_hDialogTemplateMemory);
		if(pDialogTemplate)
		{
			// Increment the control count
			pDialogTemplate->cdit += 1;

			// Get a pointer to the "end"
			BYTE* pOffset = (BYTE*)pDialogTemplate + m_bytesUsed;

			// Fill out the structure and following bytes for the control.
			// DLGITEMTEMPLATE structures should be aligned on DWORD boundaries.
			DLGITEMTEMPLATE* pDialogItem = (DLGITEMTEMPLATE*) (((DWORD_PTR)pOffset + 3) & ~3);
			pDialogItem->style = style;
			pDialogItem->dwExtendedStyle = dwExtendedStyle;
			pDialogItem->x  = x;
			pDialogItem->y  = y;
			pDialogItem->cx = cx;
			pDialogItem->cy = cy;
			pDialogItem->id = id;

			// Get a pointer to the WORD after the structure
			WORD* pWordPtr = (WORD*) (pDialogItem + 1);

			// Class Array (string or atom)
			if(classStringOrAtom)
			{
				::CopyMemory(pWordPtr, classStringOrAtom, classStringOrAtomWordCount * sizeof(WORD));
				pWordPtr += classStringOrAtomWordCount;

				// NOTE! "classStringOrAtomWordCount" should include the
				//  terminating NUL (if its not an atom value)
			}
			else
			{
				// Default to "Static" control if no control class or atom is provided
				*pWordPtr++ = 0xFFFF;
				*pWordPtr++ = eClassAtom_Static;
			}

			// Title Array (text or resource id)
			if(text)
			{
				::CopyMemory(pWordPtr, text, textWordCount * sizeof(WORD));
				pWordPtr += textWordCount;

				// NOTE! "textWordCount" should include the
				//  terminating NUL (if its not a resource ID value)
			}
			else
			{
				// default to 0 length string
				*pWordPtr++ = 0;
			}

			// no creation data
			*pWordPtr++ = 0;

			m_bytesUsed = (size_t)((BYTE*)pWordPtr - (BYTE*)pDialogTemplate);

			::GlobalUnlock(m_hDialogTemplateMemory);
		}

		return true;
	}

	bool AddControl(
		    DWORD style, DWORD dwExtendedStyle,
			short x, short y, short cx, short cy,
			WORD id, const wchar_t* text,
			const wchar_t* classString)
	{
		return this->AddControl(style, dwExtendedStyle,
			x, y, cx, cy,
			id,
			text, text ? ::lstrlenW(text) + 1 : 0,
			classString, classString ? ::lstrlenW(classString) + 1 : 0);
	}
	bool AddControl(
		    DWORD style, DWORD dwExtendedStyle,
			const DynamicDialogItemSize dialogItemSize,
			WORD id, const wchar_t* text,
			const wchar_t* classString)
	{
		return this->AddControl(style, dwExtendedStyle,
			dialogItemSize.x, dialogItemSize.y, dialogItemSize.cx, dialogItemSize.cy,
			id,
			text, text ? ::lstrlenW(text) + 1 : 0,
			classString, classString ? ::lstrlenW(classString) + 1 : 0);
	}

	bool AddControl(
		    DWORD style, DWORD dwExtendedStyle,
			short x, short y, short cx, short cy,
			WORD id, const wchar_t* text,
			ClassAtom atom)
	{
		const long classAtomWordCount = 2;
		WORD classAtom[classAtomWordCount] = {0xFFFF, (WORD)atom};
		return this->AddControl(style, dwExtendedStyle,
			x, y, cx, cy,
			id,
			text, text ? ::lstrlenW(text) + 1 : 0,
			(const wchar_t*)classAtom, classAtomWordCount);
	}
	bool AddControl(
		    DWORD style, DWORD dwExtendedStyle,
			const DynamicDialogItemSize dialogItemSize,
			WORD id, const wchar_t* text,
			ClassAtom atom)
	{
		const long classAtomWordCount = 2;
		WORD classAtom[classAtomWordCount] = {0xFFFF, (WORD)atom};
		return this->AddControl(style, dwExtendedStyle,
			dialogItemSize.x, dialogItemSize.y, dialogItemSize.cx, dialogItemSize.cy,
			id,
			text, text ? ::lstrlenW(text) + 1 : 0,
			(const wchar_t*)classAtom, classAtomWordCount);
	}

	bool AddButtonControl(
		    DWORD style, DWORD dwExtendedStyle,
			short x, short y, short cx, short cy,
			WORD id, const wchar_t* text)
	{
		return this->AddControl(style, dwExtendedStyle, x, y, cx, cy,
			id, text, eClassAtom_Button);
	}
	bool AddButtonControl(
		    DWORD style, DWORD dwExtendedStyle,
			const DynamicDialogItemSize dialogItemSize,
			WORD id, const wchar_t* text)
	{
		return this->AddControl(style, dwExtendedStyle, dialogItemSize,
			id, text, eClassAtom_Button);
	}

	bool AddEditControl(
		    DWORD style, DWORD dwExtendedStyle,
			short x, short y, short cx, short cy,
			WORD id, const wchar_t* text)
	{
		return this->AddControl(style, dwExtendedStyle, x, y, cx, cy,
			id, text, eClassAtom_Edit);
	}
	bool AddEditControl(
		    DWORD style, DWORD dwExtendedStyle,
			const DynamicDialogItemSize dialogItemSize,
			WORD id, const wchar_t* text)
	{
		return this->AddControl(style, dwExtendedStyle, dialogItemSize,
			id, text, eClassAtom_Edit);
	}

	bool AddStaticControl(
		    DWORD style, DWORD dwExtendedStyle,
			short x, short y, short cx, short cy,
			WORD id, const wchar_t* text)
	{
		return this->AddControl(style, dwExtendedStyle, x, y, cx, cy,
			id, text, eClassAtom_Static);
	}
	bool AddStaticControl(
		    DWORD style, DWORD dwExtendedStyle,
			const DynamicDialogItemSize dialogItemSize,
			WORD id, const wchar_t* text)
	{
		return this->AddControl(style, dwExtendedStyle, dialogItemSize,
			id, text, eClassAtom_Static);
	}

	bool AddListBoxControl(
		    DWORD style, DWORD dwExtendedStyle,
			short x, short y, short cx, short cy,
			WORD id, const wchar_t* text)
	{
		return this->AddControl(style, dwExtendedStyle, x, y, cx, cy,
			id, text, eClassAtom_ListBox);
	}
	bool AddListBoxControl(
		    DWORD style, DWORD dwExtendedStyle,
			const DynamicDialogItemSize dialogItemSize,
			WORD id, const wchar_t* text)
	{
		return this->AddControl(style, dwExtendedStyle, dialogItemSize,
			id, text, eClassAtom_ListBox);
	}

	bool AddScrollBarControl(
		    DWORD style, DWORD dwExtendedStyle,
			short x, short y, short cx, short cy,
			WORD id, const wchar_t* text)
	{
		return this->AddControl(style, dwExtendedStyle, x, y, cx, cy,
			id, text, eClassAtom_ScrollBar);
	}
	bool AddScrollBarControl(
		    DWORD style, DWORD dwExtendedStyle,
			const DynamicDialogItemSize dialogItemSize,
			WORD id, const wchar_t* text)
	{
		return this->AddControl(style, dwExtendedStyle, dialogItemSize,
			id, text, eClassAtom_ScrollBar);
	}

	bool AddComboBoxControl(
		    DWORD style, DWORD dwExtendedStyle,
			short x, short y, short cx, short cy,
			WORD id, const wchar_t* text)
	{
		return this->AddControl(style, dwExtendedStyle, x, y, cx, cy,
			id, text, eClassAtom_ComboBox);
	}
	bool AddComboBoxControl(
		    DWORD style, DWORD dwExtendedStyle,
			const DynamicDialogItemSize dialogItemSize,
			WORD id, const wchar_t* text)
	{
		return this->AddControl(style, dwExtendedStyle, dialogItemSize,
			id, text, eClassAtom_ComboBox);
	}
};

class CDynamicDialogExTemplate
{
};

// CDynamicDialogImpl is just like CDialogImpl, but it uses CDynamicDialogTemplate, and
//  DialogBoxIndirectParam instead of DialogBoxParam and
//  CreateDialogIndirectParam instead of CreateDialogParam

#if (_ATL_VER >= 0x0700)

// Important!  If ATL::CDialogImpl ever changes, reflect those changes here.
//  We don't inherit from CDialogImpl at all and completely duplicate (and
//  appropriately modify) everything it does.
template <class T, class TBase = ATL::CWindow, class TDynamicDialogTemplate = CDynamicDialogTemplate>
class ATL_NO_VTABLE CDynamicDialogImpl : public ATL::CDialogImplBaseT< TBase >
{
protected:
	TDynamicDialogTemplate m_dynamicDialogTemplate;

// Overrideables
public:
	// You always need to override ConstructDialogResource
	//bool ConstructDialogResource(void) { }

public:
#ifdef _DEBUG
	bool m_bModal;
	CDynamicDialogImpl() : m_bModal(false) { }
#endif //_DEBUG
	// modal dialogs
	INT_PTR DoModal(HWND hWndParent = ::GetActiveWindow(), LPARAM dwInitParam = NULL)
	{
		ATLASSERT(m_hWnd == NULL);

		T* pT = static_cast<T*>(this);
		pT->ConstructDialogResource();

		ATLASSERT((bool)m_dynamicDialogTemplate);
		_AtlWinModule.AddCreateWndData(&m_thunk.cd, (ATL::CDialogImplBaseT< TBase >*)this);
#ifdef _DEBUG
		m_bModal = true;
#endif //_DEBUG
		//return ::DialogBoxParam(_AtlBaseModule.GetResourceInstance(), MAKEINTRESOURCE(static_cast<T*>(this)->IDD),
		//			hWndParent, T::StartDialogProc, dwInitParam);
		return ::DialogBoxIndirectParam(_AtlBaseModule.GetResourceInstance(), m_dynamicDialogTemplate,
					hWndParent, T::StartDialogProc, dwInitParam);
	}
	BOOL EndDialog(int nRetCode)
	{
		ATLASSERT(::IsWindow(m_hWnd));
		ATLASSERT(m_bModal);	// must be a modal dialog
		return ::EndDialog(m_hWnd, nRetCode);
	}
	// modeless dialogs
	HWND Create(HWND hWndParent, LPARAM dwInitParam = NULL)
	{
		ATLASSERT(m_hWnd == NULL);

		T* pT = static_cast<T*>(this);
		pT->ConstructDialogResource();

		ATLASSERT((bool)m_dynamicDialogTemplate);
		_AtlWinModule.AddCreateWndData(&m_thunk.cd, (ATL::CDialogImplBaseT< TBase >*)this);
#ifdef _DEBUG
		m_bModal = false;
#endif //_DEBUG
		//HWND hWnd = ::CreateDialogParam(_AtlBaseModule.GetResourceInstance(), MAKEINTRESOURCE(static_cast<T*>(this)->IDD),
		//			hWndParent, T::StartDialogProc, dwInitParam);
		HWND hWnd = ::CreateDialogIndirectParam(_AtlBaseModule.GetResourceInstance(), m_dynamicDialogTemplate,
					hWndParent, T::StartDialogProc, dwInitParam);
		ATLASSERT(m_hWnd == hWnd);
		return hWnd;
	}
	// for CComControl
	HWND Create(HWND hWndParent, RECT&, LPARAM dwInitParam = NULL)
	{
		return Create(hWndParent, dwInitParam);
	}
	BOOL DestroyWindow()
	{
		ATLASSERT(::IsWindow(m_hWnd));
		ATLASSERT(!m_bModal);	// must not be a modal dialog
		return ::DestroyWindow(m_hWnd);
	}
};

#endif // (_ATL_VER >= 0x0700)


// CDynamicPropertyPageImpl inherits from CPropertyPageImpl, but it uses CDynamicDialogTemplate
//  to construct an in-memory dialog resource (instead of using a dialog template whose resource
//  identifier is aliased by "IDD" in the derived class).  This in-memory dialog resource is created
//  either during a "Create" call, or when there's an implicit or explict cast to PROPSHEETPAGE*
//  (such as when "AddPage" is called on the sheet with the page as the argument).

template <class T, class TBase = WTL::CPropertyPageWindow, class TDynamicDialogTemplate = CDynamicDialogTemplate>
class ATL_NO_VTABLE CDynamicPropertyPageImpl : public WTL::CPropertyPageImpl< T, TBase >
{
protected:
	typedef WTL::CPropertyPageImpl< T, TBase > baseClass;

protected:
	bool m_dialogResourceInitialized;
	TDynamicDialogTemplate m_dynamicDialogTemplate;

// Constructors
public:
	CDynamicPropertyPageImpl(ATL::_U_STRINGorID title = (LPCTSTR)NULL) :
		baseClass(title),
		m_dialogResourceInitialized(false)
	{
		// We do this after the constructor but before
		// the property page is created.
		//T* pT = static_cast<T*>(this);
		//pT->ConstructDialogResource();
		//m_psp.dwFlags |= PSP_DLGINDIRECT;
		//m_psp.pResource = m_dynamicDialogTemplate;
	}

// Enumerations
public:
	// Since we're going to provide the dialog template dynamically,
	// have IDD be 0.  We're still going to inherit from CPropertyPageImpl
	// so this will be used in its constructor (but we'll change things
	// in InitializeDialogResource so "pResource" is used instead of "pszTemplate").
	enum { IDD = 0 };

// CPropertyPageImpl Overrides:
public:
	// This Create() isn't called by WTL at all, but just in case someone else
	// calls it, we need to override it to ensure the dialog resource is initialized.
	HPROPSHEETPAGE Create()
	{
		T* pT = static_cast<T*>(this);
		pT->InitializeDialogResource();

		return baseClass::Create();
	}

public:
	// We'll do a post-constructor construction by overriding the
	//  operator PROPSHEETPAGE*(), and calling "ConstructDialogResource"
	//  which is overrideable.  This should get called when you do
	//  "AddPage", "InsertPage", . The reason we need ConstructDialogResource
	//  to be called outside of the constructor is because we want it
	//  to be overrideable, and have the version in the most derived class called.
	//  In the case of a normal dialog, we can override DoModal and Create.
	//  However, with a property page, those functions don't get called
	//  (they do get called by the sheet though, so something might be added
	//  there to call some kind of "FinalConstruct" like with COM).
	//operator PROPSHEETPAGE*() { return &m_psp; }
	operator PROPSHEETPAGE*()
	{
		T* pT = static_cast<T*>(this);
		pT->InitializeDialogResource();

		return baseClass::operator PROPSHEETPAGE*();
	}

// Overrideables
public:
	// You always need to override ConstructDialogResource
	//bool ConstructDialogResource(void) { }

	void InitializeDialogResource(void)
	{
		if(!m_dialogResourceInitialized)
		{
			m_dialogResourceInitialized = true;

			T* pT = static_cast<T*>(this);
			if(pT->ConstructDialogResource())
			{
				m_psp.dwFlags |= PSP_DLGINDIRECT;
				m_psp.pResource = m_dynamicDialogTemplate;
			}
		}
	}
};

}; // namespace DynamicDialog


#endif //__DynamicDialogTemplate_h__