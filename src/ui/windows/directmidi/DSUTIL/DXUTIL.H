//-----------------------------------------------------------------------------
// File: DXUtil.h
//
// Desc: Helper functions and typing shortcuts for DirectX programming.
//
// Copyright (c) Microsoft Corporation. All rights reserved
//-----------------------------------------------------------------------------
#ifndef DXUTIL_H
#define DXUTIL_H


//-----------------------------------------------------------------------------
// Miscellaneous helper functions
//-----------------------------------------------------------------------------
#define SAFE_DELETE(p)       { if(p) { delete (p);     (p)=NULL; } }
#define SAFE_DELETE_ARRAY(p) { if(p) { delete[] (p);   (p)=NULL; } }
#define SAFE_RELEASE(p)      { if(p) { (p)->Release(); (p)=NULL; } }


#ifndef UNDER_CE
//-----------------------------------------------------------------------------
// Name: DXUtil_GetDXSDKMediaPath() and DXUtil_FindMediaFile() 
// Desc: Returns the DirectX SDK path, as stored in the system registry
//       during the SDK install.
//-----------------------------------------------------------------------------
HRESULT DXUtil_GetDXSDKMediaPathCch( TCHAR* strDest, int cchDest );
HRESULT DXUtil_GetDXSDKMediaPathCb( TCHAR* szDest, int cbDest );
HRESULT DXUtil_FindMediaFileCch( TCHAR* strDestPath, int cchDest, TCHAR* strFilename );
HRESULT DXUtil_FindMediaFileCb( TCHAR* szDestPath, int cbDest, TCHAR* strFilename );
#endif // !UNDER_CE


//-----------------------------------------------------------------------------
// Name: DXUtil_Read*RegKey() and DXUtil_Write*RegKey()
// Desc: Helper functions to read/write a string registry key 
//-----------------------------------------------------------------------------
HRESULT DXUtil_WriteStringRegKey( HKEY hKey, TCHAR* strRegName, TCHAR* strValue );
HRESULT DXUtil_WriteIntRegKey( HKEY hKey, TCHAR* strRegName, DWORD dwValue );
HRESULT DXUtil_WriteGuidRegKey( HKEY hKey, TCHAR* strRegName, GUID guidValue );
HRESULT DXUtil_WriteBoolRegKey( HKEY hKey, TCHAR* strRegName, BOOL bValue );

HRESULT DXUtil_ReadStringRegKeyCch( HKEY hKey, TCHAR* strRegName, TCHAR* strDest, DWORD cchDest, TCHAR* strDefault );
HRESULT DXUtil_ReadStringRegKeyCb( HKEY hKey, TCHAR* strRegName, TCHAR* strDest, DWORD cbDest, TCHAR* strDefault );
HRESULT DXUtil_ReadIntRegKey( HKEY hKey, TCHAR* strRegName, DWORD* pdwValue, DWORD dwDefault );
HRESULT DXUtil_ReadGuidRegKey( HKEY hKey, TCHAR* strRegName, GUID* pGuidValue, GUID& guidDefault );
HRESULT DXUtil_ReadBoolRegKey( HKEY hKey, TCHAR* strRegName, BOOL* pbValue, BOOL bDefault );


//-----------------------------------------------------------------------------
// Name: DXUtil_Timer()
// Desc: Performs timer opertations. Use the following commands:
//          TIMER_RESET           - to reset the timer
//          TIMER_START           - to start the timer
//          TIMER_STOP            - to stop (or pause) the timer
//          TIMER_ADVANCE         - to advance the timer by 0.1 seconds
//          TIMER_GETABSOLUTETIME - to get the absolute system time
//          TIMER_GETAPPTIME      - to get the current time
//          TIMER_GETELAPSEDTIME  - to get the time that elapsed between 
//                                  TIMER_GETELAPSEDTIME calls
//-----------------------------------------------------------------------------
enum TIMER_COMMAND { TIMER_RESET, TIMER_START, TIMER_STOP, TIMER_ADVANCE,
                     TIMER_GETABSOLUTETIME, TIMER_GETAPPTIME, TIMER_GETELAPSEDTIME };
FLOAT __stdcall DXUtil_Timer( TIMER_COMMAND command );


