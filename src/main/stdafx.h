// stdafx.h : include file for standard system include files,
//  or project specific include files that are used frequently, but
//      are changed infrequently
//

#if !defined(AFX_STDAFX_H__99FC9CAD_4832_49BC_A30D_F8FA14DDBB4F__INCLUDED_)
#define AFX_STDAFX_H__99FC9CAD_4832_49BC_A30D_F8FA14DDBB4F__INCLUDED_

// Change these values to use different versions
#define WINVER          0x0500
#define _WIN32_WINNT    0x0501
#define _WIN32_IE       0x0600
#define _RICHEDIT_VER	0x0100

#define _ATL_APARTMENT_THREADED
#define _ATL_NO_OPENGL

#define WIDEN2(x) L ## x
#define WIDEN(x) WIDEN2(x)

#ifdef _DEBUG
	// When debugging, turn on the CRT's debugging facilities
	// for checking for memory leaks
	// (we call _CrtSetDbgFlag in _tWinMain)
	#define _CRTDBG_MAP_ALLOC
	#include <stdlib.h>
	#include <crtdbg.h>
	#define DEBUG_NEW new(_NORMAL_BLOCK, __FILE__, __LINE__)
	#define new DEBUG_NEW
#endif

#define _USE_MATH_DEFINES





#include <windows.h>
#include <assert.h>
#include <wchar.h>
#include <new.h>      // for placement new
#include <math.h>      
#include <limits.h>      
#include <stdio.h>
#include <stdint.h>







// This is required for hosting browser in ATL7
//#define _ATL_DLL

#include <atlbase.h>
#if _ATL_VER >= 0x0700
	#include <atlcoll.h>
	#include <atlstr.h>
	#include <atltypes.h>
	#define _WTL_NO_CSTRING
	#define _WTL_NO_WTYPES
#else
	#define _WTL_USE_CSTRING
#endif
#pragma warning(push)
#pragma warning(disable: 4996)
#include <atlapp.h>
#pragma warning(pop)

extern CAppModule _Module;
extern bool g_bXPOrLater;

#include <atlmisc.h>
#include <atlcom.h>
#include <atlhost.h>
#include <atlwin.h>
#include <atlctl.h>
#include <atlscrl.h>
#include <atlsplit.h>

#include <atlcrack.h>
#include <atlframe.h>
#include <atlctrls.h>
#include <atlctrlx.h>
#include <atldlgs.h>
#include <atlctrlw.h>


#include <fstream>
#include <vector>
#include <list>
#include <map>
#include <hash_map>
#include <string>
#include <sstream>
#include <iterator>
#include <iostream>
#include <iomanip>
#include <wchar.h> 
#include <ctype.h>

// TINYXML
#define TIXML_USE_STL

// Win32
#include <shellapi.h>
#include <commoncontrols.h>     // IImageList definitions

#include "atlgdix.h"

#include "CustomTabCtrl.h"
#include "DotNetTabCtrl.h"
#include "SimpleTabCtrls.h"
//#include "SimpleDlgTabCtrls.h"
#include "TabbedFrame.h"
//#include "TabbedMDISave.h"
#include "ListViewNoFlicker.h"

#define _TABBEDMDI_MESSAGES_EXTERN_REGISTER
#define _TABBEDMDI_MESSAGES_NO_WARN_ATL_MIN_CRT
#include "TabbedMDI.h"

#include <DWAutoHide.h>


// Types
struct CCompressedFileInfo
{
    CString  sFilename;     // name of the file as stored in the CAB
    CString  sFileType;     // descriptio of the file type
    DWORD    dwFileSize;    // uncompressed size of the file
    FILETIME ftDateTime;    // modified date/time of the file
    UINT     uAttribs;      // file attributes
    enum { from_prev_cab, in_this_cab, to_next_cab } location;
    CString  sOtherCabName; // name of the prev/next CAB
    bool     bExtractable;  // true => we can extract this file

    CCompressedFileInfo() :
        dwFileSize(0), uAttribs(0), location(in_this_cab), bExtractable(true)
    { }
};

struct CDraggedFileInfo
{
    // Data set at the beginning of a drag/drop:
    CString sFilename;      // name of the file as stored in the CAB
    CString sTempFilePath;  // path to the file we extract from the CAB
    int nListIdx;           // index of this item in the list ctrl

    // Data set while extracting files:
    bool bPartialFile;      // true if this file is continued in another cab
    CString sCabName;       // name of the CAB file
    bool bCabMissing;       // true if the file is partially in this cab and
                            // the CAB it's continued in isn't found, meaning
                            // the file can't be extracted

    CDraggedFileInfo ( const CString& s, int n ) :
        sFilename(s), nListIdx(n), bPartialFile(false), bCabMissing(false)
    { }
};

// Version of CComObjectStack that doesn't freak out and assert when IUnknown
// methods are called.
template <class Base>
class CComObjectStack2 : public CComObjectStack<Base>
{
public:
    CComObjectStack2() : CComObjectStack<Base>() { }

    STDMETHOD_(ULONG, AddRef)() { return 1; }
    STDMETHOD_(ULONG, Release)() { return 1; }

    STDMETHOD(QueryInterface)(REFIID iid, void ** ppvObject)
        { return _InternalQueryInterface(iid, ppvObject); }
};

// Convenience macros
#define countof(x) (sizeof(x)/sizeof((x)[0]))
#define _S(x) (CString(LPCTSTR(x)))

#if _ATL_VER < 0x0700
#undef BEGIN_MSG_MAP
#define BEGIN_MSG_MAP(x) BEGIN_MSG_MAP_EX(x)
#endif


// Enable extra D3D debugging in debug builds if using the debug DirectX runtime.  
// This makes D3D objects work well in the debugger watch window, but slows down 
// performance slightly.
#if defined(DEBUG) || defined(_DEBUG)
#ifndef D3D_DEBUG_INFO
#define D3D_DEBUG_INFO
#endif
#endif

// DirectSound includes
#include <mmsystem.h>
#include <mmreg.h>
#include <dsound.h>



#if defined(DEBUG) || defined(_DEBUG)
#ifndef V
#define V(x)           { hr = x; if( FAILED(hr) ) { DXUTTrace( __FILE__, (DWORD)__LINE__, hr, L#x, true ); } }
#endif
#ifndef V_RETURN
#define V_RETURN(x)    { hr = x; if( FAILED(hr) ) { return DXUTTrace( __FILE__, (DWORD)__LINE__, hr, L#x, true ); } }
#endif
#else
#ifndef V
#define V(x)           { hr = x; }
#endif
#ifndef V_RETURN
#define V_RETURN(x)    { hr = x; if( FAILED(hr) ) { return hr; } }
#endif
#endif

#ifndef SAFE_DELETE
#define SAFE_DELETE(p)       { if(p) { delete (p);     (p)=NULL; } }
#endif    
#ifndef SAFE_DELETE_ARRAY
#define SAFE_DELETE_ARRAY(p) { if(p) { delete[] (p);   (p)=NULL; } }
#endif    
#ifndef SAFE_RELEASE
#define SAFE_RELEASE(p)      { if(p) { (p)->Release(); (p)=NULL; } }
#endif



//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.




#endif // !defined(AFX_STDAFX_H__99FC9CAD_4832_49BC_A30D_F8FA14DDBB4F__INCLUDED_)
