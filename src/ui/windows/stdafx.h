/**
 * VGMTrans (c) - 2002-2021
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#if !defined(AFX_STDAFX_H__99FC9CAD_4832_49BC_A30D_F8FA14DDBB4F__INCLUDED_)
#define AFX_STDAFX_H__99FC9CAD_4832_49BC_A30D_F8FA14DDBB4F__INCLUDED_

#define _ATL_NO_OPENGL

/* Support Windows 7+ */
#define WINVER 0x0601
#define _WIN32_WINNT 0x0601

#include <sdkddkver.h>
#include <windows.h>

#include <assert.h>
#include <wchar.h>
#include <new.h>  // for placement new
#include <math.h>
#include <limits.h>
#include <stdio.h>
#include <stdint.h>

#include <atlbase.h>
#include <atlcoll.h>
#include <atlstr.h>
#include <atltypes.h>
#define _WTL_NO_CSTRING
#define _WTL_NO_WTYPES
#pragma warning(push)
#pragma warning(disable : 4996)
#include <atlapp.h>
#pragma warning(pop)

extern CAppModule _Module;

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
#include <unordered_map>
#include <unordered_set>
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
#include <commoncontrols.h>  // IImageList definitions

#include "atlgdix.h"

#include "CustomTabCtrl.h"
#include "DotNetTabCtrl.h"
#include "SimpleTabCtrls.h"
#include "TabbedFrame.h"
#include "ListViewNoFlicker.h"

#define _TABBEDMDI_MESSAGES_EXTERN_REGISTER
#define _TABBEDMDI_MESSAGES_NO_WARN_ATL_MIN_CRT
#include "TabbedMDI.h"

#include <DWAutoHide.h>

// Types
struct CCompressedFileInfo {
  CString sFilename;    // name of the file as stored in the CAB
  CString sFileType;    // descriptio of the file type
  DWORD dwFileSize;     // uncompressed size of the file
  FILETIME ftDateTime;  // modified date/time of the file
  UINT uAttribs;        // file attributes
  enum { from_prev_cab, in_this_cab, to_next_cab } location;
  CString sOtherCabName;  // name of the prev/next CAB
  bool bExtractable;      // true => we can extract this file

  CCompressedFileInfo() : dwFileSize(0), uAttribs(0), location(in_this_cab), bExtractable(true) {}
};

struct CDraggedFileInfo {
  // Data set at the beginning of a drag/drop:
  CString sFilename;      // name of the file as stored in the CAB
  CString sTempFilePath;  // path to the file we extract from the CAB
  int nListIdx;           // index of this item in the list ctrl

  // Data set while extracting files:
  bool bPartialFile;  // true if this file is continued in another cab
  CString sCabName;   // name of the CAB file
  bool bCabMissing;   // true if the file is partially in this cab and
                      // the CAB it's continued in isn't found, meaning
                      // the file can't be extracted

  CDraggedFileInfo(const CString& s, int n)
      : sFilename(s), nListIdx(n), bPartialFile(false), bCabMissing(false) {}
};


// Convenience macros
#define _S(x) (CString(LPCTSTR(x)))

// DirectSound includes
#include <mmsystem.h>
#include <mmreg.h>
#include <dsound.h>

#if defined(DEBUG) || defined(_DEBUG)
#ifndef V
#define V(x)                                               \
  {                                                        \
    hr = x;                                                \
    if (FAILED(hr)) {                                      \
      DXUTTrace(__FILE__, (DWORD)__LINE__, hr, L#x, true); \
    }                                                      \
  }
#endif
#ifndef V_RETURN
#define V_RETURN(x)                                               \
  {                                                               \
    hr = x;                                                       \
    if (FAILED(hr)) {                                             \
      return DXUTTrace(__FILE__, (DWORD)__LINE__, hr, L#x, true); \
    }                                                             \
  }
#endif
#else
#ifndef V
#define V(x) \
  { hr = x; }
#endif
#ifndef V_RETURN
#define V_RETURN(x)   \
  {                   \
    hr = x;           \
    if (FAILED(hr)) { \
      return hr;      \
    }                 \
  }
#endif
#endif

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif  // !defined(AFX_STDAFX_H__99FC9CAD_4832_49BC_A30D_F8FA14DDBB4F__INCLUDED_)