//-----------------------------------------------------------------------------
// UNICODE support for converting between CHAR, TCHAR, and WCHAR strings
//-----------------------------------------------------------------------------
HRESULT DXUtil_ConvertAnsiStringToWideCch( WCHAR* wstrDestination, const CHAR* strSource, int cchDestChar );
HRESULT DXUtil_ConvertWideStringToAnsiCch( CHAR* strDestination, const WCHAR* wstrSource, int cchDestChar );
HRESULT DXUtil_ConvertGenericStringToAnsiCch( CHAR* strDestination, const TCHAR* tstrSource, int cchDestChar );
HRESULT DXUtil_ConvertGenericStringToWideCch( WCHAR* wstrDestination, const TCHAR* tstrSource, int cchDestChar );
HRESULT DXUtil_ConvertAnsiStringToGenericCch( TCHAR* tstrDestination, const CHAR* strSource, int cchDestChar );
HRESULT DXUtil_ConvertWideStringToGenericCch( TCHAR* tstrDestination, const WCHAR* wstrSource, int cchDestChar );
HRESULT DXUtil_ConvertAnsiStringToWideCb( WCHAR* wstrDestination, const CHAR* strSource, int cbDestChar );
HRESULT DXUtil_ConvertWideStringToAnsiCb( CHAR* strDestination, const WCHAR* wstrSource, int cbDestChar );
HRESULT DXUtil_ConvertGenericStringToAnsiCb( CHAR* strDestination, const TCHAR* tstrSource, int cbDestChar );
HRESULT DXUtil_ConvertGenericStringToWideCb( WCHAR* wstrDestination, const TCHAR* tstrSource, int cbDestChar );
HRESULT DXUtil_ConvertAnsiStringToGenericCb( TCHAR* tstrDestination, const CHAR* strSource, int cbDestChar );
HRESULT DXUtil_ConvertWideStringToGenericCb( TCHAR* tstrDestination, const WCHAR* wstrSource, int cbDestChar );


//-----------------------------------------------------------------------------
// Readme functions
//-----------------------------------------------------------------------------
VOID DXUtil_LaunchReadme( HWND hWnd, TCHAR* strLoc = NULL );

//-----------------------------------------------------------------------------
// GUID to String converting 
//-----------------------------------------------------------------------------
HRESULT DXUtil_ConvertGUIDToStringCch( const GUID* pGuidSrc, TCHAR* strDest, int cchDestChar );
HRESULT DXUtil_ConvertGUIDToStringCb( const GUID* pGuidSrc, TCHAR* strDest, int cbDestChar );
HRESULT DXUtil_ConvertStringToGUID( const TCHAR* strIn, GUID* pGuidOut );


//-----------------------------------------------------------------------------
// Debug printing support
// See dxerr9.h for more debug printing support
//-----------------------------------------------------------------------------
VOID    DXUtil_Trace( TCHAR* strMsg, ... );

#if defined(DEBUG) | defined(_DEBUG)
    #define DXTRACE           DXUtil_Trace
#else
    #define DXTRACE           sizeof
#endif


//-----------------------------------------------------------------------------
// Name: ArrayListType
// Desc: Indicates how data should be stored in a CArrayList
//-----------------------------------------------------------------------------
enum ArrayListType
{
    AL_VALUE,       // entry data is copied into the list
    AL_REFERENCE,   // entry pointers are copied into the list
};


//-----------------------------------------------------------------------------
// Name: CArrayList
// Desc: A growable array
//-----------------------------------------------------------------------------
class CArrayList
{
protected:
    ArrayListType m_ArrayListType;
    void* m_pData;
    UINT m_BytesPerEntry;
    UINT m_NumEntries;
    UINT m_NumEntriesAllocated;

public:
    CArrayList( ArrayListType Type, UINT BytesPerEntry = 0 );
    ~CArrayList( void );
    HRESULT Add( void* pEntry );
    void Remove( UINT Entry );
    void* GetPtr( UINT Entry );
    UINT Count( void ) { return m_NumEntries; }
    bool Contains( void* pEntryData );
    void Clear( void ) { m_NumEntries = 0; }
};

//-----------------------------------------------------------------------------
// WinCE build support
//-----------------------------------------------------------------------------

#ifdef UNDER_CE

#define CheckDlgButton(hdialog, id, state) ::SendMessage(::GetDlgItem(hdialog, id), BM_SETCHECK, state, 0)
#define IsDlgButtonChecked(hdialog, id) ::SendMessage(::GetDlgItem(hdialog, id), BM_GETCHECK, 0L, 0L)
#define GETTIMESTAMP GetTickCount
#define _TWINCE(x) _T(x)

__inline int GetScrollPos(HWND hWnd, int nBar)
{
	SCROLLINFO si;
	memset(&si, 0, sizeof(si));
	si.cbSize = sizeof(si);
	si.fMask = SIF_POS;
	if (!GetScrollInfo(hWnd, nBar, &si))
	{
		return 0;
	}
	else
	{
		return si.nPos;
	}
}

#else // !UNDER_CE

#define GETTIMESTAMP timeGetTime
#define _TWINCE(x) x

#endif // UNDER_CE


#endif // DXUTIL_H
