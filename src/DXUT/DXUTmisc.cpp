//--------------------------------------------------------------------------------------
// File: DXUTMisc.cpp
//
// Shortcut macros and functions for using DX objects
//
// Copyright (c) Microsoft Corporation. All rights reserved
//--------------------------------------------------------------------------------------
#include "stdafx.h"
#define DXUT_GAMEPAD_TRIGGER_THRESHOLD      30
#undef min // use __min instead
#undef max // use __max instead

//--------------------------------------------------------------------------------------
// Global/Static Members
//--------------------------------------------------------------------------------------
CDXUTResourceCache& DXUTGetGlobalResourceCache()
{
    // Using an accessor function gives control of the construction order
    static CDXUTResourceCache cache;
    return cache;
}
CDXUTTimer* DXUTGetGlobalTimer()
{
    // Using an accessor function gives control of the construction order
    static CDXUTTimer timer;
    return &timer;
}


//--------------------------------------------------------------------------------------
// Internal functions forward declarations
//--------------------------------------------------------------------------------------
bool DXUTFindMediaSearchTypicalDirs( WCHAR* strSearchPath, int cchSearch, LPCWSTR strLeaf, WCHAR* strExePath, WCHAR* strExeName );
bool DXUTFindMediaSearchParentDirs( WCHAR* strSearchPath, int cchSearch, WCHAR* strStartAt, WCHAR* strLeafName );
INT_PTR CALLBACK DisplaySwitchToREFWarningProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);


//--------------------------------------------------------------------------------------
// Shared code for samples to ask user if they want to use a REF device or quit
//--------------------------------------------------------------------------------------
void DXUTDisplaySwitchingToREFWarning()
{
    if( DXUTGetShowMsgBoxOnError() )
    {
        // Open the appropriate registry key
        DWORD dwSkipWarning = 0;
        HKEY hKey;
        LONG lResult = RegOpenKeyEx( HKEY_CURRENT_USER, L"Software\\Microsoft\\DirectX 9.0 SDK", 0, KEY_READ, &hKey );
        if( ERROR_SUCCESS == lResult ) 
        {
            DWORD dwType;
            DWORD dwSize = sizeof(DWORD);
            lResult = RegQueryValueEx( hKey, L"Skip Warning On REF", NULL, &dwType, (BYTE*)&dwSkipWarning, &dwSize );
            RegCloseKey( hKey );
        }

        if( dwSkipWarning == 0 )
        {
            // Compact code to create a custom dialog box without using a template in a resource file.
            // If this dialog were in a .rc file, this would be a lot simpler but every sample calling this function would
            // need a copy of the dialog in its own .rc file. Also MessageBox API could be used here instead, but 
            // the MessageBox API is simpler to call but it can't provide a "Don't show again" checkbox
            typedef struct { DLGITEMTEMPLATE a; WORD b; WORD c; WORD d; WORD e; WORD f; } DXUT_DLG_ITEM; 
            typedef struct { DLGTEMPLATE a; WORD b; WORD c; WCHAR d[2]; WORD e; WCHAR f[14]; DXUT_DLG_ITEM i1; DXUT_DLG_ITEM i2; DXUT_DLG_ITEM i3; DXUT_DLG_ITEM i4; DXUT_DLG_ITEM i5; } DXUT_DLG_DATA; 

            DXUT_DLG_DATA dtp = 
            {                                                                                                                                                  
                {WS_CAPTION|WS_POPUP|WS_VISIBLE|WS_SYSMENU|DS_ABSALIGN|DS_3DLOOK|DS_SETFONT|DS_MODALFRAME|DS_CENTER,0,5,0,0,269,82},0,0,L" ",8,L"MS Sans Serif", 
                {{WS_CHILD|WS_VISIBLE|SS_ICON|SS_CENTERIMAGE,0,7,7,24,24,0x100},0xFFFF,0x0082,0,0,0}, // icon
                {{WS_CHILD|WS_VISIBLE,0,40,7,230,25,0x101},0xFFFF,0x0082,0,0,0}, // static text
                {{WS_CHILD|WS_VISIBLE|BS_DEFPUSHBUTTON,0,80,39,50,14,IDYES},0xFFFF,0x0080,0,0,0}, // Yes button
                {{WS_CHILD|WS_VISIBLE,0,133,39,50,14,IDNO},0xFFFF,0x0080,0,0,0}, // No button
                {{WS_CHILD|WS_VISIBLE|BS_CHECKBOX,0,7,59,70,16,IDIGNORE},0xFFFF,0x0080,0,0,0}, // checkbox
            }; 

            int nResult = (int) DialogBoxIndirect( DXUTGetHINSTANCE(), (DLGTEMPLATE*)&dtp, DXUTGetHWND(), DisplaySwitchToREFWarningProc ); 

            if( (nResult & 0x80) == 0x80 ) // "Don't show again" checkbox was checked
            {
                lResult = RegOpenKeyEx( HKEY_CURRENT_USER, L"Software\\Microsoft\\DirectX 9.0 SDK", 0, KEY_WRITE, &hKey );
                if( ERROR_SUCCESS == lResult ) 
                {
                    dwSkipWarning = 1;
                    RegSetValueEx( hKey, L"Skip Warning On REF", 0, REG_DWORD, (BYTE*)&dwSkipWarning, sizeof(DWORD) );
                    RegCloseKey( hKey );
                }
            }

            // User choose not to continue
            if( (nResult & 0x0F) == IDNO )
                DXUTShutdown(1);
        }
    }
}


//--------------------------------------------------------------------------------------
// MsgProc for DXUTDisplaySwitchingToREFWarning() dialog box
//--------------------------------------------------------------------------------------
INT_PTR CALLBACK DisplaySwitchToREFWarningProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) 
{ 
    switch (message) 
    { 
        case WM_INITDIALOG:
            // Easier to set text here than in the DLGITEMTEMPLATE
            SetWindowText( hDlg, DXUTGetWindowTitle() );
            SendMessage( GetDlgItem(hDlg, 0x100), STM_SETIMAGE, IMAGE_ICON, (LPARAM)LoadIcon(0, IDI_QUESTION));
            SetDlgItemText( hDlg, 0x101, L"Switching to the Direct3D reference rasterizer, a software device\nthat implements the entire Direct3D feature set, but runs very slowly.\nDo you wish to continue?" ); 
            SetDlgItemText( hDlg, IDYES, L"&Yes" );
            SetDlgItemText( hDlg, IDNO, L"&No" );
            SetDlgItemText( hDlg, IDIGNORE, L"&Don't show again" );
            break;

        case WM_COMMAND: 
            switch (LOWORD(wParam)) 
            { 
                case IDIGNORE: CheckDlgButton( hDlg, IDIGNORE, (IsDlgButtonChecked( hDlg, IDIGNORE ) == BST_CHECKED) ? BST_UNCHECKED : BST_CHECKED ); EnableWindow( GetDlgItem( hDlg, IDNO ), (IsDlgButtonChecked( hDlg, IDIGNORE ) != BST_CHECKED) ); break;
                case IDNO: EndDialog(hDlg, (IsDlgButtonChecked( hDlg, IDIGNORE ) == BST_CHECKED) ? IDNO|0x80 : IDNO|0x00 ); return TRUE; 
                case IDCANCEL:
                case IDYES: EndDialog(hDlg, (IsDlgButtonChecked( hDlg, IDIGNORE ) == BST_CHECKED) ? IDYES|0x80 : IDYES|0x00 ); return TRUE; 
            } 
            break;
    } 
    return FALSE; 
} 


//--------------------------------------------------------------------------------------
CDXUTTimer::CDXUTTimer()
{
    m_bTimerStopped     = true;
    m_llQPFTicksPerSec  = 0;

    m_llStopTime        = 0;
    m_llLastElapsedTime = 0;
    m_llBaseTime        = 0;

    // Use QueryPerformanceFrequency to get the frequency of the counter
    LARGE_INTEGER qwTicksPerSec;
    QueryPerformanceFrequency( &qwTicksPerSec );
    m_llQPFTicksPerSec = qwTicksPerSec.QuadPart;
}


//--------------------------------------------------------------------------------------
void CDXUTTimer::Reset()
{
    LARGE_INTEGER qwTime = GetAdjustedCurrentTime();
    
    m_llBaseTime        = qwTime.QuadPart;
    m_llLastElapsedTime = qwTime.QuadPart;
    m_llStopTime        = 0;
    m_bTimerStopped     = FALSE;
}


//--------------------------------------------------------------------------------------
void CDXUTTimer::Start()
{
    // Get the current time
    LARGE_INTEGER qwTime;
    QueryPerformanceCounter( &qwTime );

    if( m_bTimerStopped )
        m_llBaseTime += qwTime.QuadPart - m_llStopTime;
    m_llStopTime = 0;
    m_llLastElapsedTime = qwTime.QuadPart;
    m_bTimerStopped = FALSE;
}


//--------------------------------------------------------------------------------------
void CDXUTTimer::Stop()
{
    if( !m_bTimerStopped )
    {
        LARGE_INTEGER qwTime;
        QueryPerformanceCounter( &qwTime );
        m_llStopTime = qwTime.QuadPart;
        m_llLastElapsedTime = qwTime.QuadPart;
        m_bTimerStopped = TRUE;
    }
}


//--------------------------------------------------------------------------------------
void CDXUTTimer::Advance()
{
    m_llStopTime += m_llQPFTicksPerSec/10;
}


//--------------------------------------------------------------------------------------
double CDXUTTimer::GetAbsoluteTime()
{
    LARGE_INTEGER qwTime;
    QueryPerformanceCounter( &qwTime );

    double fTime = qwTime.QuadPart / (double) m_llQPFTicksPerSec;

    return fTime;
}


//--------------------------------------------------------------------------------------
double CDXUTTimer::GetTime()
{
    LARGE_INTEGER qwTime = GetAdjustedCurrentTime();

    double fAppTime = (double) ( qwTime.QuadPart - m_llBaseTime ) / (double) m_llQPFTicksPerSec;

    return fAppTime;
}


//--------------------------------------------------------------------------------------
void CDXUTTimer::GetTimeValues( double* pfTime, double* pfAbsoluteTime, float* pfElapsedTime )
{
    assert( pfTime && pfAbsoluteTime && pfElapsedTime );    

    LARGE_INTEGER qwTime = GetAdjustedCurrentTime();

    float fElapsedTime = (float) ((double) ( qwTime.QuadPart - m_llLastElapsedTime ) / (double) m_llQPFTicksPerSec);
    m_llLastElapsedTime = qwTime.QuadPart;

    // Clamp the timer to non-negative values to ensure the timer is accurate.
    // fElapsedTime can be outside this range if processor goes into a 
    // power save mode or we somehow get shuffled to another processor.  
    // However, the main thread should call SetThreadAffinityMask to ensure that 
    // we don't get shuffled to another processor.  Other worker threads should NOT call 
    // SetThreadAffinityMask, but use a shared copy of the timer data gathered from 
    // the main thread.
    if( fElapsedTime < 0.0f )
        fElapsedTime = 0.0f;
    
    *pfAbsoluteTime = qwTime.QuadPart / (double) m_llQPFTicksPerSec;
    *pfTime = ( qwTime.QuadPart - m_llBaseTime ) / (double) m_llQPFTicksPerSec;   
    *pfElapsedTime = fElapsedTime;
}


//--------------------------------------------------------------------------------------
double CDXUTTimer::GetElapsedTime()
{
    LARGE_INTEGER qwTime = GetAdjustedCurrentTime();

    double fElapsedTime = (double) ( qwTime.QuadPart - m_llLastElapsedTime ) / (double) m_llQPFTicksPerSec;
    m_llLastElapsedTime = qwTime.QuadPart;

    // See the explanation about clamping in CDXUTTimer::GetTimeValues()
    if( fElapsedTime < 0.0f )
        fElapsedTime = 0.0f;

    return fElapsedTime;
}


//--------------------------------------------------------------------------------------
// If stopped, returns time when stopped otherwise returns current time
//--------------------------------------------------------------------------------------
LARGE_INTEGER CDXUTTimer::GetAdjustedCurrentTime()
{
    LARGE_INTEGER qwTime;
    if( m_llStopTime != 0 )
        qwTime.QuadPart = m_llStopTime;
    else
        QueryPerformanceCounter( &qwTime );
    return qwTime;
}


//--------------------------------------------------------------------------------------
bool CDXUTTimer::IsStopped()
{
    return m_bTimerStopped;
}


//--------------------------------------------------------------------------------------
// Limit the current thread to one processor (the current one). This ensures that timing code 
// runs on only one processor, and will not suffer any ill effects from power management.
// See "Game Timing and Multicore Processors" for more details
//--------------------------------------------------------------------------------------
void CDXUTTimer::LimitThreadAffinityToCurrentProc()
{
    HANDLE hCurrentProcess = GetCurrentProcess();
    
    // Get the processor affinity mask for this process
    DWORD_PTR dwProcessAffinityMask = 0;
    DWORD_PTR dwSystemAffinityMask = 0;
    
    if( GetProcessAffinityMask( hCurrentProcess, &dwProcessAffinityMask, &dwSystemAffinityMask ) != 0 && dwProcessAffinityMask )
    {
        // Find the lowest processor that our process is allows to run against
        DWORD_PTR dwAffinityMask = ( dwProcessAffinityMask & ((~dwProcessAffinityMask) + 1 ) );

        // Set this as the processor that our thread must always run against
        // This must be a subset of the process affinity mask
        HANDLE hCurrentThread = GetCurrentThread();
        if( INVALID_HANDLE_VALUE != hCurrentThread )
        {
            SetThreadAffinityMask( hCurrentThread, dwAffinityMask );
            CloseHandle( hCurrentThread );
        }
    }

    CloseHandle( hCurrentProcess );
}


//--------------------------------------------------------------------------------------
// Returns pointer to static media search buffer
//--------------------------------------------------------------------------------------
WCHAR* DXUTMediaSearchPath()
{
    static WCHAR s_strMediaSearchPath[MAX_PATH] = {0};
    return s_strMediaSearchPath;

}   

//--------------------------------------------------------------------------------------
LPCWSTR DXUTGetMediaSearchPath()
{
    return DXUTMediaSearchPath();
}


//--------------------------------------------------------------------------------------
HRESULT DXUTSetMediaSearchPath( LPCWSTR strPath )
{
    HRESULT hr;

    WCHAR* s_strSearchPath = DXUTMediaSearchPath();

    hr = StringCchCopy( s_strSearchPath, MAX_PATH, strPath );   
    if( SUCCEEDED(hr) )
    {
        // append slash if needed
        size_t ch;
        hr = StringCchLength( s_strSearchPath, MAX_PATH, &ch );
        if( SUCCEEDED(hr) && s_strSearchPath[ch-1] != L'\\')
        {
            hr = StringCchCat( s_strSearchPath, MAX_PATH, L"\\" );
        }
    }

    return hr;
}


//--------------------------------------------------------------------------------------
// Tries to find the location of a SDK media file
//       cchDest is the size in WCHARs of strDestPath.  Be careful not to 
//       pass in sizeof(strDest) on UNICODE builds.
//--------------------------------------------------------------------------------------
HRESULT DXUTFindDXSDKMediaFileCch( WCHAR* strDestPath, int cchDest, LPCWSTR strFilename )
{
    bool bFound;
    WCHAR strSearchFor[MAX_PATH];
    
    if( NULL==strFilename || strFilename[0] == 0 || NULL==strDestPath || cchDest < 10 )
        return E_INVALIDARG;

    // Get the exe name, and exe path
    WCHAR strExePath[MAX_PATH] = {0};
    WCHAR strExeName[MAX_PATH] = {0};
    WCHAR* strLastSlash = NULL;
    GetModuleFileName( NULL, strExePath, MAX_PATH );
    strExePath[MAX_PATH-1]=0;
    strLastSlash = wcsrchr( strExePath, TEXT('\\') );
    if( strLastSlash )
    {
        StringCchCopy( strExeName, MAX_PATH, &strLastSlash[1] );

        // Chop the exe name from the exe path
        *strLastSlash = 0;

        // Chop the .exe from the exe name
        strLastSlash = wcsrchr( strExeName, TEXT('.') );
        if( strLastSlash )
            *strLastSlash = 0;
    }

    // Typical directories:
    //      .\
    //      ..\
    //      ..\..\
    //      %EXE_DIR%\
    //      %EXE_DIR%\..\
    //      %EXE_DIR%\..\..\
    //      %EXE_DIR%\..\%EXE_NAME%
    //      %EXE_DIR%\..\..\%EXE_NAME%

    // Typical directory search
    bFound = DXUTFindMediaSearchTypicalDirs( strDestPath, cchDest, strFilename, strExePath, strExeName );
    if( bFound )
        return S_OK;

    // Typical directory search again, but also look in a subdir called "\media\" 
    StringCchPrintf( strSearchFor, MAX_PATH, L"media\\%s", strFilename ); 
    bFound = DXUTFindMediaSearchTypicalDirs( strDestPath, cchDest, strSearchFor, strExePath, strExeName );
    if( bFound )
        return S_OK;

    WCHAR strLeafName[MAX_PATH] = {0};

    // Search all parent directories starting at .\ and using strFilename as the leaf name
    StringCchCopy( strLeafName, MAX_PATH, strFilename ); 
    bFound = DXUTFindMediaSearchParentDirs( strDestPath, cchDest, L".", strLeafName );
    if( bFound )
        return S_OK;

    // Search all parent directories starting at the exe's dir and using strFilename as the leaf name
    bFound = DXUTFindMediaSearchParentDirs( strDestPath, cchDest, strExePath, strLeafName );
    if( bFound )
        return S_OK;

    // Search all parent directories starting at .\ and using "media\strFilename" as the leaf name
    StringCchPrintf( strLeafName, MAX_PATH, L"media\\%s", strFilename ); 
    bFound = DXUTFindMediaSearchParentDirs( strDestPath, cchDest, L".", strLeafName );
    if( bFound )
        return S_OK;

    // Search all parent directories starting at the exe's dir and using "media\strFilename" as the leaf name
    bFound = DXUTFindMediaSearchParentDirs( strDestPath, cchDest, strExePath, strLeafName );
    if( bFound )
        return S_OK;

    // On failure, return the file as the path but also return an error code
    StringCchCopy( strDestPath, cchDest, strFilename );

    return DXUTERR_MEDIANOTFOUND;
}


//--------------------------------------------------------------------------------------
// Search a set of typical directories
//--------------------------------------------------------------------------------------
bool DXUTFindMediaSearchTypicalDirs( WCHAR* strSearchPath, int cchSearch, LPCWSTR strLeaf, 
                                     WCHAR* strExePath, WCHAR* strExeName )
{
    // Typical directories:
    //      .\
    //      ..\
    //      ..\..\
    //      %EXE_DIR%\
    //      %EXE_DIR%\..\
    //      %EXE_DIR%\..\..\
    //      %EXE_DIR%\..\%EXE_NAME%
    //      %EXE_DIR%\..\..\%EXE_NAME%
    //      DXSDK media path

    // Search in .\  
    StringCchCopy( strSearchPath, cchSearch, strLeaf ); 
    if( GetFileAttributes( strSearchPath ) != 0xFFFFFFFF )
        return true;

    // Search in ..\  
    StringCchPrintf( strSearchPath, cchSearch, L"..\\%s", strLeaf ); 
    if( GetFileAttributes( strSearchPath ) != 0xFFFFFFFF )
        return true;

    // Search in ..\..\ 
    StringCchPrintf( strSearchPath, cchSearch, L"..\\..\\%s", strLeaf ); 
    if( GetFileAttributes( strSearchPath ) != 0xFFFFFFFF )
        return true;

    // Search in ..\..\ 
    StringCchPrintf( strSearchPath, cchSearch, L"..\\..\\%s", strLeaf ); 
    if( GetFileAttributes( strSearchPath ) != 0xFFFFFFFF )
        return true;

    // Search in the %EXE_DIR%\ 
    StringCchPrintf( strSearchPath, cchSearch, L"%s\\%s", strExePath, strLeaf ); 
    if( GetFileAttributes( strSearchPath ) != 0xFFFFFFFF )
        return true;

    // Search in the %EXE_DIR%\..\ 
    StringCchPrintf( strSearchPath, cchSearch, L"%s\\..\\%s", strExePath, strLeaf ); 
    if( GetFileAttributes( strSearchPath ) != 0xFFFFFFFF )
        return true;

    // Search in the %EXE_DIR%\..\..\ 
    StringCchPrintf( strSearchPath, cchSearch, L"%s\\..\\..\\%s", strExePath, strLeaf ); 
    if( GetFileAttributes( strSearchPath ) != 0xFFFFFFFF )
        return true;

    // Search in "%EXE_DIR%\..\%EXE_NAME%\".  This matches the DirectX SDK layout
    StringCchPrintf( strSearchPath, cchSearch, L"%s\\..\\%s\\%s", strExePath, strExeName, strLeaf ); 
    if( GetFileAttributes( strSearchPath ) != 0xFFFFFFFF )
        return true;

    // Search in "%EXE_DIR%\..\..\%EXE_NAME%\".  This matches the DirectX SDK layout
    StringCchPrintf( strSearchPath, cchSearch, L"%s\\..\\..\\%s\\%s", strExePath, strExeName, strLeaf ); 
    if( GetFileAttributes( strSearchPath ) != 0xFFFFFFFF )
        return true;

    // Search in media search dir 
    WCHAR* s_strSearchPath = DXUTMediaSearchPath();
    if( s_strSearchPath[0] != 0 )
    {
        StringCchPrintf( strSearchPath, cchSearch, L"%s%s", s_strSearchPath, strLeaf ); 
        if( GetFileAttributes( strSearchPath ) != 0xFFFFFFFF )
            return true;
    }

    return false;
}



//--------------------------------------------------------------------------------------
// Search parent directories starting at strStartAt, and appending strLeafName
// at each parent directory.  It stops at the root directory.
//--------------------------------------------------------------------------------------
bool DXUTFindMediaSearchParentDirs( WCHAR* strSearchPath, int cchSearch, WCHAR* strStartAt, WCHAR* strLeafName )
{
    WCHAR strFullPath[MAX_PATH] = {0};
    WCHAR strFullFileName[MAX_PATH] = {0};
    WCHAR strSearch[MAX_PATH] = {0};
    WCHAR* strFilePart = NULL;

    GetFullPathName( strStartAt, MAX_PATH, strFullPath, &strFilePart );
    if( strFilePart == NULL )
        return false;
   
    while( strFilePart != NULL && *strFilePart != '\0' )
    {
        StringCchPrintf( strFullFileName, MAX_PATH, L"%s\\%s", strFullPath, strLeafName ); 
        if( GetFileAttributes( strFullFileName ) != 0xFFFFFFFF )
        {
            StringCchCopy( strSearchPath, cchSearch, strFullFileName ); 
            return true;
        }

        StringCchPrintf( strSearch, MAX_PATH, L"%s\\..", strFullPath ); 
        GetFullPathName( strSearch, MAX_PATH, strFullPath, &strFilePart );
    }

    return false;
}


//--------------------------------------------------------------------------------------
// CDXUTResourceCache
//--------------------------------------------------------------------------------------


CDXUTResourceCache::~CDXUTResourceCache()
{
    OnDestroyDevice();

    m_TextureCache.RemoveAll();
    m_EffectCache.RemoveAll();
    m_FontCache.RemoveAll();
}


HRESULT CDXUTResourceCache::CreateTextureFromFile( LPDIRECT3DDEVICE9 pDevice, LPCTSTR pSrcFile, LPDIRECT3DTEXTURE9 *ppTexture )
{
    return CreateTextureFromFileEx( pDevice, pSrcFile, D3DX_DEFAULT, D3DX_DEFAULT, D3DX_DEFAULT,
                                    0, D3DFMT_UNKNOWN, D3DPOOL_MANAGED, D3DX_DEFAULT, D3DX_DEFAULT,
                                    0, NULL, NULL, ppTexture );
}


//--------------------------------------------------------------------------------------
HRESULT CDXUTResourceCache::CreateTextureFromFileEx( LPDIRECT3DDEVICE9 pDevice, LPCTSTR pSrcFile, UINT Width, UINT Height, UINT MipLevels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, DWORD Filter, DWORD MipFilter, D3DCOLOR ColorKey, D3DXIMAGE_INFO *pSrcInfo, PALETTEENTRY *pPalette, LPDIRECT3DTEXTURE9 *ppTexture )
{
    // Search the cache for a matching entry.
    for( int i = 0; i < m_TextureCache.GetSize(); ++i )
    {
        DXUTCache_Texture &Entry = m_TextureCache[i];
        if( Entry.Location == DXUTCACHE_LOCATION_FILE &&
            !lstrcmpW( Entry.wszSource, pSrcFile ) &&
            Entry.Width == Width &&
            Entry.Height == Height &&
            Entry.MipLevels == MipLevels &&
            Entry.Usage == Usage &&
            Entry.Format == Format &&
            Entry.Pool == Pool &&
            Entry.Type == D3DRTYPE_TEXTURE )
        {
            // A match is found. Obtain the IDirect3DTexture9 interface and return that.
            return Entry.pTexture->QueryInterface( IID_IDirect3DTexture9, (LPVOID*)ppTexture );
        }
    }

    HRESULT hr;

    // No matching entry.  Load the resource and create a new entry.
    hr = D3DXCreateTextureFromFileEx( pDevice, pSrcFile, Width, Height, MipLevels, Usage, Format,
                                      Pool, Filter, MipFilter, ColorKey, pSrcInfo, pPalette, ppTexture );
    if( FAILED( hr ) )
        return hr;

    DXUTCache_Texture NewEntry;
    NewEntry.Location = DXUTCACHE_LOCATION_FILE;
    StringCchCopy( NewEntry.wszSource, MAX_PATH, pSrcFile );
    NewEntry.Width = Width;
    NewEntry.Height = Height;
    NewEntry.MipLevels = MipLevels;
    NewEntry.Usage = Usage;
    NewEntry.Format = Format;
    NewEntry.Pool = Pool;
    NewEntry.Type = D3DRTYPE_TEXTURE;
    (*ppTexture)->QueryInterface( IID_IDirect3DBaseTexture9, (LPVOID*)&NewEntry.pTexture );

    m_TextureCache.Add( NewEntry );
    return S_OK;
}


//--------------------------------------------------------------------------------------
HRESULT CDXUTResourceCache::CreateTextureFromResource( LPDIRECT3DDEVICE9 pDevice, HMODULE hSrcModule, LPCTSTR pSrcResource, LPDIRECT3DTEXTURE9 *ppTexture )
{
    return CreateTextureFromResourceEx( pDevice, hSrcModule, pSrcResource, D3DX_DEFAULT, D3DX_DEFAULT,
                                        D3DX_DEFAULT, 0, D3DFMT_UNKNOWN, D3DPOOL_MANAGED, D3DX_DEFAULT,
                                        D3DX_DEFAULT, 0, NULL, NULL, ppTexture );
}


//--------------------------------------------------------------------------------------
HRESULT CDXUTResourceCache::CreateTextureFromResourceEx( LPDIRECT3DDEVICE9 pDevice, HMODULE hSrcModule, LPCTSTR pSrcResource, UINT Width, UINT Height, UINT MipLevels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, DWORD Filter, DWORD MipFilter, D3DCOLOR ColorKey, D3DXIMAGE_INFO *pSrcInfo, PALETTEENTRY *pPalette, LPDIRECT3DTEXTURE9 *ppTexture )
{
    // Search the cache for a matching entry.
    for( int i = 0; i < m_TextureCache.GetSize(); ++i )
    {
        DXUTCache_Texture &Entry = m_TextureCache[i];
        if( Entry.Location == DXUTCACHE_LOCATION_RESOURCE &&
            Entry.hSrcModule == hSrcModule &&
            !lstrcmpW( Entry.wszSource, pSrcResource ) &&
            Entry.Width == Width &&
            Entry.Height == Height &&
            Entry.MipLevels == MipLevels &&
            Entry.Usage == Usage &&
            Entry.Format == Format &&
            Entry.Pool == Pool &&
            Entry.Type == D3DRTYPE_TEXTURE )
        {
            // A match is found. Obtain the IDirect3DTexture9 interface and return that.
            return Entry.pTexture->QueryInterface( IID_IDirect3DTexture9, (LPVOID*)ppTexture );
        }
    }

    HRESULT hr;

    // No matching entry.  Load the resource and create a new entry.
    hr = D3DXCreateTextureFromResourceEx( pDevice, hSrcModule, pSrcResource, Width, Height, MipLevels, Usage,
                                          Format, Pool, Filter, MipFilter, ColorKey, pSrcInfo, pPalette, ppTexture );
    if( FAILED( hr ) )
        return hr;

    DXUTCache_Texture NewEntry;
    NewEntry.Location = DXUTCACHE_LOCATION_RESOURCE;
    NewEntry.hSrcModule = hSrcModule;
    StringCchCopy( NewEntry.wszSource, MAX_PATH, pSrcResource );
    NewEntry.Width = Width;
    NewEntry.Height = Height;
    NewEntry.MipLevels = MipLevels;
    NewEntry.Usage = Usage;
    NewEntry.Format = Format;
    NewEntry.Pool = Pool;
    NewEntry.Type = D3DRTYPE_TEXTURE;
    (*ppTexture)->QueryInterface( IID_IDirect3DBaseTexture9, (LPVOID*)&NewEntry.pTexture );

    m_TextureCache.Add( NewEntry );
    return S_OK;
}


//--------------------------------------------------------------------------------------
HRESULT CDXUTResourceCache::CreateCubeTextureFromFile( LPDIRECT3DDEVICE9 pDevice, LPCTSTR pSrcFile, LPDIRECT3DCUBETEXTURE9 *ppCubeTexture )
{
    return CreateCubeTextureFromFileEx( pDevice, pSrcFile, D3DX_DEFAULT, D3DX_DEFAULT, 0,
                                        D3DFMT_UNKNOWN, D3DPOOL_MANAGED, D3DX_DEFAULT, D3DX_DEFAULT,
                                        0, NULL, NULL, ppCubeTexture );
}


//--------------------------------------------------------------------------------------
HRESULT CDXUTResourceCache::CreateCubeTextureFromFileEx( LPDIRECT3DDEVICE9 pDevice, LPCTSTR pSrcFile, UINT Size, UINT MipLevels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, DWORD Filter, DWORD MipFilter, D3DCOLOR ColorKey, D3DXIMAGE_INFO *pSrcInfo, PALETTEENTRY *pPalette, LPDIRECT3DCUBETEXTURE9 *ppCubeTexture )
{
    // Search the cache for a matching entry.
    for( int i = 0; i < m_TextureCache.GetSize(); ++i )
    {
        DXUTCache_Texture &Entry = m_TextureCache[i];
        if( Entry.Location == DXUTCACHE_LOCATION_FILE &&
            !lstrcmpW( Entry.wszSource, pSrcFile ) &&
            Entry.Width == Size &&
            Entry.MipLevels == MipLevels &&
            Entry.Usage == Usage &&
            Entry.Format == Format &&
            Entry.Pool == Pool &&
            Entry.Type == D3DRTYPE_CUBETEXTURE )
        {
            // A match is found. Obtain the IDirect3DCubeTexture9 interface and return that.
            return Entry.pTexture->QueryInterface( IID_IDirect3DCubeTexture9, (LPVOID*)ppCubeTexture );
        }
    }

    HRESULT hr;

    // No matching entry.  Load the resource and create a new entry.
    hr = D3DXCreateCubeTextureFromFileEx( pDevice, pSrcFile, Size, MipLevels, Usage, Format, Pool, Filter,
                                          MipFilter, ColorKey, pSrcInfo, pPalette, ppCubeTexture );
    if( FAILED( hr ) )
        return hr;

    DXUTCache_Texture NewEntry;
    NewEntry.Location = DXUTCACHE_LOCATION_FILE;
    StringCchCopy( NewEntry.wszSource, MAX_PATH, pSrcFile );
    NewEntry.Width = Size;
    NewEntry.MipLevels = MipLevels;
    NewEntry.Usage = Usage;
    NewEntry.Format = Format;
    NewEntry.Pool = Pool;
    NewEntry.Type = D3DRTYPE_CUBETEXTURE;
    (*ppCubeTexture)->QueryInterface( IID_IDirect3DBaseTexture9, (LPVOID*)&NewEntry.pTexture );

    m_TextureCache.Add( NewEntry );
    return S_OK;
}


//--------------------------------------------------------------------------------------
HRESULT CDXUTResourceCache::CreateCubeTextureFromResource( LPDIRECT3DDEVICE9 pDevice, HMODULE hSrcModule, LPCTSTR pSrcResource, LPDIRECT3DCUBETEXTURE9 *ppCubeTexture )
{
    return CreateCubeTextureFromResourceEx( pDevice, hSrcModule, pSrcResource, D3DX_DEFAULT, D3DX_DEFAULT,
                                            0, D3DFMT_UNKNOWN, D3DPOOL_MANAGED, D3DX_DEFAULT, D3DX_DEFAULT,
                                            0, NULL, NULL, ppCubeTexture );
}


//--------------------------------------------------------------------------------------
HRESULT CDXUTResourceCache::CreateCubeTextureFromResourceEx( LPDIRECT3DDEVICE9 pDevice, HMODULE hSrcModule, LPCTSTR pSrcResource, UINT Size, UINT MipLevels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, DWORD Filter, DWORD MipFilter, D3DCOLOR ColorKey, D3DXIMAGE_INFO *pSrcInfo, PALETTEENTRY *pPalette, LPDIRECT3DCUBETEXTURE9 *ppCubeTexture )
{
    // Search the cache for a matching entry.
    for( int i = 0; i < m_TextureCache.GetSize(); ++i )
    {
        DXUTCache_Texture &Entry = m_TextureCache[i];
        if( Entry.Location == DXUTCACHE_LOCATION_RESOURCE &&
            Entry.hSrcModule == hSrcModule &&
            !lstrcmpW( Entry.wszSource, pSrcResource ) &&
            Entry.Width == Size &&
            Entry.MipLevels == MipLevels &&
            Entry.Usage == Usage &&
            Entry.Format == Format &&
            Entry.Pool == Pool &&
            Entry.Type == D3DRTYPE_CUBETEXTURE )
        {
            // A match is found. Obtain the IDirect3DCubeTexture9 interface and return that.
            return Entry.pTexture->QueryInterface( IID_IDirect3DCubeTexture9, (LPVOID*)ppCubeTexture );
        }
    }

    HRESULT hr;

    // No matching entry.  Load the resource and create a new entry.
    hr = D3DXCreateCubeTextureFromResourceEx( pDevice, hSrcModule, pSrcResource, Size, MipLevels, Usage, Format,
                                              Pool, Filter, MipFilter, ColorKey, pSrcInfo, pPalette, ppCubeTexture );
    if( FAILED( hr ) )
        return hr;

    DXUTCache_Texture NewEntry;
    NewEntry.Location = DXUTCACHE_LOCATION_RESOURCE;
    NewEntry.hSrcModule = hSrcModule;
    StringCchCopy( NewEntry.wszSource, MAX_PATH, pSrcResource );
    NewEntry.Width = Size;
    NewEntry.MipLevels = MipLevels;
    NewEntry.Usage = Usage;
    NewEntry.Format = Format;
    NewEntry.Pool = Pool;
    NewEntry.Type = D3DRTYPE_CUBETEXTURE;
    (*ppCubeTexture)->QueryInterface( IID_IDirect3DBaseTexture9, (LPVOID*)&NewEntry.pTexture );

    m_TextureCache.Add( NewEntry );
    return S_OK;
}


//--------------------------------------------------------------------------------------
HRESULT CDXUTResourceCache::CreateVolumeTextureFromFile( LPDIRECT3DDEVICE9 pDevice, LPCTSTR pSrcFile, LPDIRECT3DVOLUMETEXTURE9 *ppVolumeTexture )
{
    return CreateVolumeTextureFromFileEx( pDevice, pSrcFile, D3DX_DEFAULT, D3DX_DEFAULT, D3DX_DEFAULT, D3DX_DEFAULT,
                                          0, D3DFMT_UNKNOWN, D3DPOOL_MANAGED, D3DX_DEFAULT, D3DX_DEFAULT,
                                          0, NULL, NULL, ppVolumeTexture );
}


//--------------------------------------------------------------------------------------
HRESULT CDXUTResourceCache::CreateVolumeTextureFromFileEx( LPDIRECT3DDEVICE9 pDevice, LPCTSTR pSrcFile, UINT Width, UINT Height, UINT Depth, UINT MipLevels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, DWORD Filter, DWORD MipFilter, D3DCOLOR ColorKey, D3DXIMAGE_INFO *pSrcInfo, PALETTEENTRY *pPalette, LPDIRECT3DVOLUMETEXTURE9 *ppTexture )
{
    // Search the cache for a matching entry.
    for( int i = 0; i < m_TextureCache.GetSize(); ++i )
    {
        DXUTCache_Texture &Entry = m_TextureCache[i];
        if( Entry.Location == DXUTCACHE_LOCATION_FILE &&
            !lstrcmpW( Entry.wszSource, pSrcFile ) &&
            Entry.Width == Width &&
            Entry.Height == Height &&
            Entry.Depth == Depth &&
            Entry.MipLevels == MipLevels &&
            Entry.Usage == Usage &&
            Entry.Format == Format &&
            Entry.Pool == Pool &&
            Entry.Type == D3DRTYPE_VOLUMETEXTURE )
        {
            // A match is found. Obtain the IDirect3DVolumeTexture9 interface and return that.
            return Entry.pTexture->QueryInterface( IID_IDirect3DVolumeTexture9, (LPVOID*)ppTexture );
        }
    }

    HRESULT hr;

    // No matching entry.  Load the resource and create a new entry.
    hr = D3DXCreateVolumeTextureFromFileEx( pDevice, pSrcFile, Width, Height, Depth, MipLevels, Usage, Format,
                                            Pool, Filter, MipFilter, ColorKey, pSrcInfo, pPalette, ppTexture );
    if( FAILED( hr ) )
        return hr;

    DXUTCache_Texture NewEntry;
    NewEntry.Location = DXUTCACHE_LOCATION_FILE;
    StringCchCopy( NewEntry.wszSource, MAX_PATH, pSrcFile );
    NewEntry.Width = Width;
    NewEntry.Height = Height;
    NewEntry.Depth = Depth;
    NewEntry.MipLevels = MipLevels;
    NewEntry.Usage = Usage;
    NewEntry.Format = Format;
    NewEntry.Pool = Pool;
    NewEntry.Type = D3DRTYPE_VOLUMETEXTURE;
    (*ppTexture)->QueryInterface( IID_IDirect3DBaseTexture9, (LPVOID*)&NewEntry.pTexture );

    m_TextureCache.Add( NewEntry );
    return S_OK;
}


//--------------------------------------------------------------------------------------
HRESULT CDXUTResourceCache::CreateVolumeTextureFromResource( LPDIRECT3DDEVICE9 pDevice, HMODULE hSrcModule, LPCTSTR pSrcResource, LPDIRECT3DVOLUMETEXTURE9 *ppVolumeTexture )
{
    return CreateVolumeTextureFromResourceEx( pDevice, hSrcModule, pSrcResource, D3DX_DEFAULT, D3DX_DEFAULT,
                                              D3DX_DEFAULT, D3DX_DEFAULT, 0, D3DFMT_UNKNOWN, D3DPOOL_MANAGED,
                                              D3DX_DEFAULT, D3DX_DEFAULT, 0, NULL, NULL, ppVolumeTexture );
}


//--------------------------------------------------------------------------------------
HRESULT CDXUTResourceCache::CreateVolumeTextureFromResourceEx( LPDIRECT3DDEVICE9 pDevice, HMODULE hSrcModule, LPCTSTR pSrcResource, UINT Width, UINT Height, UINT Depth, UINT MipLevels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, DWORD Filter, DWORD MipFilter, D3DCOLOR ColorKey, D3DXIMAGE_INFO *pSrcInfo, PALETTEENTRY *pPalette, LPDIRECT3DVOLUMETEXTURE9 *ppVolumeTexture )
{
    // Search the cache for a matching entry.
    for( int i = 0; i < m_TextureCache.GetSize(); ++i )
    {
        DXUTCache_Texture &Entry = m_TextureCache[i];
        if( Entry.Location == DXUTCACHE_LOCATION_RESOURCE &&
            Entry.hSrcModule == hSrcModule &&
            !lstrcmpW( Entry.wszSource, pSrcResource ) &&
            Entry.Width == Width &&
            Entry.Height == Height &&
            Entry.Depth == Depth &&
            Entry.MipLevels == MipLevels &&
            Entry.Usage == Usage &&
            Entry.Format == Format &&
            Entry.Pool == Pool &&
            Entry.Type == D3DRTYPE_VOLUMETEXTURE )
        {
            // A match is found. Obtain the IDirect3DVolumeTexture9 interface and return that.
            return Entry.pTexture->QueryInterface( IID_IDirect3DVolumeTexture9, (LPVOID*)ppVolumeTexture );
        }
    }

    HRESULT hr;

    // No matching entry.  Load the resource and create a new entry.
    hr = D3DXCreateVolumeTextureFromResourceEx( pDevice, hSrcModule, pSrcResource, Width, Height, Depth, MipLevels, Usage,
                                                Format, Pool, Filter, MipFilter, ColorKey, pSrcInfo, pPalette, ppVolumeTexture );
    if( FAILED( hr ) )
        return hr;

    DXUTCache_Texture NewEntry;
    NewEntry.Location = DXUTCACHE_LOCATION_RESOURCE;
    NewEntry.hSrcModule = hSrcModule;
    StringCchCopy( NewEntry.wszSource, MAX_PATH, pSrcResource );
    NewEntry.Width = Width;
    NewEntry.Height = Height;
    NewEntry.Depth = Depth;
    NewEntry.MipLevels = MipLevels;
    NewEntry.Usage = Usage;
    NewEntry.Format = Format;
    NewEntry.Pool = Pool;
    NewEntry.Type = D3DRTYPE_VOLUMETEXTURE;
    (*ppVolumeTexture)->QueryInterface( IID_IDirect3DBaseTexture9, (LPVOID*)&NewEntry.pTexture );

    m_TextureCache.Add( NewEntry );
    return S_OK;
}


//--------------------------------------------------------------------------------------
HRESULT CDXUTResourceCache::CreateFont( LPDIRECT3DDEVICE9 pDevice, UINT Height, UINT Width, UINT Weight, UINT MipLevels, BOOL Italic, DWORD CharSet, DWORD OutputPrecision, DWORD Quality, DWORD PitchAndFamily, LPCTSTR pFacename, LPD3DXFONT *ppFont )
{
    D3DXFONT_DESCW Desc;
    
    Desc.Height = Height;
    Desc.Width = Width;
    Desc.Weight = Weight;
    Desc.MipLevels = MipLevels;
    Desc.Italic = Italic;
    Desc.CharSet = (BYTE)CharSet;
    Desc.OutputPrecision = (BYTE)OutputPrecision;
    Desc.Quality = (BYTE)Quality;
    Desc.PitchAndFamily = (BYTE)PitchAndFamily;
    StringCchCopy( Desc.FaceName, LF_FACESIZE, pFacename );

    return CreateFontIndirect( pDevice, &Desc, ppFont );
}


//--------------------------------------------------------------------------------------
HRESULT CDXUTResourceCache::CreateFontIndirect( LPDIRECT3DDEVICE9 pDevice, CONST D3DXFONT_DESC *pDesc, LPD3DXFONT *ppFont )
{
    // Search the cache for a matching entry.
    for( int i = 0; i < m_FontCache.GetSize(); ++i )
    {
        DXUTCache_Font &Entry = m_FontCache[i];

        if( Entry.Width == pDesc->Width &&
            Entry.Height == pDesc->Height &&
            Entry.Weight == pDesc->Weight &&
            Entry.MipLevels == pDesc->MipLevels &&
            Entry.Italic == pDesc->Italic &&
            Entry.CharSet == pDesc->CharSet &&
            Entry.OutputPrecision == pDesc->OutputPrecision &&
            Entry.Quality == pDesc->Quality &&
            Entry.PitchAndFamily == pDesc->PitchAndFamily &&
            CompareString( LOCALE_USER_DEFAULT, NORM_IGNORECASE,
                           Entry.FaceName, -1,
                           pDesc->FaceName, -1 ) == CSTR_EQUAL )
        {
            // A match is found.  Increment the reference and return the ID3DXFont object.
            Entry.pFont->AddRef();
            *ppFont = Entry.pFont;
            return S_OK;
        }
    }

    HRESULT hr;

    // No matching entry.  Load the resource and create a new entry.
    hr = D3DXCreateFontIndirect( pDevice, pDesc, ppFont );
    if( FAILED( hr ) )
        return hr;

    DXUTCache_Font NewEntry;
    (D3DXFONT_DESC &)NewEntry = *pDesc;
    NewEntry.pFont = *ppFont;
    NewEntry.pFont->AddRef();

    m_FontCache.Add( NewEntry );
    return S_OK;
}


//--------------------------------------------------------------------------------------
HRESULT CDXUTResourceCache::CreateEffectFromFile( LPDIRECT3DDEVICE9 pDevice, LPCTSTR pSrcFile, const D3DXMACRO *pDefines, LPD3DXINCLUDE pInclude, DWORD Flags, LPD3DXEFFECTPOOL pPool, LPD3DXEFFECT *ppEffect, LPD3DXBUFFER *ppCompilationErrors )
{
    // Search the cache for a matching entry.
    for( int i = 0; i < m_EffectCache.GetSize(); ++i )
    {
        DXUTCache_Effect &Entry = m_EffectCache[i];

        if( Entry.Location == DXUTCACHE_LOCATION_FILE &&
            !lstrcmpW( Entry.wszSource, pSrcFile ) &&
            Entry.dwFlags == Flags )
        {
            // A match is found.  Increment the ref coutn and return the ID3DXEffect object.
            *ppEffect = Entry.pEffect;
            (*ppEffect)->AddRef();
            return S_OK;
        }
    }

    HRESULT hr;

    // No matching entry.  Load the resource and create a new entry.
    hr = D3DXCreateEffectFromFile( pDevice, pSrcFile, pDefines, pInclude, Flags, pPool, ppEffect, ppCompilationErrors );
    if( FAILED( hr ) )
        return hr;

    DXUTCache_Effect NewEntry;
    NewEntry.Location = DXUTCACHE_LOCATION_FILE;
    StringCchCopy( NewEntry.wszSource, MAX_PATH, pSrcFile );
    NewEntry.dwFlags = Flags;
    NewEntry.pEffect = *ppEffect;
    NewEntry.pEffect->AddRef();

    m_EffectCache.Add( NewEntry );
    return S_OK;
}


//--------------------------------------------------------------------------------------
HRESULT CDXUTResourceCache::CreateEffectFromResource( LPDIRECT3DDEVICE9 pDevice, HMODULE hSrcModule, LPCTSTR pSrcResource, const D3DXMACRO *pDefines, LPD3DXINCLUDE pInclude, DWORD Flags, LPD3DXEFFECTPOOL pPool, LPD3DXEFFECT *ppEffect, LPD3DXBUFFER *ppCompilationErrors )
{
    // Search the cache for a matching entry.
    for( int i = 0; i < m_EffectCache.GetSize(); ++i )
    {
        DXUTCache_Effect &Entry = m_EffectCache[i];

        if( Entry.Location == DXUTCACHE_LOCATION_RESOURCE &&
            Entry.hSrcModule == hSrcModule &&
            !lstrcmpW( Entry.wszSource, pSrcResource ) &&
            Entry.dwFlags == Flags )
        {
            // A match is found.  Increment the ref coutn and return the ID3DXEffect object.
            *ppEffect = Entry.pEffect;
            (*ppEffect)->AddRef();
            return S_OK;
        }
    }

    HRESULT hr;

    // No matching entry.  Load the resource and create a new entry.
    hr = D3DXCreateEffectFromResource( pDevice, hSrcModule, pSrcResource, pDefines, pInclude, Flags,
                                       pPool, ppEffect, ppCompilationErrors );
    if( FAILED( hr ) )
        return hr;

    DXUTCache_Effect NewEntry;
    NewEntry.Location = DXUTCACHE_LOCATION_RESOURCE;
    NewEntry.hSrcModule = hSrcModule;
    StringCchCopy( NewEntry.wszSource, MAX_PATH, pSrcResource );
    NewEntry.dwFlags = Flags;
    NewEntry.pEffect = *ppEffect;
    NewEntry.pEffect->AddRef();

    m_EffectCache.Add( NewEntry );
    return S_OK;
}


//--------------------------------------------------------------------------------------
// Device event callbacks
//--------------------------------------------------------------------------------------


//--------------------------------------------------------------------------------------
HRESULT CDXUTResourceCache::OnCreateDevice( IDirect3DDevice9 *pd3dDevice )
{
    return S_OK;
}


//--------------------------------------------------------------------------------------
HRESULT CDXUTResourceCache::OnResetDevice( IDirect3DDevice9 *pd3dDevice )
{
    // Call OnResetDevice on all effect and font objects
    for( int i = 0; i < m_EffectCache.GetSize(); ++i )
        m_EffectCache[i].pEffect->OnResetDevice();
    for( int i = 0; i < m_FontCache.GetSize(); ++i )
        m_FontCache[i].pFont->OnResetDevice();


    return S_OK;
}


//--------------------------------------------------------------------------------------
HRESULT CDXUTResourceCache::OnLostDevice()
{
    // Call OnLostDevice on all effect and font objects
    for( int i = 0; i < m_EffectCache.GetSize(); ++i )
        m_EffectCache[i].pEffect->OnLostDevice();
    for( int i = 0; i < m_FontCache.GetSize(); ++i )
        m_FontCache[i].pFont->OnLostDevice();

    // Release all the default pool textures
    for( int i = m_TextureCache.GetSize() - 1; i >= 0; --i )
        if( m_TextureCache[i].Pool == D3DPOOL_DEFAULT )
        {
            SAFE_RELEASE( m_TextureCache[i].pTexture );
            m_TextureCache.Remove( i );  // Remove the entry
        }

    return S_OK;
}


//--------------------------------------------------------------------------------------
HRESULT CDXUTResourceCache::OnDestroyDevice()
{
    // Release all resources
    for( int i = m_EffectCache.GetSize() - 1; i >= 0; --i )
    {
        SAFE_RELEASE( m_EffectCache[i].pEffect );
        m_EffectCache.Remove( i );
    }
    for( int i = m_FontCache.GetSize() - 1; i >= 0; --i )
    {
        SAFE_RELEASE( m_FontCache[i].pFont );
        m_FontCache.Remove( i );
    }
    for( int i = m_TextureCache.GetSize() - 1; i >= 0; --i )
    {
        SAFE_RELEASE( m_TextureCache[i].pTexture );
        m_TextureCache.Remove( i );
    }

    return S_OK;
}


//--------------------------------------------------------------------------------------
CD3DArcBall::CD3DArcBall()
{
    Reset();
    m_vDownPt = D3DXVECTOR3(0,0,0);
    m_vCurrentPt = D3DXVECTOR3(0,0,0);
    m_Offset.x = m_Offset.y = 0;

    RECT rc;
    GetClientRect( GetForegroundWindow(), &rc );
    SetWindow( rc.right, rc.bottom );
}





//--------------------------------------------------------------------------------------
void CD3DArcBall::Reset()
{
    D3DXQuaternionIdentity( &m_qDown );
    D3DXQuaternionIdentity( &m_qNow );
    D3DXMatrixIdentity( &m_mRotation );
    D3DXMatrixIdentity( &m_mTranslation );
    D3DXMatrixIdentity( &m_mTranslationDelta );
    m_bDrag = FALSE;
    m_fRadiusTranslation = 1.0f;
    m_fRadius = 1.0f;
}




//--------------------------------------------------------------------------------------
D3DXVECTOR3 CD3DArcBall::ScreenToVector( float fScreenPtX, float fScreenPtY )
{
    // Scale to screen
    FLOAT x   = -(fScreenPtX - m_Offset.x - m_nWidth/2)  / (m_fRadius*m_nWidth/2);
    FLOAT y   =  (fScreenPtY - m_Offset.y - m_nHeight/2) / (m_fRadius*m_nHeight/2);

    FLOAT z   = 0.0f;
    FLOAT mag = x*x + y*y;

    if( mag > 1.0f )
    {
        FLOAT scale = 1.0f/sqrtf(mag);
        x *= scale;
        y *= scale;
    }
    else
        z = sqrtf( 1.0f - mag );

    // Return vector
    return D3DXVECTOR3( x, y, z );
}




//--------------------------------------------------------------------------------------
D3DXQUATERNION CD3DArcBall::QuatFromBallPoints(const D3DXVECTOR3 &vFrom, const D3DXVECTOR3 &vTo)
{
    D3DXVECTOR3 vPart;
    float fDot = D3DXVec3Dot(&vFrom, &vTo);
    D3DXVec3Cross(&vPart, &vFrom, &vTo);

    return D3DXQUATERNION(vPart.x, vPart.y, vPart.z, fDot);
}




//--------------------------------------------------------------------------------------
void CD3DArcBall::OnBegin( int nX, int nY )
{
    // Only enter the drag state if the click falls
    // inside the click rectangle.
    if( nX >= m_Offset.x &&
        nX < m_Offset.x + m_nWidth &&
        nY >= m_Offset.y &&
        nY < m_Offset.y + m_nHeight )
    {
        m_bDrag = true;
        m_qDown = m_qNow;
        m_vDownPt = ScreenToVector( (float)nX, (float)nY );
    }
}




//--------------------------------------------------------------------------------------
void CD3DArcBall::OnMove( int nX, int nY )
{
    if (m_bDrag) 
    { 
        m_vCurrentPt = ScreenToVector( (float)nX, (float)nY );
        m_qNow = m_qDown * QuatFromBallPoints( m_vDownPt, m_vCurrentPt );
    }
}




//--------------------------------------------------------------------------------------
void CD3DArcBall::OnEnd()
{
    m_bDrag = false;
}




//--------------------------------------------------------------------------------------
// Desc:
//--------------------------------------------------------------------------------------
LRESULT CD3DArcBall::HandleMessages( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam )
{
    // Current mouse position
    int iMouseX = (short)LOWORD(lParam);
    int iMouseY = (short)HIWORD(lParam);

    switch( uMsg )
    {
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK:
            SetCapture( hWnd );
            OnBegin( iMouseX, iMouseY );
            return TRUE;

        case WM_LBUTTONUP:
            ReleaseCapture();
            OnEnd();
            return TRUE;

        case WM_CAPTURECHANGED:
            if( (HWND)lParam != hWnd )
            {
                ReleaseCapture();
                OnEnd();
            }
            return TRUE;

        case WM_RBUTTONDOWN:
        case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONDBLCLK:
            SetCapture( hWnd );
            // Store off the position of the cursor when the button is pressed
            m_ptLastMouse.x = iMouseX;
            m_ptLastMouse.y = iMouseY;
            return TRUE;

        case WM_RBUTTONUP:
        case WM_MBUTTONUP:
            ReleaseCapture();
            return TRUE;

        case WM_MOUSEMOVE:
            if( MK_LBUTTON&wParam )
            {
                OnMove( iMouseX, iMouseY );
            }
            else if( (MK_RBUTTON&wParam) || (MK_MBUTTON&wParam) )
            {
                // Normalize based on size of window and bounding sphere radius
                FLOAT fDeltaX = ( m_ptLastMouse.x-iMouseX ) * m_fRadiusTranslation / m_nWidth;
                FLOAT fDeltaY = ( m_ptLastMouse.y-iMouseY ) * m_fRadiusTranslation / m_nHeight;

                if( wParam & MK_RBUTTON )
                {
                    D3DXMatrixTranslation( &m_mTranslationDelta, -2*fDeltaX, 2*fDeltaY, 0.0f );
                    D3DXMatrixMultiply( &m_mTranslation, &m_mTranslation, &m_mTranslationDelta );
                }
                else  // wParam & MK_MBUTTON
                {
                    D3DXMatrixTranslation( &m_mTranslationDelta, 0.0f, 0.0f, 5*fDeltaY );
                    D3DXMatrixMultiply( &m_mTranslation, &m_mTranslation, &m_mTranslationDelta );
                }

                // Store mouse coordinate
                m_ptLastMouse.x = iMouseX;
                m_ptLastMouse.y = iMouseY;
            }
            return TRUE;
    }

    return FALSE;
}




//--------------------------------------------------------------------------------------
// Constructor
//--------------------------------------------------------------------------------------
CBaseCamera::CBaseCamera()
{
    m_cKeysDown = 0;
    ZeroMemory( m_aKeys, sizeof(BYTE)*CAM_MAX_KEYS );
    ZeroMemory( m_GamePad, sizeof(DXUT_GAMEPAD)*DXUT_MAX_CONTROLLERS );

    // Set attributes for the view matrix
    D3DXVECTOR3 vEyePt    = D3DXVECTOR3(0.0f,0.0f,0.0f);
    D3DXVECTOR3 vLookatPt = D3DXVECTOR3(0.0f,0.0f,1.0f);

    // Setup the view matrix
    SetViewParams( &vEyePt, &vLookatPt );

    // Setup the projection matrix
    SetProjParams( D3DX_PI/4, 1.0f, 1.0f, 1000.0f );

    GetCursorPos( &m_ptLastMousePosition );
    m_bMouseLButtonDown = false;
    m_bMouseMButtonDown = false;
    m_bMouseRButtonDown = false;
    m_nCurrentButtonMask = 0;
    m_nMouseWheelDelta = 0;

    m_fCameraYawAngle = 0.0f;
    m_fCameraPitchAngle = 0.0f;

    SetRect( &m_rcDrag, LONG_MIN, LONG_MIN, LONG_MAX, LONG_MAX );
    m_vVelocity     = D3DXVECTOR3(0,0,0);
    m_bMovementDrag = false;
    m_vVelocityDrag = D3DXVECTOR3(0,0,0);
    m_fDragTimer    = 0.0f;
    m_fTotalDragTimeToZero = 0.25;
    m_vRotVelocity = D3DXVECTOR2(0,0);

    m_fRotationScaler = 0.01f;           
    m_fMoveScaler = 5.0f;           

    m_bInvertPitch = false;
    m_bEnableYAxisMovement = true;
    m_bEnablePositionMovement = true;

    m_vMouseDelta   = D3DXVECTOR2(0,0);
    m_fFramesToSmoothMouseData = 2.0f;

    m_bClipToBoundary = false;
    m_vMinBoundary = D3DXVECTOR3(-1,-1,-1);
    m_vMaxBoundary = D3DXVECTOR3(1,1,1);
}


//--------------------------------------------------------------------------------------
// Client can call this to change the position and direction of camera
//--------------------------------------------------------------------------------------
VOID CBaseCamera::SetViewParams( D3DXVECTOR3* pvEyePt, D3DXVECTOR3* pvLookatPt )
{
    if( NULL == pvEyePt || NULL == pvLookatPt )
        return;

    m_vDefaultEye = m_vEye = *pvEyePt;
    m_vDefaultLookAt = m_vLookAt = *pvLookatPt;

    // Calc the view matrix
    D3DXVECTOR3 vUp(0,1,0);
    D3DXMatrixLookAtLH( &m_mView, pvEyePt, pvLookatPt, &vUp );

    D3DXMATRIX mInvView;
    D3DXMatrixInverse( &mInvView, NULL, &m_mView );

    // The axis basis vectors and camera position are stored inside the 
    // position matrix in the 4 rows of the camera's world matrix.
    // To figure out the yaw/pitch of the camera, we just need the Z basis vector
    D3DXVECTOR3* pZBasis = (D3DXVECTOR3*) &mInvView._31;

    m_fCameraYawAngle   = atan2f( pZBasis->x, pZBasis->z );
    float fLen = sqrtf(pZBasis->z*pZBasis->z + pZBasis->x*pZBasis->x);
    m_fCameraPitchAngle = -atan2f( pZBasis->y, fLen );
}




//--------------------------------------------------------------------------------------
// Calculates the projection matrix based on input params
//--------------------------------------------------------------------------------------
VOID CBaseCamera::SetProjParams( FLOAT fFOV, FLOAT fAspect, FLOAT fNearPlane,
                                   FLOAT fFarPlane )
{
    // Set attributes for the projection matrix
    m_fFOV        = fFOV;
    m_fAspect     = fAspect;
    m_fNearPlane  = fNearPlane;
    m_fFarPlane   = fFarPlane;

    D3DXMatrixPerspectiveFovLH( &m_mProj, fFOV, fAspect, fNearPlane, fFarPlane );
}




//--------------------------------------------------------------------------------------
// Call this from your message proc so this class can handle window messages
//--------------------------------------------------------------------------------------
LRESULT CBaseCamera::HandleMessages( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam )
{
    UNREFERENCED_PARAMETER( hWnd );
    UNREFERENCED_PARAMETER( lParam );

    switch( uMsg )
    {
        case WM_KEYDOWN:
        {
            // Map this key to a D3DUtil_CameraKeys enum and update the
            // state of m_aKeys[] by adding the KEY_WAS_DOWN_MASK|KEY_IS_DOWN_MASK mask
            // only if the key is not down
            D3DUtil_CameraKeys mappedKey = MapKey( (UINT)wParam );
            if( mappedKey != CAM_UNKNOWN )
            {
                if( FALSE == IsKeyDown(m_aKeys[mappedKey]) )
                {
                    m_aKeys[ mappedKey ] = KEY_WAS_DOWN_MASK | KEY_IS_DOWN_MASK;
                    ++m_cKeysDown;
                }
            }
            break;
        }

        case WM_KEYUP:
        {
            // Map this key to a D3DUtil_CameraKeys enum and update the
            // state of m_aKeys[] by removing the KEY_IS_DOWN_MASK mask.
            D3DUtil_CameraKeys mappedKey = MapKey( (UINT)wParam );
            if( mappedKey != CAM_UNKNOWN && (DWORD)mappedKey < 8 )
            {
                m_aKeys[ mappedKey ] &= ~KEY_IS_DOWN_MASK;
                --m_cKeysDown;
            }
            break;
        }

        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDBLCLK:
        case WM_LBUTTONDBLCLK:
        {
            // Compute the drag rectangle in screen coord.
            POINT ptCursor = { (short)LOWORD(lParam), (short)HIWORD(lParam) };

            // Update member var state
            if( ( uMsg == WM_LBUTTONDOWN || uMsg == WM_LBUTTONDBLCLK ) && PtInRect( &m_rcDrag, ptCursor ) )
                { m_bMouseLButtonDown = true; m_nCurrentButtonMask |= MOUSE_LEFT_BUTTON; }
            if( ( uMsg == WM_MBUTTONDOWN || uMsg == WM_MBUTTONDBLCLK ) && PtInRect( &m_rcDrag, ptCursor ) )
                { m_bMouseMButtonDown = true; m_nCurrentButtonMask |= MOUSE_MIDDLE_BUTTON; }
            if( ( uMsg == WM_RBUTTONDOWN || uMsg == WM_RBUTTONDBLCLK ) && PtInRect( &m_rcDrag, ptCursor ) )
                { m_bMouseRButtonDown = true; m_nCurrentButtonMask |= MOUSE_RIGHT_BUTTON; }

            // Capture the mouse, so if the mouse button is 
            // released outside the window, we'll get the WM_LBUTTONUP message
            SetCapture(hWnd);
            GetCursorPos( &m_ptLastMousePosition );
            return TRUE;
        }

        case WM_RBUTTONUP: 
        case WM_MBUTTONUP: 
        case WM_LBUTTONUP:   
        {
            // Update member var state
            if( uMsg == WM_LBUTTONUP ) { m_bMouseLButtonDown = false; m_nCurrentButtonMask &= ~MOUSE_LEFT_BUTTON; }
            if( uMsg == WM_MBUTTONUP ) { m_bMouseMButtonDown = false; m_nCurrentButtonMask &= ~MOUSE_MIDDLE_BUTTON; }
            if( uMsg == WM_RBUTTONUP ) { m_bMouseRButtonDown = false; m_nCurrentButtonMask &= ~MOUSE_RIGHT_BUTTON; }

            // Release the capture if no mouse buttons down
            if( !m_bMouseLButtonDown  && 
                !m_bMouseRButtonDown &&
                !m_bMouseMButtonDown )
            {
                ReleaseCapture();
            }
            break;
        }

        case WM_CAPTURECHANGED:
        {
            if( (HWND)lParam != hWnd )
            {
                if( (m_nCurrentButtonMask & MOUSE_LEFT_BUTTON) ||
                    (m_nCurrentButtonMask & MOUSE_MIDDLE_BUTTON) ||
                    (m_nCurrentButtonMask & MOUSE_RIGHT_BUTTON) )
                {
                    m_bMouseLButtonDown = false;
                    m_bMouseMButtonDown = false;
                    m_bMouseRButtonDown = false;
                    m_nCurrentButtonMask &= ~MOUSE_LEFT_BUTTON;
                    m_nCurrentButtonMask &= ~MOUSE_MIDDLE_BUTTON;
                    m_nCurrentButtonMask &= ~MOUSE_RIGHT_BUTTON;
                    ReleaseCapture();
                }
            }
            break;
        }

        case WM_MOUSEWHEEL: 
            // Update member var state
            m_nMouseWheelDelta = (short)HIWORD(wParam) / 120;
            break;
    }

    return FALSE;
}


//--------------------------------------------------------------------------------------
// Figure out the velocity based on keyboard input & drag if any
//--------------------------------------------------------------------------------------
void CBaseCamera::GetInput( bool bGetKeyboardInput, bool bGetMouseInput, bool bGetGamepadInput, bool bResetCursorAfterMove )
{
    m_vKeyboardDirection = D3DXVECTOR3(0,0,0);
    if( bGetKeyboardInput )
    {
        // Update acceleration vector based on keyboard state
        if( IsKeyDown(m_aKeys[CAM_MOVE_FORWARD]) )
            m_vKeyboardDirection.z += 1.0f;
        if( IsKeyDown(m_aKeys[CAM_MOVE_BACKWARD]) )
            m_vKeyboardDirection.z -= 1.0f;
        if( m_bEnableYAxisMovement )
        {
            if( IsKeyDown(m_aKeys[CAM_MOVE_UP]) )
                m_vKeyboardDirection.y += 1.0f;
            if( IsKeyDown(m_aKeys[CAM_MOVE_DOWN]) )
                m_vKeyboardDirection.y -= 1.0f;
        }
        if( IsKeyDown(m_aKeys[CAM_STRAFE_RIGHT]) )
            m_vKeyboardDirection.x += 1.0f;
        if( IsKeyDown(m_aKeys[CAM_STRAFE_LEFT]) )
            m_vKeyboardDirection.x -= 1.0f;
    }

    if( bGetMouseInput )
    {
        // Get current position of mouse
        POINT ptCurMouseDelta;
        POINT ptCurMousePos;
        
        if( GetCursorPos( &ptCurMousePos ) )
        {
            // Calc how far it's moved since last frame
            ptCurMouseDelta.x = ptCurMousePos.x - m_ptLastMousePosition.x;
            ptCurMouseDelta.y = ptCurMousePos.y - m_ptLastMousePosition.y;
            
            // Record current position for next time
            m_ptLastMousePosition = ptCurMousePos;
        }
        else
        {
            // If GetCursorPos() returns false, just set delta to zero
            ptCurMouseDelta.x = 0;
            ptCurMouseDelta.y = 0;
        }

        if( bResetCursorAfterMove && DXUTIsActive() )
        {
            // Set position of camera to center of desktop, 
            // so it always has room to move.  This is very useful
            // if the cursor is hidden.  If this isn't done and cursor is hidden, 
            // then invisible cursor will hit the edge of the screen 
            // and the user can't tell what happened
            POINT ptCenter;

            // Get the center of the current monitor
            MONITORINFO mi;
            mi.cbSize = sizeof(MONITORINFO);
            DXUTGetMonitorInfo( DXUTMonitorFromWindow(DXUTGetHWND(),MONITOR_DEFAULTTONEAREST), &mi );
            ptCenter.x = (mi.rcMonitor.left + mi.rcMonitor.right) / 2;
            ptCenter.y = (mi.rcMonitor.top + mi.rcMonitor.bottom) / 2;   
            SetCursorPos( ptCenter.x, ptCenter.y );
            m_ptLastMousePosition = ptCenter;
        }

        // Smooth the relative mouse data over a few frames so it isn't 
        // jerky when moving slowly at low frame rates.
        float fPercentOfNew =  1.0f / m_fFramesToSmoothMouseData;
        float fPercentOfOld =  1.0f - fPercentOfNew;
        m_vMouseDelta.x = m_vMouseDelta.x*fPercentOfOld + ptCurMouseDelta.x*fPercentOfNew;
        m_vMouseDelta.y = m_vMouseDelta.y*fPercentOfOld + ptCurMouseDelta.y*fPercentOfNew;

    }

    if( bGetGamepadInput )
    {
        m_vGamePadLeftThumb = D3DXVECTOR3(0,0,0);
        m_vGamePadRightThumb = D3DXVECTOR3(0,0,0);

        // Get controller state
        for( DWORD iUserIndex=0; iUserIndex<DXUT_MAX_CONTROLLERS; iUserIndex++ )
        {
            DXUTGetGamepadState( iUserIndex, &m_GamePad[iUserIndex], true, true );

            // Mark time if the controller is in a non-zero state
            if( m_GamePad[iUserIndex].wButtons || 
                m_GamePad[iUserIndex].sThumbLX || m_GamePad[iUserIndex].sThumbLX || 
                m_GamePad[iUserIndex].sThumbRX || m_GamePad[iUserIndex].sThumbRY || 
                m_GamePad[iUserIndex].bLeftTrigger || m_GamePad[iUserIndex].bRightTrigger )
            {
                m_GamePadLastActive[iUserIndex] = DXUTGetTime();
            }
        }

        // Find out which controller was non-zero last
        int iMostRecentlyActive = -1;
        double fMostRecentlyActiveTime = 0.0f;
        for( DWORD iUserIndex=0; iUserIndex<DXUT_MAX_CONTROLLERS; iUserIndex++ )
        {
            if( m_GamePadLastActive[iUserIndex] > fMostRecentlyActiveTime )
            {
                fMostRecentlyActiveTime = m_GamePadLastActive[iUserIndex];
                iMostRecentlyActive = iUserIndex;
            }
        }

        // Use the most recent non-zero controller if its connected
        if( iMostRecentlyActive >= 0 && m_GamePad[iMostRecentlyActive].bConnected )
        {
            m_vGamePadLeftThumb.x = m_GamePad[iMostRecentlyActive].fThumbLX;
            m_vGamePadLeftThumb.y = 0.0f;
            m_vGamePadLeftThumb.z = m_GamePad[iMostRecentlyActive].fThumbLY;

            m_vGamePadRightThumb.x = m_GamePad[iMostRecentlyActive].fThumbRX;
            m_vGamePadRightThumb.y = 0.0f;
            m_vGamePadRightThumb.z = m_GamePad[iMostRecentlyActive].fThumbRY;
        }
    }
}


//--------------------------------------------------------------------------------------
// Figure out the velocity based on keyboard input & drag if any
//--------------------------------------------------------------------------------------
void CBaseCamera::UpdateVelocity( float fElapsedTime )
{
    D3DXMATRIX mRotDelta;

    D3DXVECTOR2 vGamePadRightThumb = D3DXVECTOR2(m_vGamePadRightThumb.x, -m_vGamePadRightThumb.z);
    m_vRotVelocity = m_vMouseDelta * m_fRotationScaler + vGamePadRightThumb * 0.02f;

    D3DXVECTOR3 vAccel = m_vKeyboardDirection + m_vGamePadLeftThumb;

    // Normalize vector so if moving 2 dirs (left & forward), 
    // the camera doesn't move faster than if moving in 1 dir
    D3DXVec3Normalize( &vAccel, &vAccel );

    // Scale the acceleration vector
    vAccel *= m_fMoveScaler;

    if( m_bMovementDrag )
    {
        // Is there any acceleration this frame?
        if( D3DXVec3LengthSq( &vAccel ) > 0 )
        {
            // If so, then this means the user has pressed a movement key\
            // so change the velocity immediately to acceleration 
            // upon keyboard input.  This isn't normal physics
            // but it will give a quick response to keyboard input
            m_vVelocity = vAccel;
            m_fDragTimer = m_fTotalDragTimeToZero;
            m_vVelocityDrag = vAccel / m_fDragTimer;
        }
        else 
        {
            // If no key being pressed, then slowly decrease velocity to 0
            if( m_fDragTimer > 0 )
            {
                // Drag until timer is <= 0
                m_vVelocity -= m_vVelocityDrag * fElapsedTime;
                m_fDragTimer -= fElapsedTime;
            }
            else
            {
                // Zero velocity
                m_vVelocity = D3DXVECTOR3(0,0,0);
            }
        }
    }
    else
    {
        // No drag, so immediately change the velocity
        m_vVelocity = vAccel;
    }
}




//--------------------------------------------------------------------------------------
// Clamps pV to lie inside m_vMinBoundary & m_vMaxBoundary
//--------------------------------------------------------------------------------------
void CBaseCamera::ConstrainToBoundary( D3DXVECTOR3* pV )
{
    // Constrain vector to a bounding box 
    pV->x = __max(pV->x, m_vMinBoundary.x);
    pV->y = __max(pV->y, m_vMinBoundary.y);
    pV->z = __max(pV->z, m_vMinBoundary.z);

    pV->x = __min(pV->x, m_vMaxBoundary.x);
    pV->y = __min(pV->y, m_vMaxBoundary.y);
    pV->z = __min(pV->z, m_vMaxBoundary.z);
}




//--------------------------------------------------------------------------------------
// Maps a windows virtual key to an enum
//--------------------------------------------------------------------------------------
D3DUtil_CameraKeys CBaseCamera::MapKey( UINT nKey )
{
    // This could be upgraded to a method that's user-definable but for 
    // simplicity, we'll use a hardcoded mapping.
    switch( nKey )
    {
        case VK_CONTROL:  return CAM_CONTROLDOWN;
        case VK_LEFT:  return CAM_STRAFE_LEFT;
        case VK_RIGHT: return CAM_STRAFE_RIGHT;
        case VK_UP:    return CAM_MOVE_FORWARD;
        case VK_DOWN:  return CAM_MOVE_BACKWARD;
        case VK_PRIOR: return CAM_MOVE_UP;        // pgup
        case VK_NEXT:  return CAM_MOVE_DOWN;      // pgdn

        case 'A':      return CAM_STRAFE_LEFT;
        case 'D':      return CAM_STRAFE_RIGHT;
        case 'W':      return CAM_MOVE_FORWARD;
        case 'S':      return CAM_MOVE_BACKWARD;
        case 'Q':      return CAM_MOVE_DOWN;
        case 'E':      return CAM_MOVE_UP;

        case VK_NUMPAD4: return CAM_STRAFE_LEFT;
        case VK_NUMPAD6: return CAM_STRAFE_RIGHT;
        case VK_NUMPAD8: return CAM_MOVE_FORWARD;
        case VK_NUMPAD2: return CAM_MOVE_BACKWARD;
        case VK_NUMPAD9: return CAM_MOVE_UP;        
        case VK_NUMPAD3: return CAM_MOVE_DOWN;      

        case VK_HOME:   return CAM_RESET;
    }

    return CAM_UNKNOWN;
}




//--------------------------------------------------------------------------------------
// Reset the camera's position back to the default
//--------------------------------------------------------------------------------------
VOID CBaseCamera::Reset()
{
    SetViewParams( &m_vDefaultEye, &m_vDefaultLookAt );
}




//--------------------------------------------------------------------------------------
// Constructor
//--------------------------------------------------------------------------------------
CFirstPersonCamera::CFirstPersonCamera() :
    m_nActiveButtonMask( 0x07 )
{
    m_bRotateWithoutButtonDown = false;
}




//--------------------------------------------------------------------------------------
// Update the view matrix based on user input & elapsed time
//--------------------------------------------------------------------------------------
VOID CFirstPersonCamera::FrameMove( FLOAT fElapsedTime )
{
    if( DXUTGetGlobalTimer()->IsStopped() )
        fElapsedTime = 1.0f / DXUTGetFPS();

    if( IsKeyDown(m_aKeys[CAM_RESET]) )
        Reset();

    // Get keyboard/mouse/gamepad input
    GetInput( m_bEnablePositionMovement, (m_nActiveButtonMask & m_nCurrentButtonMask) || m_bRotateWithoutButtonDown, true, m_bResetCursorAfterMove );

    // Get amount of velocity based on the keyboard input and drag (if any)
    UpdateVelocity( fElapsedTime );

    // Simple euler method to calculate position delta
    D3DXVECTOR3 vPosDelta = m_vVelocity * fElapsedTime;

    // If rotating the camera 
    if( (m_nActiveButtonMask & m_nCurrentButtonMask) || 
        m_bRotateWithoutButtonDown || 
        m_vGamePadRightThumb.x != 0 || 
        m_vGamePadRightThumb.z != 0 )
    {
        // Update the pitch & yaw angle based on mouse movement
        float fYawDelta   = m_vRotVelocity.x;
        float fPitchDelta = m_vRotVelocity.y;

        // Invert pitch if requested
        if( m_bInvertPitch )
            fPitchDelta = -fPitchDelta;

        m_fCameraPitchAngle += fPitchDelta;
        m_fCameraYawAngle   += fYawDelta;

        // Limit pitch to straight up or straight down
        m_fCameraPitchAngle = __max( -D3DX_PI/2.0f,  m_fCameraPitchAngle );
        m_fCameraPitchAngle = __min( +D3DX_PI/2.0f,  m_fCameraPitchAngle );
    }

    // Make a rotation matrix based on the camera's yaw & pitch
    D3DXMATRIX mCameraRot;
    D3DXMatrixRotationYawPitchRoll( &mCameraRot, m_fCameraYawAngle, m_fCameraPitchAngle, 0 );

    // Transform vectors based on camera's rotation matrix
    D3DXVECTOR3 vWorldUp, vWorldAhead;
    D3DXVECTOR3 vLocalUp    = D3DXVECTOR3(0,1,0);
    D3DXVECTOR3 vLocalAhead = D3DXVECTOR3(0,0,1);
    D3DXVec3TransformCoord( &vWorldUp, &vLocalUp, &mCameraRot );
    D3DXVec3TransformCoord( &vWorldAhead, &vLocalAhead, &mCameraRot );

    // Transform the position delta by the camera's rotation 
    D3DXVECTOR3 vPosDeltaWorld;
    if( !m_bEnableYAxisMovement )
    {
        // If restricting Y movement, do not include pitch
        // when transforming position delta vector.
        D3DXMatrixRotationYawPitchRoll( &mCameraRot, m_fCameraYawAngle, 0.0f, 0.0f );
    }
    D3DXVec3TransformCoord( &vPosDeltaWorld, &vPosDelta, &mCameraRot );

    // Move the eye position 
    m_vEye += vPosDeltaWorld;
    if( m_bClipToBoundary )
        ConstrainToBoundary( &m_vEye );

    // Update the lookAt position based on the eye position 
    m_vLookAt = m_vEye + vWorldAhead;

    // Update the view matrix
    D3DXMatrixLookAtLH( &m_mView, &m_vEye, &m_vLookAt, &vWorldUp );

    D3DXMatrixInverse( &m_mCameraWorld, NULL, &m_mView );
}


//--------------------------------------------------------------------------------------
// Enable or disable each of the mouse buttons for rotation drag.
//--------------------------------------------------------------------------------------
void CFirstPersonCamera::SetRotateButtons( bool bLeft, bool bMiddle, bool bRight, bool bRotateWithoutButtonDown )
{
    m_nActiveButtonMask = ( bLeft ? MOUSE_LEFT_BUTTON : 0 ) |
                          ( bMiddle ? MOUSE_MIDDLE_BUTTON : 0 ) |
                          ( bRight ? MOUSE_RIGHT_BUTTON : 0 );
    m_bRotateWithoutButtonDown = bRotateWithoutButtonDown;
}


//--------------------------------------------------------------------------------------
// Constructor 
//--------------------------------------------------------------------------------------
CModelViewerCamera::CModelViewerCamera()
{
    D3DXMatrixIdentity( &m_mWorld );
    D3DXMatrixIdentity( &m_mModelRot );
    D3DXMatrixIdentity( &m_mModelLastRot );    
    D3DXMatrixIdentity( &m_mCameraRotLast );    
    m_vModelCenter = D3DXVECTOR3(0,0,0);
    m_fRadius    = 5.0f;
    m_fDefaultRadius = 5.0f;
    m_fMinRadius = 1.0f;
    m_fMaxRadius = FLT_MAX;
    m_bLimitPitch = false;
    m_bEnablePositionMovement = false;
    m_bAttachCameraToModel = false;

    m_nRotateModelButtonMask  = MOUSE_LEFT_BUTTON;
    m_nZoomButtonMask         = MOUSE_WHEEL;
    m_nRotateCameraButtonMask = MOUSE_RIGHT_BUTTON;
    m_bDragSinceLastUpdate    = true;
}




//--------------------------------------------------------------------------------------
// Update the view matrix & the model's world matrix based 
//       on user input & elapsed time
//--------------------------------------------------------------------------------------
VOID CModelViewerCamera::FrameMove( FLOAT fElapsedTime )
{
    if( IsKeyDown(m_aKeys[CAM_RESET]) )
        Reset();

    // If no dragged has happend since last time FrameMove is called,
    // and no camera key is held down, then no need to handle again.
    if( !m_bDragSinceLastUpdate && 0 == m_cKeysDown )
        return;
    m_bDragSinceLastUpdate = false;

    // Get keyboard/mouse/gamepad input
    GetInput( m_bEnablePositionMovement, m_nCurrentButtonMask != 0, true, false );

    // Get amount of velocity based on the keyboard input and drag (if any)
    UpdateVelocity( fElapsedTime );

    // Simple euler method to calculate position delta
    D3DXVECTOR3 vPosDelta = m_vVelocity * fElapsedTime;

    // Change the radius from the camera to the model based on wheel scrolling
    if( m_nMouseWheelDelta && m_nZoomButtonMask == MOUSE_WHEEL )
        m_fRadius -= m_nMouseWheelDelta * m_fRadius * 0.1f;
    m_fRadius = __min( m_fMaxRadius, m_fRadius );
    m_fRadius = __max( m_fMinRadius, m_fRadius );
    m_nMouseWheelDelta = 0;

    // Get the inverse of the arcball's rotation matrix
    D3DXMATRIX mCameraRot;
    D3DXMatrixInverse( &mCameraRot, NULL, m_ViewArcBall.GetRotationMatrix() );

    // Transform vectors based on camera's rotation matrix
    D3DXVECTOR3 vWorldUp, vWorldAhead;
    D3DXVECTOR3 vLocalUp    = D3DXVECTOR3(0,1,0);
    D3DXVECTOR3 vLocalAhead = D3DXVECTOR3(0,0,1);
    D3DXVec3TransformCoord( &vWorldUp, &vLocalUp, &mCameraRot );
    D3DXVec3TransformCoord( &vWorldAhead, &vLocalAhead, &mCameraRot );

    // Transform the position delta by the camera's rotation 
    D3DXVECTOR3 vPosDeltaWorld;
    D3DXVec3TransformCoord( &vPosDeltaWorld, &vPosDelta, &mCameraRot );

    // Move the lookAt position 
    m_vLookAt += vPosDeltaWorld;
    if( m_bClipToBoundary )
        ConstrainToBoundary( &m_vLookAt );

    // Update the eye point based on a radius away from the lookAt position
    m_vEye = m_vLookAt - vWorldAhead * m_fRadius;

    // Update the view matrix
    D3DXMatrixLookAtLH( &m_mView, &m_vEye, &m_vLookAt, &vWorldUp );

    D3DXMATRIX mInvView;
    D3DXMatrixInverse( &mInvView, NULL, &m_mView );
    mInvView._41 = mInvView._42 = mInvView._43 = 0;

    D3DXMATRIX mModelLastRotInv;
    D3DXMatrixInverse(&mModelLastRotInv, NULL, &m_mModelLastRot);

    // Accumulate the delta of the arcball's rotation in view space.
    // Note that per-frame delta rotations could be problematic over long periods of time.
    D3DXMATRIX mModelRot;
    mModelRot = *m_WorldArcBall.GetRotationMatrix();
    m_mModelRot *= m_mView * mModelLastRotInv * mModelRot * mInvView;

    if( m_ViewArcBall.IsBeingDragged() && m_bAttachCameraToModel && !IsKeyDown(m_aKeys[CAM_CONTROLDOWN]) )
    {
        // Attach camera to model by inverse of the model rotation
        D3DXMATRIX mCameraLastRotInv;
        D3DXMatrixInverse(&mCameraLastRotInv, NULL, &m_mCameraRotLast);
        D3DXMATRIX mCameraRotDelta = mCameraLastRotInv * mCameraRot; // local to world matrix
        m_mModelRot *= mCameraRotDelta;
    }
    m_mCameraRotLast = mCameraRot; 

    m_mModelLastRot = mModelRot;

    // Since we're accumulating delta rotations, we need to orthonormalize 
    // the matrix to prevent eventual matrix skew
    D3DXVECTOR3* pXBasis = (D3DXVECTOR3*) &m_mModelRot._11;
    D3DXVECTOR3* pYBasis = (D3DXVECTOR3*) &m_mModelRot._21;
    D3DXVECTOR3* pZBasis = (D3DXVECTOR3*) &m_mModelRot._31;
    D3DXVec3Normalize( pXBasis, pXBasis );
    D3DXVec3Cross( pYBasis, pZBasis, pXBasis );
    D3DXVec3Normalize( pYBasis, pYBasis );
    D3DXVec3Cross( pZBasis, pXBasis, pYBasis );

    // Translate the rotation matrix to the same position as the lookAt position
    m_mModelRot._41 = m_vLookAt.x;
    m_mModelRot._42 = m_vLookAt.y;
    m_mModelRot._43 = m_vLookAt.z;

    // Translate world matrix so its at the center of the model
    D3DXMATRIX mTrans;
    D3DXMatrixTranslation( &mTrans, -m_vModelCenter.x, -m_vModelCenter.y, -m_vModelCenter.z );
    m_mWorld = mTrans * m_mModelRot;
}


void CModelViewerCamera::SetDragRect( RECT &rc )
{
    CBaseCamera::SetDragRect( rc );

    m_WorldArcBall.SetOffset( rc.left, rc.top );
    m_ViewArcBall.SetOffset( rc.left, rc.top );
    SetWindow( rc.right - rc.left, rc.bottom - rc.top );
}


//--------------------------------------------------------------------------------------
// Reset the camera's position back to the default
//--------------------------------------------------------------------------------------
VOID CModelViewerCamera::Reset()
{
    CBaseCamera::Reset();

    D3DXMatrixIdentity( &m_mWorld );
    D3DXMatrixIdentity( &m_mModelRot );
    D3DXMatrixIdentity( &m_mModelLastRot );    
    D3DXMatrixIdentity( &m_mCameraRotLast );    

    m_fRadius = m_fDefaultRadius;
    m_WorldArcBall.Reset();
    m_ViewArcBall.Reset();
}


//--------------------------------------------------------------------------------------
// Override for setting the view parameters
//--------------------------------------------------------------------------------------
void CModelViewerCamera::SetViewParams( D3DXVECTOR3* pvEyePt, D3DXVECTOR3* pvLookatPt )
{
    CBaseCamera::SetViewParams( pvEyePt, pvLookatPt );

    // Propogate changes to the member arcball
    D3DXQUATERNION quat;
    D3DXMATRIXA16 mRotation;
    D3DXVECTOR3 vUp(0,1,0);
    D3DXMatrixLookAtLH( &mRotation, pvEyePt, pvLookatPt, &vUp );
    D3DXQuaternionRotationMatrix( &quat, &mRotation );
    m_ViewArcBall.SetQuatNow( quat );

    // Set the radius according to the distance
    D3DXVECTOR3 vEyeToPoint;
    D3DXVec3Subtract( &vEyeToPoint, pvLookatPt, pvEyePt );
    SetRadius( D3DXVec3Length( &vEyeToPoint ) );

    // View information changed. FrameMove should be called.
    m_bDragSinceLastUpdate = true;
}



//--------------------------------------------------------------------------------------
// Call this from your message proc so this class can handle window messages
//--------------------------------------------------------------------------------------
LRESULT CModelViewerCamera::HandleMessages( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam )
{
    CBaseCamera::HandleMessages( hWnd, uMsg, wParam, lParam );

    if( ( (uMsg == WM_LBUTTONDOWN || uMsg == WM_LBUTTONDBLCLK ) && m_nRotateModelButtonMask & MOUSE_LEFT_BUTTON) ||
        ( (uMsg == WM_MBUTTONDOWN || uMsg == WM_MBUTTONDBLCLK ) && m_nRotateModelButtonMask & MOUSE_MIDDLE_BUTTON) ||
        ( (uMsg == WM_RBUTTONDOWN || uMsg == WM_RBUTTONDBLCLK ) && m_nRotateModelButtonMask & MOUSE_RIGHT_BUTTON) )
    {
        int iMouseX = (short)LOWORD(lParam);
        int iMouseY = (short)HIWORD(lParam);
        m_WorldArcBall.OnBegin( iMouseX, iMouseY );
    }

    if( ( (uMsg == WM_LBUTTONDOWN || uMsg == WM_LBUTTONDBLCLK ) && m_nRotateCameraButtonMask & MOUSE_LEFT_BUTTON) ||
        ( (uMsg == WM_MBUTTONDOWN || uMsg == WM_MBUTTONDBLCLK ) && m_nRotateCameraButtonMask & MOUSE_MIDDLE_BUTTON) ||
        ( (uMsg == WM_RBUTTONDOWN || uMsg == WM_RBUTTONDBLCLK ) && m_nRotateCameraButtonMask & MOUSE_RIGHT_BUTTON) )
    {
        int iMouseX = (short)LOWORD(lParam);
        int iMouseY = (short)HIWORD(lParam);
        m_ViewArcBall.OnBegin( iMouseX, iMouseY );
    }

    if( uMsg == WM_MOUSEMOVE )
    {
        int iMouseX = (short)LOWORD(lParam);
        int iMouseY = (short)HIWORD(lParam);
        m_WorldArcBall.OnMove( iMouseX, iMouseY );
        m_ViewArcBall.OnMove( iMouseX, iMouseY );
    }

    if( (uMsg == WM_LBUTTONUP && m_nRotateModelButtonMask & MOUSE_LEFT_BUTTON) ||
        (uMsg == WM_MBUTTONUP && m_nRotateModelButtonMask & MOUSE_MIDDLE_BUTTON) ||
        (uMsg == WM_RBUTTONUP && m_nRotateModelButtonMask & MOUSE_RIGHT_BUTTON) )
    {
        m_WorldArcBall.OnEnd();
    }

    if( (uMsg == WM_LBUTTONUP && m_nRotateCameraButtonMask & MOUSE_LEFT_BUTTON) ||
        (uMsg == WM_MBUTTONUP && m_nRotateCameraButtonMask & MOUSE_MIDDLE_BUTTON) ||
        (uMsg == WM_RBUTTONUP && m_nRotateCameraButtonMask & MOUSE_RIGHT_BUTTON) )
    {
        m_ViewArcBall.OnEnd();
    }

    if( uMsg == WM_CAPTURECHANGED )
    {
        if( (HWND)lParam != hWnd )
        {
            if( (m_nRotateModelButtonMask & MOUSE_LEFT_BUTTON) ||
                (m_nRotateModelButtonMask & MOUSE_MIDDLE_BUTTON) ||
                (m_nRotateModelButtonMask & MOUSE_RIGHT_BUTTON) )
            {
                m_WorldArcBall.OnEnd();
            }
        
            if( (m_nRotateCameraButtonMask & MOUSE_LEFT_BUTTON) ||
                (m_nRotateCameraButtonMask & MOUSE_MIDDLE_BUTTON) ||
                (m_nRotateCameraButtonMask & MOUSE_RIGHT_BUTTON) )
            {
                m_ViewArcBall.OnEnd();
            }
        }
    }

    if( uMsg == WM_LBUTTONDOWN ||
        uMsg == WM_LBUTTONDBLCLK ||
        uMsg == WM_MBUTTONDOWN ||
        uMsg == WM_MBUTTONDBLCLK ||
        uMsg == WM_RBUTTONDOWN ||
        uMsg == WM_RBUTTONDBLCLK ||
        uMsg == WM_LBUTTONUP ||
        uMsg == WM_MBUTTONUP ||
        uMsg == WM_RBUTTONUP ||
        uMsg == WM_MOUSEWHEEL ||
        uMsg == WM_MOUSEMOVE )
    {
        m_bDragSinceLastUpdate = true;
    }

    return FALSE;
}




//--------------------------------------------------------------------------------------
// Desc: Returns a view matrix for rendering to a face of a cubemap.
//--------------------------------------------------------------------------------------
D3DXMATRIX DXUTGetCubeMapViewMatrix( DWORD dwFace )
{
    D3DXVECTOR3 vEyePt   = D3DXVECTOR3( 0.0f, 0.0f, 0.0f );
    D3DXVECTOR3 vLookDir;
    D3DXVECTOR3 vUpDir;

    switch( dwFace )
    {
        case D3DCUBEMAP_FACE_POSITIVE_X:
            vLookDir = D3DXVECTOR3( 1.0f, 0.0f, 0.0f );
            vUpDir   = D3DXVECTOR3( 0.0f, 1.0f, 0.0f );
            break;
        case D3DCUBEMAP_FACE_NEGATIVE_X:
            vLookDir = D3DXVECTOR3(-1.0f, 0.0f, 0.0f );
            vUpDir   = D3DXVECTOR3( 0.0f, 1.0f, 0.0f );
            break;
        case D3DCUBEMAP_FACE_POSITIVE_Y:
            vLookDir = D3DXVECTOR3( 0.0f, 1.0f, 0.0f );
            vUpDir   = D3DXVECTOR3( 0.0f, 0.0f,-1.0f );
            break;
        case D3DCUBEMAP_FACE_NEGATIVE_Y:
            vLookDir = D3DXVECTOR3( 0.0f,-1.0f, 0.0f );
            vUpDir   = D3DXVECTOR3( 0.0f, 0.0f, 1.0f );
            break;
        case D3DCUBEMAP_FACE_POSITIVE_Z:
            vLookDir = D3DXVECTOR3( 0.0f, 0.0f, 1.0f );
            vUpDir   = D3DXVECTOR3( 0.0f, 1.0f, 0.0f );
            break;
        case D3DCUBEMAP_FACE_NEGATIVE_Z:
            vLookDir = D3DXVECTOR3( 0.0f, 0.0f,-1.0f );
            vUpDir   = D3DXVECTOR3( 0.0f, 1.0f, 0.0f );
            break;
    }

    // Set the view transform for this cubemap surface
    D3DXMATRIXA16 mView;
    D3DXMatrixLookAtLH( &mView, &vEyePt, &vLookDir, &vUpDir );
    return mView;
}


//--------------------------------------------------------------------------------------
// Returns the string for the given D3DFORMAT.
//--------------------------------------------------------------------------------------
LPCWSTR DXUTD3DFormatToString( D3DFORMAT format, bool bWithPrefix )
{
    WCHAR* pstr = NULL;
    switch( format )
    {
    case D3DFMT_UNKNOWN:         pstr = L"D3DFMT_UNKNOWN"; break;
    case D3DFMT_R8G8B8:          pstr = L"D3DFMT_R8G8B8"; break;
    case D3DFMT_A8R8G8B8:        pstr = L"D3DFMT_A8R8G8B8"; break;
    case D3DFMT_X8R8G8B8:        pstr = L"D3DFMT_X8R8G8B8"; break;
    case D3DFMT_R5G6B5:          pstr = L"D3DFMT_R5G6B5"; break;
    case D3DFMT_X1R5G5B5:        pstr = L"D3DFMT_X1R5G5B5"; break;
    case D3DFMT_A1R5G5B5:        pstr = L"D3DFMT_A1R5G5B5"; break;
    case D3DFMT_A4R4G4B4:        pstr = L"D3DFMT_A4R4G4B4"; break;
    case D3DFMT_R3G3B2:          pstr = L"D3DFMT_R3G3B2"; break;
    case D3DFMT_A8:              pstr = L"D3DFMT_A8"; break;
    case D3DFMT_A8R3G3B2:        pstr = L"D3DFMT_A8R3G3B2"; break;
    case D3DFMT_X4R4G4B4:        pstr = L"D3DFMT_X4R4G4B4"; break;
    case D3DFMT_A2B10G10R10:     pstr = L"D3DFMT_A2B10G10R10"; break;
    case D3DFMT_A8B8G8R8:        pstr = L"D3DFMT_A8B8G8R8"; break;
    case D3DFMT_X8B8G8R8:        pstr = L"D3DFMT_X8B8G8R8"; break;
    case D3DFMT_G16R16:          pstr = L"D3DFMT_G16R16"; break;
    case D3DFMT_A2R10G10B10:     pstr = L"D3DFMT_A2R10G10B10"; break;
    case D3DFMT_A16B16G16R16:    pstr = L"D3DFMT_A16B16G16R16"; break;
    case D3DFMT_A8P8:            pstr = L"D3DFMT_A8P8"; break;
    case D3DFMT_P8:              pstr = L"D3DFMT_P8"; break;
    case D3DFMT_L8:              pstr = L"D3DFMT_L8"; break;
    case D3DFMT_A8L8:            pstr = L"D3DFMT_A8L8"; break;
    case D3DFMT_A4L4:            pstr = L"D3DFMT_A4L4"; break;
    case D3DFMT_V8U8:            pstr = L"D3DFMT_V8U8"; break;
    case D3DFMT_L6V5U5:          pstr = L"D3DFMT_L6V5U5"; break;
    case D3DFMT_X8L8V8U8:        pstr = L"D3DFMT_X8L8V8U8"; break;
    case D3DFMT_Q8W8V8U8:        pstr = L"D3DFMT_Q8W8V8U8"; break;
    case D3DFMT_V16U16:          pstr = L"D3DFMT_V16U16"; break;
    case D3DFMT_A2W10V10U10:     pstr = L"D3DFMT_A2W10V10U10"; break;
    case D3DFMT_UYVY:            pstr = L"D3DFMT_UYVY"; break;
    case D3DFMT_YUY2:            pstr = L"D3DFMT_YUY2"; break;
    case D3DFMT_DXT1:            pstr = L"D3DFMT_DXT1"; break;
    case D3DFMT_DXT2:            pstr = L"D3DFMT_DXT2"; break;
    case D3DFMT_DXT3:            pstr = L"D3DFMT_DXT3"; break;
    case D3DFMT_DXT4:            pstr = L"D3DFMT_DXT4"; break;
    case D3DFMT_DXT5:            pstr = L"D3DFMT_DXT5"; break;
    case D3DFMT_D16_LOCKABLE:    pstr = L"D3DFMT_D16_LOCKABLE"; break;
    case D3DFMT_D32:             pstr = L"D3DFMT_D32"; break;
    case D3DFMT_D15S1:           pstr = L"D3DFMT_D15S1"; break;
    case D3DFMT_D24S8:           pstr = L"D3DFMT_D24S8"; break;
    case D3DFMT_D24X8:           pstr = L"D3DFMT_D24X8"; break;
    case D3DFMT_D24X4S4:         pstr = L"D3DFMT_D24X4S4"; break;
    case D3DFMT_D16:             pstr = L"D3DFMT_D16"; break;
    case D3DFMT_L16:             pstr = L"D3DFMT_L16"; break;
    case D3DFMT_VERTEXDATA:      pstr = L"D3DFMT_VERTEXDATA"; break;
    case D3DFMT_INDEX16:         pstr = L"D3DFMT_INDEX16"; break;
    case D3DFMT_INDEX32:         pstr = L"D3DFMT_INDEX32"; break;
    case D3DFMT_Q16W16V16U16:    pstr = L"D3DFMT_Q16W16V16U16"; break;
    case D3DFMT_MULTI2_ARGB8:    pstr = L"D3DFMT_MULTI2_ARGB8"; break;
    case D3DFMT_R16F:            pstr = L"D3DFMT_R16F"; break;
    case D3DFMT_G16R16F:         pstr = L"D3DFMT_G16R16F"; break;
    case D3DFMT_A16B16G16R16F:   pstr = L"D3DFMT_A16B16G16R16F"; break;
    case D3DFMT_R32F:            pstr = L"D3DFMT_R32F"; break;
    case D3DFMT_G32R32F:         pstr = L"D3DFMT_G32R32F"; break;
    case D3DFMT_A32B32G32R32F:   pstr = L"D3DFMT_A32B32G32R32F"; break;
    case D3DFMT_CxV8U8:          pstr = L"D3DFMT_CxV8U8"; break;
    default:                     pstr = L"Unknown format"; break;
    }
    if( bWithPrefix || wcsstr( pstr, L"D3DFMT_" )== NULL )
        return pstr;
    else
        return pstr + lstrlen( L"D3DFMT_" );
}



//--------------------------------------------------------------------------------------
// Outputs to the debug stream a formatted Unicode string with a variable-argument list.
//--------------------------------------------------------------------------------------
VOID DXUTOutputDebugStringW( LPCWSTR strMsg, ... )
{
#if defined(DEBUG) || defined(_DEBUG)
    WCHAR strBuffer[512];
    
    va_list args;
    va_start(args, strMsg);
    StringCchVPrintfW( strBuffer, 512, strMsg, args );
    strBuffer[511] = L'\0';
    va_end(args);

    OutputDebugString( strBuffer );
#else
    UNREFERENCED_PARAMETER(strMsg);
#endif
}


//--------------------------------------------------------------------------------------
// Outputs to the debug stream a formatted MBCS string with a variable-argument list.
//--------------------------------------------------------------------------------------
VOID DXUTOutputDebugStringA( LPCSTR strMsg, ... )
{
#if defined(DEBUG) || defined(_DEBUG)
    CHAR strBuffer[512];
    
    va_list args;
    va_start(args, strMsg);
    StringCchVPrintfA( strBuffer, 512, strMsg, args );
    strBuffer[511] = '\0';
    va_end(args);

    OutputDebugStringA( strBuffer );
#else
    UNREFERENCED_PARAMETER(strMsg);
#endif
}


//--------------------------------------------------------------------------------------
CDXUTLineManager::CDXUTLineManager()
{
    m_pd3dDevice = NULL;
    m_pD3DXLine = NULL;
}


//--------------------------------------------------------------------------------------
CDXUTLineManager::~CDXUTLineManager()
{
    OnDeletedDevice();
}


//--------------------------------------------------------------------------------------
HRESULT CDXUTLineManager::OnCreatedDevice( IDirect3DDevice9* pd3dDevice )
{
    m_pd3dDevice = pd3dDevice;

    HRESULT hr;
    hr = D3DXCreateLine( m_pd3dDevice, &m_pD3DXLine );
    if( FAILED(hr) )
        return hr;

    return S_OK;
}


//--------------------------------------------------------------------------------------
HRESULT CDXUTLineManager::OnResetDevice()
{
    if( m_pD3DXLine )
        m_pD3DXLine->OnResetDevice();

    return S_OK;
}


//--------------------------------------------------------------------------------------
HRESULT CDXUTLineManager::OnRender()
{
    HRESULT hr;
    if( NULL == m_pD3DXLine )
        return E_INVALIDARG;

    bool bDrawingHasBegun = false;
    float fLastWidth = 0.0f;
    bool bLastAntiAlias = false;
    
    for( int i=0; i<m_LinesList.GetSize(); i++ )
    {
        LINE_NODE* pLineNode = m_LinesList.GetAt(i);
        if( pLineNode )
        {
            if( !bDrawingHasBegun || 
                fLastWidth != pLineNode->fWidth || 
                bLastAntiAlias != pLineNode->bAntiAlias )
            {
                if( bDrawingHasBegun )
                {
                    hr = m_pD3DXLine->End();
                    if( FAILED(hr) )
                        return hr;
                }

                m_pD3DXLine->SetWidth( pLineNode->fWidth );
                m_pD3DXLine->SetAntialias( pLineNode->bAntiAlias );

                fLastWidth = pLineNode->fWidth;
                bLastAntiAlias = pLineNode->bAntiAlias;

                hr = m_pD3DXLine->Begin();
                if( FAILED(hr) )
                    return hr;
                bDrawingHasBegun = true;
            }

            hr = m_pD3DXLine->Draw( pLineNode->pVertexList, pLineNode->dwVertexListCount, pLineNode->Color );
            if( FAILED(hr) )
                return hr;
        }
    }

    if( bDrawingHasBegun )
    {
        hr = m_pD3DXLine->End();
        if( FAILED(hr) )
            return hr;
    }

    return S_OK;
}


//--------------------------------------------------------------------------------------
HRESULT CDXUTLineManager::OnLostDevice()
{
    if( m_pD3DXLine )
        m_pD3DXLine->OnLostDevice();

    return S_OK;
}


//--------------------------------------------------------------------------------------
HRESULT CDXUTLineManager::OnDeletedDevice()
{
    RemoveAllLines();
    SAFE_RELEASE( m_pD3DXLine );

    return S_OK;
}


//--------------------------------------------------------------------------------------
HRESULT CDXUTLineManager::AddLine( int* pnLineID, D3DXVECTOR2* pVertexList, DWORD dwVertexListCount, D3DCOLOR Color, float fWidth, float fScaleRatio, bool bAntiAlias )
{
    if( pVertexList == NULL || dwVertexListCount == 0 )
        return E_INVALIDARG;

    LINE_NODE* pLineNode = new LINE_NODE;
    if( pLineNode == NULL )
        return E_OUTOFMEMORY;
    ZeroMemory( pLineNode, sizeof(LINE_NODE) );

    pLineNode->nLineID = m_LinesList.GetSize();
    pLineNode->Color = Color;
    pLineNode->fWidth = fWidth;
    pLineNode->bAntiAlias = bAntiAlias;
    pLineNode->dwVertexListCount = dwVertexListCount;

    if( pnLineID )
        *pnLineID = pLineNode->nLineID;

    pLineNode->pVertexList = new D3DXVECTOR2[dwVertexListCount];
    if( pLineNode->pVertexList == NULL )
    {
        delete pLineNode;
        return E_OUTOFMEMORY;
    }
    for( DWORD i=0; i<dwVertexListCount; i++ )
    {
        pLineNode->pVertexList[i] = pVertexList[i] * fScaleRatio;
    }

    m_LinesList.Add( pLineNode );

    return S_OK;
}


//--------------------------------------------------------------------------------------
HRESULT CDXUTLineManager::AddRect( int* pnLineID, RECT rc, D3DCOLOR Color, float fWidth, float fScaleRatio, bool bAntiAlias )
{
    if( fWidth > 2.0f )
    {
        D3DXVECTOR2 vertexList[8];

        vertexList[0].x = (float)rc.left;
        vertexList[0].y = (float)rc.top - (fWidth/2.0f);

        vertexList[1].x = (float)rc.left;
        vertexList[1].y = (float)rc.bottom + (fWidth/2.0f);

        vertexList[2].x = (float)rc.left;
        vertexList[2].y = (float)rc.bottom - 0.5f;

        vertexList[3].x = (float)rc.right;
        vertexList[3].y = (float)rc.bottom - 0.5f;

        vertexList[4].x = (float)rc.right;
        vertexList[4].y = (float)rc.bottom + (fWidth/2.0f);

        vertexList[5].x = (float)rc.right;
        vertexList[5].y = (float)rc.top - (fWidth/2.0f);

        vertexList[6].x = (float)rc.right;
        vertexList[6].y = (float)rc.top;

        vertexList[7].x = (float)rc.left;
        vertexList[7].y = (float)rc.top;
        
        return AddLine( pnLineID, vertexList, 8, Color, fWidth, fScaleRatio, bAntiAlias );
    }
    else
    {
        D3DXVECTOR2 vertexList[5];
        vertexList[0].x = (float)rc.left;
        vertexList[0].y = (float)rc.top;

        vertexList[1].x = (float)rc.left;
        vertexList[1].y = (float)rc.bottom;

        vertexList[2].x = (float)rc.right;
        vertexList[2].y = (float)rc.bottom;

        vertexList[3].x = (float)rc.right;
        vertexList[3].y = (float)rc.top;

        vertexList[4].x = (float)rc.left;
        vertexList[4].y = (float)rc.top;
        
        return AddLine( pnLineID, vertexList, 5, Color, fWidth, fScaleRatio, bAntiAlias );
    }
}



//--------------------------------------------------------------------------------------
HRESULT CDXUTLineManager::RemoveLine( int nLineID )
{
    for( int i=0; i<m_LinesList.GetSize(); i++ )
    {
        LINE_NODE* pLineNode = m_LinesList.GetAt(i);
        if( pLineNode && pLineNode->nLineID == nLineID )
        {
            SAFE_DELETE_ARRAY( pLineNode->pVertexList );
            delete pLineNode;
            m_LinesList.SetAt(i, NULL);
        }
    }

    return S_OK;
}


//--------------------------------------------------------------------------------------
HRESULT CDXUTLineManager::RemoveAllLines()
{
    for( int i=0; i<m_LinesList.GetSize(); i++ )
    {
        LINE_NODE* pLineNode = m_LinesList.GetAt(i);
        if( pLineNode )
        {
            SAFE_DELETE_ARRAY( pLineNode->pVertexList );
            delete pLineNode;
        }
    }
    m_LinesList.RemoveAll();

    return S_OK;
}


//--------------------------------------------------------------------------------------
CDXUTTextHelper::CDXUTTextHelper( ID3DXFont* pFont, ID3DXSprite* pSprite, int nLineHeight )
{
    m_pFont = pFont;
    m_pSprite = pSprite;
    m_clr = D3DXCOLOR(1,1,1,1);
    m_pt.x = 0; 
    m_pt.y = 0; 
    m_nLineHeight = nLineHeight;
}


//--------------------------------------------------------------------------------------
HRESULT CDXUTTextHelper::DrawFormattedTextLine( const WCHAR* strMsg, ... )
{
    WCHAR strBuffer[512];
    
    va_list args;
    va_start(args, strMsg);
    StringCchVPrintf( strBuffer, 512, strMsg, args );
    strBuffer[511] = L'\0';
    va_end(args);

    return DrawTextLine( strBuffer );
}


//--------------------------------------------------------------------------------------
HRESULT CDXUTTextHelper::DrawTextLine( const WCHAR* strMsg )
{
    if( NULL == m_pFont ) 
        return DXUT_ERR_MSGBOX( L"DrawTextLine", E_INVALIDARG );

    HRESULT hr;
    RECT rc;
    SetRect( &rc, m_pt.x, m_pt.y, 0, 0 ); 
    hr = m_pFont->DrawText( m_pSprite, strMsg, -1, &rc, DT_NOCLIP, m_clr );
    if( FAILED(hr) )
        return DXTRACE_ERR_MSGBOX( L"DrawText", hr );

    m_pt.y += m_nLineHeight;

    return S_OK;
}


HRESULT CDXUTTextHelper::DrawFormattedTextLine( RECT &rc, DWORD dwFlags, const WCHAR* strMsg, ... )
{
    WCHAR strBuffer[512];
    
    va_list args;
    va_start(args, strMsg);
    StringCchVPrintf( strBuffer, 512, strMsg, args );
    strBuffer[511] = L'\0';
    va_end(args);

    return DrawTextLine( rc, dwFlags, strBuffer );
}


HRESULT CDXUTTextHelper::DrawTextLine( RECT &rc, DWORD dwFlags, const WCHAR* strMsg )
{
    if( NULL == m_pFont ) 
        return DXUT_ERR_MSGBOX( L"DrawTextLine", E_INVALIDARG );

    HRESULT hr;
    hr = m_pFont->DrawText( m_pSprite, strMsg, -1, &rc, dwFlags, m_clr );
    if( FAILED(hr) )
        return DXTRACE_ERR_MSGBOX( L"DrawText", hr );

    m_pt.y += m_nLineHeight;

    return S_OK;
}


//--------------------------------------------------------------------------------------
void CDXUTTextHelper::Begin()
{
    if( m_pSprite )
        m_pSprite->Begin( D3DXSPRITE_ALPHABLEND | D3DXSPRITE_SORT_TEXTURE );
}
void CDXUTTextHelper::End()
{
    if( m_pSprite )
        m_pSprite->End();
}


//--------------------------------------------------------------------------------------
IDirect3DDevice9* CDXUTDirectionWidget::s_pd3dDevice = NULL;
ID3DXEffect*      CDXUTDirectionWidget::s_pEffect = NULL;       
ID3DXMesh*        CDXUTDirectionWidget::s_pMesh = NULL;    


//--------------------------------------------------------------------------------------
CDXUTDirectionWidget::CDXUTDirectionWidget()
{
    m_fRadius = 1.0f;
    m_vDefaultDir = D3DXVECTOR3(0,1,0);
    m_vCurrentDir = m_vDefaultDir;
    m_nRotateMask = MOUSE_RIGHT_BUTTON;

    D3DXMatrixIdentity( &m_mView );
    D3DXMatrixIdentity( &m_mRot );
    D3DXMatrixIdentity( &m_mRotSnapshot );
}


//--------------------------------------------------------------------------------------
HRESULT CDXUTDirectionWidget::StaticOnCreateDevice( IDirect3DDevice9* pd3dDevice )
{
    HRESULT hr;

    s_pd3dDevice = pd3dDevice;

    const char* g_strBuffer = 
    "float4 g_MaterialDiffuseColor;      // Material's diffuse color\r\n"
    "float3 g_LightDir;                  // Light's direction in world space\r\n"
    "float4x4 g_mWorld;                  // World matrix for object\r\n"
    "float4x4 g_mWorldViewProjection;    // World * View * Projection matrix\r\n"
    "\r\n"
    "struct VS_OUTPUT\r\n"
    "{\r\n"
    "    float4 Position   : POSITION;   // vertex position\r\n"
    "    float4 Diffuse    : COLOR0;     // vertex diffuse color\r\n"
    "};\r\n"
    "\r\n"
    "VS_OUTPUT RenderWith1LightNoTextureVS( float4 vPos : POSITION,\r\n"
    "                                       float3 vNormal : NORMAL )\r\n"
    "{\r\n"
    "    VS_OUTPUT Output;\r\n"
    "\r\n"
    "    // Transform the position from object space to homogeneous projection space\r\n"
    "    Output.Position = mul(vPos, g_mWorldViewProjection);\r\n"
    "\r\n"
    "    // Transform the normal from object space to world space\r\n"
    "    float3 vNormalWorldSpace;\r\n"
    "    vNormalWorldSpace = normalize(mul(vNormal, (float3x3)g_mWorld)); // normal (world space)\r\n"
    "\r\n"
    "    // Compute simple directional lighting equation\r\n"
    "    Output.Diffuse.rgb = g_MaterialDiffuseColor * max(0,dot(vNormalWorldSpace, g_LightDir));\r\n"
    "    Output.Diffuse.a = 1.0f;\r\n"
    "\r\n"
    "    return Output;\r\n"
    "}\r\n"
    "\r\n"
    "float4 RenderWith1LightNoTexturePS( float4 Diffuse : COLOR0 ) : COLOR0\r\n"
    "{\r\n"
    "    return Diffuse;\r\n"
    "}\r\n"
    "\r\n"
    "technique RenderWith1LightNoTexture\r\n"
    "{\r\n"
    "    pass P0\r\n"
    "    {\r\n"
    "        VertexShader = compile vs_2_0 RenderWith1LightNoTextureVS();\r\n"
    "        PixelShader  = compile ps_2_0 RenderWith1LightNoTexturePS();\r\n"
    "    }\r\n"
    "}\r\n"
    "";

    UINT dwBufferSize = (UINT)strlen(g_strBuffer) + 1; 

    V_RETURN( D3DXCreateEffect( s_pd3dDevice, g_strBuffer, dwBufferSize, NULL, NULL, D3DXFX_NOT_CLONEABLE, NULL, &s_pEffect, NULL ) );

    // Load the mesh with D3DX and get back a ID3DXMesh*.  For this
    // sample we'll ignore the X file's embedded materials since we know 
    // exactly the model we're loading.  See the mesh samples such as
    // "OptimizedMesh" for a more generic mesh loading example.
    V_RETURN( DXUTCreateArrowMeshFromInternalArray( s_pd3dDevice, &s_pMesh ) );

    // Optimize the mesh for this graphics card's vertex cache 
    // so when rendering the mesh's triangle list the vertices will 
    // cache hit more often so it won't have to re-execute the vertex shader 
    // on those vertices so it will improve perf.     
    DWORD* rgdwAdjacency = new DWORD[s_pMesh->GetNumFaces() * 3];
    if( rgdwAdjacency == NULL )
        return E_OUTOFMEMORY;
    V( s_pMesh->GenerateAdjacency(1e-6f,rgdwAdjacency) );
    V( s_pMesh->OptimizeInplace(D3DXMESHOPT_VERTEXCACHE, rgdwAdjacency, NULL, NULL, NULL) );
    delete []rgdwAdjacency;

    return S_OK;
}


//--------------------------------------------------------------------------------------
HRESULT CDXUTDirectionWidget::OnResetDevice( const D3DSURFACE_DESC* pBackBufferSurfaceDesc )
{
    m_ArcBall.SetWindow( pBackBufferSurfaceDesc->Width, pBackBufferSurfaceDesc->Height );
    return S_OK;
}


//--------------------------------------------------------------------------------------
void CDXUTDirectionWidget::StaticOnLostDevice()
{
    if( s_pEffect )
        s_pEffect->OnLostDevice();
}


//--------------------------------------------------------------------------------------
void CDXUTDirectionWidget::StaticOnDestroyDevice()
{
    SAFE_RELEASE(s_pEffect);
    SAFE_RELEASE(s_pMesh);
}    


//--------------------------------------------------------------------------------------
LRESULT CDXUTDirectionWidget::HandleMessages( HWND hWnd, UINT uMsg, 
                                              WPARAM wParam, LPARAM lParam )
{
    switch( uMsg )
    {
        case WM_LBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_RBUTTONDOWN:
        {
            if( ((m_nRotateMask & MOUSE_LEFT_BUTTON) != 0 && uMsg == WM_LBUTTONDOWN) ||
                ((m_nRotateMask & MOUSE_MIDDLE_BUTTON) != 0 && uMsg == WM_MBUTTONDOWN) ||
                ((m_nRotateMask & MOUSE_RIGHT_BUTTON) != 0 && uMsg == WM_RBUTTONDOWN) )
            {
                int iMouseX = (int)(short)LOWORD(lParam);
                int iMouseY = (int)(short)HIWORD(lParam);
                m_ArcBall.OnBegin( iMouseX, iMouseY );
                SetCapture(hWnd);
            }
            return TRUE;
        }

        case WM_MOUSEMOVE:
        {
            if( m_ArcBall.IsBeingDragged() )
            {
                int iMouseX = (int)(short)LOWORD(lParam);
                int iMouseY = (int)(short)HIWORD(lParam);
                m_ArcBall.OnMove( iMouseX, iMouseY );
                UpdateLightDir();
            }
            return TRUE;
        }

        case WM_LBUTTONUP:
        case WM_MBUTTONUP:
        case WM_RBUTTONUP:
        {
            if( ((m_nRotateMask & MOUSE_LEFT_BUTTON) != 0 && uMsg == WM_LBUTTONUP) ||
                ((m_nRotateMask & MOUSE_MIDDLE_BUTTON) != 0 && uMsg == WM_MBUTTONUP) ||
                ((m_nRotateMask & MOUSE_RIGHT_BUTTON) != 0 && uMsg == WM_RBUTTONUP) )
            {
                m_ArcBall.OnEnd();
                ReleaseCapture();
            }

            UpdateLightDir();
            return TRUE;
        }

        case WM_CAPTURECHANGED:
        {
            if( (HWND)lParam != hWnd )
            {
                if( (m_nRotateMask & MOUSE_LEFT_BUTTON) ||
                    (m_nRotateMask & MOUSE_MIDDLE_BUTTON) ||
                    (m_nRotateMask & MOUSE_RIGHT_BUTTON) )
                {
                    m_ArcBall.OnEnd();
                    ReleaseCapture();
                }
            }

            return TRUE;
        }
    }

    return 0;
}


//--------------------------------------------------------------------------------------
HRESULT CDXUTDirectionWidget::OnRender( D3DXCOLOR color, const D3DXMATRIX* pmView, 
                                        const D3DXMATRIX* pmProj, const D3DXVECTOR3* pEyePt )
{
    m_mView = *pmView;

    // Render the light spheres so the user can visually see the light dir
    UINT iPass, cPasses;
    D3DXMATRIX mRotate;
    D3DXMATRIX mScale;
    D3DXMATRIX mTrans;
    D3DXMATRIXA16 mWorldViewProj;
    HRESULT hr;

    V( s_pEffect->SetTechnique( "RenderWith1LightNoTexture" ) );
    V( s_pEffect->SetVector( "g_MaterialDiffuseColor", (D3DXVECTOR4*)&color ) );

    D3DXVECTOR3 vEyePt;
    D3DXVec3Normalize( &vEyePt, pEyePt );
    V( s_pEffect->SetValue( "g_LightDir", &vEyePt, sizeof(D3DXVECTOR3) ) );

    // Rotate arrow model to point towards origin
    D3DXMATRIX mRotateA, mRotateB;
    D3DXVECTOR3 vAt = D3DXVECTOR3(0,0,0);
    D3DXVECTOR3 vUp = D3DXVECTOR3(0,1,0);
    D3DXMatrixRotationX( &mRotateB, D3DX_PI );
    D3DXMatrixLookAtLH( &mRotateA, &m_vCurrentDir, &vAt, &vUp );
    D3DXMatrixInverse( &mRotateA, NULL, &mRotateA );
    mRotate = mRotateB * mRotateA;

    D3DXVECTOR3 vL = m_vCurrentDir * m_fRadius * 1.0f;
    D3DXMatrixTranslation( &mTrans, vL.x, vL.y, vL.z );
    D3DXMatrixScaling( &mScale, m_fRadius*0.2f, m_fRadius*0.2f, m_fRadius*0.2f );

    D3DXMATRIX mWorld = mRotate * mScale * mTrans;
    mWorldViewProj = mWorld * (m_mView) * (*pmProj);

    V( s_pEffect->SetMatrix( "g_mWorldViewProjection", &mWorldViewProj ) );
    V( s_pEffect->SetMatrix( "g_mWorld", &mWorld ) );

    for( int iSubset=0; iSubset<2; iSubset++ )
    {
        V( s_pEffect->Begin(&cPasses, 0) );
        for (iPass = 0; iPass < cPasses; iPass++)
        {
            V( s_pEffect->BeginPass(iPass) );
            V( s_pMesh->DrawSubset(iSubset) );
            V( s_pEffect->EndPass() );
        }
        V( s_pEffect->End() );
    }

    return S_OK;
}


//--------------------------------------------------------------------------------------
HRESULT CDXUTDirectionWidget::UpdateLightDir()
{
    D3DXMATRIX mInvView;
    D3DXMatrixInverse(&mInvView, NULL, &m_mView);
    mInvView._41 = mInvView._42 = mInvView._43 = 0;

    D3DXMATRIX mLastRotInv;
    D3DXMatrixInverse(&mLastRotInv, NULL, &m_mRotSnapshot);

    D3DXMATRIX mRot = *m_ArcBall.GetRotationMatrix();
    m_mRotSnapshot = mRot;

    // Accumulate the delta of the arcball's rotation in view space.
    // Note that per-frame delta rotations could be problematic over long periods of time.
    m_mRot *= m_mView * mLastRotInv * mRot * mInvView;

    // Since we're accumulating delta rotations, we need to orthonormalize 
    // the matrix to prevent eventual matrix skew
    D3DXVECTOR3* pXBasis = (D3DXVECTOR3*) &m_mRot._11;
    D3DXVECTOR3* pYBasis = (D3DXVECTOR3*) &m_mRot._21;
    D3DXVECTOR3* pZBasis = (D3DXVECTOR3*) &m_mRot._31;
    D3DXVec3Normalize( pXBasis, pXBasis );
    D3DXVec3Cross( pYBasis, pZBasis, pXBasis );
    D3DXVec3Normalize( pYBasis, pYBasis );
    D3DXVec3Cross( pZBasis, pXBasis, pYBasis );

    // Transform the default direction vector by the light's rotation matrix
    D3DXVec3TransformNormal( &m_vCurrentDir, &m_vDefaultDir, &m_mRot );

    return S_OK;
}

//--------------------------------------------------------------------------------------
// Direct3D9 dynamic linking support -- calls top-level D3D9 APIs with graceful
// failure if APIs are not present.
//--------------------------------------------------------------------------------------

// Function prototypes
typedef IDirect3D9* (WINAPI * LPDIRECT3DCREATE9) (UINT);
typedef INT         (WINAPI * LPD3DPERF_BEGINEVENT)(D3DCOLOR, LPCWSTR);
typedef INT         (WINAPI * LPD3DPERF_ENDEVENT)(void);
typedef VOID        (WINAPI * LPD3DPERF_SETMARKER)(D3DCOLOR, LPCWSTR);
typedef VOID        (WINAPI * LPD3DPERF_SETREGION)(D3DCOLOR, LPCWSTR);
typedef BOOL        (WINAPI * LPD3DPERF_QUERYREPEATFRAME)(void);
typedef VOID        (WINAPI * LPD3DPERF_SETOPTIONS)( DWORD dwOptions );
typedef DWORD       (WINAPI * LPD3DPERF_GETSTATUS)( void );

// Module and function pointers
static HMODULE s_hModD3D9 = NULL;
static LPDIRECT3DCREATE9 s_DynamicDirect3DCreate9 = NULL;
static LPD3DPERF_BEGINEVENT s_DynamicD3DPERF_BeginEvent = NULL;
static LPD3DPERF_ENDEVENT s_DynamicD3DPERF_EndEvent = NULL;
static LPD3DPERF_SETMARKER s_DynamicD3DPERF_SetMarker = NULL;
static LPD3DPERF_SETREGION s_DynamicD3DPERF_SetRegion = NULL;
static LPD3DPERF_QUERYREPEATFRAME s_DynamicD3DPERF_QueryRepeatFrame = NULL;
static LPD3DPERF_SETOPTIONS s_DynamicD3DPERF_SetOptions = NULL;
static LPD3DPERF_GETSTATUS s_DynamicD3DPERF_GetStatus = NULL;

// Ensure function pointers are initialized
static bool DXUT_EnsureD3DAPIs( void )
{
    // If module is non-NULL, this function has already been called.  Note
    // that this doesn't guarantee that all D3D9 procaddresses were found.
    if( s_hModD3D9 != NULL )
        return true;

    // This may fail if DirectX 9 isn't installed
    s_hModD3D9 = LoadLibrary( L"d3d9.dll" );
    if( s_hModD3D9 == NULL ) 
        return false;
    s_DynamicDirect3DCreate9 = (LPDIRECT3DCREATE9)GetProcAddress( s_hModD3D9, "Direct3DCreate9" );
    s_DynamicD3DPERF_BeginEvent = (LPD3DPERF_BEGINEVENT)GetProcAddress( s_hModD3D9, "D3DPERF_BeginEvent" );
    s_DynamicD3DPERF_EndEvent = (LPD3DPERF_ENDEVENT)GetProcAddress( s_hModD3D9, "D3DPERF_EndEvent" );
    s_DynamicD3DPERF_SetMarker = (LPD3DPERF_SETMARKER)GetProcAddress( s_hModD3D9, "D3DPERF_SetMarker" );
    s_DynamicD3DPERF_SetRegion = (LPD3DPERF_SETREGION)GetProcAddress( s_hModD3D9, "D3DPERF_SetRegion" );
    s_DynamicD3DPERF_QueryRepeatFrame = (LPD3DPERF_QUERYREPEATFRAME)GetProcAddress( s_hModD3D9, "D3DPERF_QueryRepeatFrame" );
    s_DynamicD3DPERF_SetOptions = (LPD3DPERF_SETOPTIONS)GetProcAddress( s_hModD3D9, "D3DPERF_SetOptions" );
    s_DynamicD3DPERF_GetStatus = (LPD3DPERF_GETSTATUS)GetProcAddress( s_hModD3D9, "D3DPERF_GetStatus" );
    return true;
}

IDirect3D9 * WINAPI DXUT_Dynamic_Direct3DCreate9(UINT SDKVersion) 
{
    if( DXUT_EnsureD3DAPIs() && s_DynamicDirect3DCreate9 != NULL )
        return s_DynamicDirect3DCreate9( SDKVersion );
    else
        return NULL;
}

int WINAPI DXUT_Dynamic_D3DPERF_BeginEvent( D3DCOLOR col, LPCWSTR wszName )
{
    if( DXUT_EnsureD3DAPIs() && s_DynamicD3DPERF_BeginEvent != NULL )
        return s_DynamicD3DPERF_BeginEvent( col, wszName );
    else
        return -1;
}

int WINAPI DXUT_Dynamic_D3DPERF_EndEvent( void )
{
    if( DXUT_EnsureD3DAPIs() && s_DynamicD3DPERF_EndEvent != NULL )
        return s_DynamicD3DPERF_EndEvent();
    else
        return -1;
}

void WINAPI DXUT_Dynamic_D3DPERF_SetMarker( D3DCOLOR col, LPCWSTR wszName )
{
    if( DXUT_EnsureD3DAPIs() && s_DynamicD3DPERF_SetMarker != NULL )
        s_DynamicD3DPERF_SetMarker( col, wszName );
}

void WINAPI DXUT_Dynamic_D3DPERF_SetRegion( D3DCOLOR col, LPCWSTR wszName )
{
    if( DXUT_EnsureD3DAPIs() && s_DynamicD3DPERF_SetRegion != NULL )
        s_DynamicD3DPERF_SetRegion( col, wszName );
}

BOOL WINAPI DXUT_Dynamic_D3DPERF_QueryRepeatFrame( void )
{
    if( DXUT_EnsureD3DAPIs() && s_DynamicD3DPERF_QueryRepeatFrame != NULL )
        return s_DynamicD3DPERF_QueryRepeatFrame();
    else
        return FALSE;
}

void WINAPI DXUT_Dynamic_D3DPERF_SetOptions( DWORD dwOptions )
{
    if( DXUT_EnsureD3DAPIs() && s_DynamicD3DPERF_SetOptions != NULL )
        s_DynamicD3DPERF_SetOptions( dwOptions );
}

DWORD WINAPI DXUT_Dynamic_D3DPERF_GetStatus( void )
{
    if( DXUT_EnsureD3DAPIs() && s_DynamicD3DPERF_GetStatus != NULL )
        return s_DynamicD3DPERF_GetStatus();
    else
        return 0;
}


//--------------------------------------------------------------------------------------
// Trace a string description of a decl 
//--------------------------------------------------------------------------------------
void DXUTTraceDecl( D3DVERTEXELEMENT9 decl[MAX_FVF_DECL_SIZE] )
{
    int iDecl=0;
    for( iDecl=0; iDecl<MAX_FVF_DECL_SIZE; iDecl++ )
    {
        if( decl[iDecl].Stream == 0xFF )
            break;

        DXUTOutputDebugString( L"decl[%d]=Stream:%d, Offset:%d, %s, %s, %s, UsageIndex:%d\n", iDecl, 
                    decl[iDecl].Stream,
                    decl[iDecl].Offset,
                    DXUTTraceD3DDECLTYPEtoString( decl[iDecl].Type ),
                    DXUTTraceD3DDECLMETHODtoString( decl[iDecl].Method ),
                    DXUTTraceD3DDECLUSAGEtoString( decl[iDecl].Usage ),
                    decl[iDecl].UsageIndex );
    }

    DXUTOutputDebugString( L"decl[%d]=D3DDECL_END\n", iDecl );
}


//--------------------------------------------------------------------------------------
WCHAR* DXUTTraceD3DDECLTYPEtoString( BYTE t )
{
    switch( t )
    {
        case D3DDECLTYPE_FLOAT1: return L"D3DDECLTYPE_FLOAT1";
        case D3DDECLTYPE_FLOAT2: return L"D3DDECLTYPE_FLOAT2";
        case D3DDECLTYPE_FLOAT3: return L"D3DDECLTYPE_FLOAT3";
        case D3DDECLTYPE_FLOAT4: return L"D3DDECLTYPE_FLOAT4";
        case D3DDECLTYPE_D3DCOLOR: return L"D3DDECLTYPE_D3DCOLOR";
        case D3DDECLTYPE_UBYTE4: return L"D3DDECLTYPE_UBYTE4";
        case D3DDECLTYPE_SHORT2: return L"D3DDECLTYPE_SHORT2";
        case D3DDECLTYPE_SHORT4: return L"D3DDECLTYPE_SHORT4";
        case D3DDECLTYPE_UBYTE4N: return L"D3DDECLTYPE_UBYTE4N";
        case D3DDECLTYPE_SHORT2N: return L"D3DDECLTYPE_SHORT2N";
        case D3DDECLTYPE_SHORT4N: return L"D3DDECLTYPE_SHORT4N";
        case D3DDECLTYPE_USHORT2N: return L"D3DDECLTYPE_USHORT2N";
        case D3DDECLTYPE_USHORT4N: return L"D3DDECLTYPE_USHORT4N";
        case D3DDECLTYPE_UDEC3: return L"D3DDECLTYPE_UDEC3";
        case D3DDECLTYPE_DEC3N: return L"D3DDECLTYPE_DEC3N";
        case D3DDECLTYPE_FLOAT16_2: return L"D3DDECLTYPE_FLOAT16_2";
        case D3DDECLTYPE_FLOAT16_4: return L"D3DDECLTYPE_FLOAT16_4";
        case D3DDECLTYPE_UNUSED: return L"D3DDECLTYPE_UNUSED";
        default: return L"D3DDECLTYPE Unknown";
    }
}

WCHAR* DXUTTraceD3DDECLMETHODtoString( BYTE m )
{
    switch( m )
    {
        case D3DDECLMETHOD_DEFAULT: return L"D3DDECLMETHOD_DEFAULT";
        case D3DDECLMETHOD_PARTIALU: return L"D3DDECLMETHOD_PARTIALU";
        case D3DDECLMETHOD_PARTIALV: return L"D3DDECLMETHOD_PARTIALV";
        case D3DDECLMETHOD_CROSSUV: return L"D3DDECLMETHOD_CROSSUV";
        case D3DDECLMETHOD_UV: return L"D3DDECLMETHOD_UV";
        case D3DDECLMETHOD_LOOKUP: return L"D3DDECLMETHOD_LOOKUP";
        case D3DDECLMETHOD_LOOKUPPRESAMPLED: return L"D3DDECLMETHOD_LOOKUPPRESAMPLED";
        default: return L"D3DDECLMETHOD Unknown";
    }
}

WCHAR* DXUTTraceD3DDECLUSAGEtoString( BYTE u )
{
    switch( u )
    {
        case D3DDECLUSAGE_POSITION: return L"D3DDECLUSAGE_POSITION";
        case D3DDECLUSAGE_BLENDWEIGHT: return L"D3DDECLUSAGE_BLENDWEIGHT";
        case D3DDECLUSAGE_BLENDINDICES: return L"D3DDECLUSAGE_BLENDINDICES";
        case D3DDECLUSAGE_NORMAL: return L"D3DDECLUSAGE_NORMAL";
        case D3DDECLUSAGE_PSIZE: return L"D3DDECLUSAGE_PSIZE";
        case D3DDECLUSAGE_TEXCOORD: return L"D3DDECLUSAGE_TEXCOORD";
        case D3DDECLUSAGE_TANGENT: return L"D3DDECLUSAGE_TANGENT";
        case D3DDECLUSAGE_BINORMAL: return L"D3DDECLUSAGE_BINORMAL";
        case D3DDECLUSAGE_TESSFACTOR: return L"D3DDECLUSAGE_TESSFACTOR";
        case D3DDECLUSAGE_POSITIONT: return L"D3DDECLUSAGE_POSITIONT";
        case D3DDECLUSAGE_COLOR: return L"D3DDECLUSAGE_COLOR";
        case D3DDECLUSAGE_FOG: return L"D3DDECLUSAGE_FOG";
        case D3DDECLUSAGE_DEPTH: return L"D3DDECLUSAGE_DEPTH";
        case D3DDECLUSAGE_SAMPLE: return L"D3DDECLUSAGE_SAMPLE";
        default: return L"D3DDECLUSAGE Unknown";
    }
}


//--------------------------------------------------------------------------------------
// Multimon API handling for OSes with or without multimon API support
//--------------------------------------------------------------------------------------
#define DXUT_PRIMARY_MONITOR ((HMONITOR)0x12340042)
typedef HMONITOR (WINAPI* LPMONITORFROMWINDOW)(HWND, DWORD);
typedef BOOL     (WINAPI* LPGETMONITORINFO)(HMONITOR, LPMONITORINFO);

BOOL DXUTGetMonitorInfo(HMONITOR hMonitor, LPMONITORINFO lpMonitorInfo)
{
    static bool s_bInited = false;
    static LPGETMONITORINFO s_pFnGetMonitorInfo = NULL;
    if( !s_bInited )        
    {
        s_bInited = true;
        HMODULE hUser32 = GetModuleHandle( L"USER32" );
        if (hUser32 ) 
        {
            OSVERSIONINFOA osvi = {0}; osvi.dwOSVersionInfoSize = sizeof(osvi); GetVersionExA((OSVERSIONINFOA*)&osvi);
            bool bNT = (VER_PLATFORM_WIN32_NT == osvi.dwPlatformId);    
            s_pFnGetMonitorInfo = (LPGETMONITORINFO) (bNT ? GetProcAddress(hUser32,"GetMonitorInfoW") : GetProcAddress(hUser32,"GetMonitorInfoA"));
        }
    }

    if( s_pFnGetMonitorInfo ) 
        return s_pFnGetMonitorInfo(hMonitor, lpMonitorInfo);

    RECT rcWork;
    if ((hMonitor == DXUT_PRIMARY_MONITOR) && lpMonitorInfo && (lpMonitorInfo->cbSize >= sizeof(MONITORINFO)) && SystemParametersInfoA(SPI_GETWORKAREA, 0, &rcWork, 0))
    {
        lpMonitorInfo->rcMonitor.left = 0;
        lpMonitorInfo->rcMonitor.top  = 0;
        lpMonitorInfo->rcMonitor.right  = GetSystemMetrics(SM_CXSCREEN);
        lpMonitorInfo->rcMonitor.bottom = GetSystemMetrics(SM_CYSCREEN);
        lpMonitorInfo->rcWork = rcWork;
        lpMonitorInfo->dwFlags = MONITORINFOF_PRIMARY;
        return TRUE;
    }
    return FALSE;
}


HMONITOR DXUTMonitorFromWindow(HWND hWnd, DWORD dwFlags)
{
    static bool s_bInited = false;
    static LPMONITORFROMWINDOW s_pFnGetMonitorFronWindow = NULL;
    if( !s_bInited )        
    {
        s_bInited = true;
        HMODULE hUser32 = GetModuleHandle( L"USER32" );
        if (hUser32 ) s_pFnGetMonitorFronWindow = (LPMONITORFROMWINDOW) GetProcAddress(hUser32,"MonitorFromWindow");
    }

    if( s_pFnGetMonitorFronWindow ) 
        return s_pFnGetMonitorFronWindow(hWnd, dwFlags);
    if (dwFlags & (MONITOR_DEFAULTTOPRIMARY | MONITOR_DEFAULTTONEAREST))
        return DXUT_PRIMARY_MONITOR;
    return NULL;
}


//--------------------------------------------------------------------------------------
// Get the desktop resolution of an adapter. This isn't the same as the current resolution 
// from GetAdapterDisplayMode since the device might be fullscreen 
//--------------------------------------------------------------------------------------
void DXUTGetDesktopResolution( UINT AdapterOrdinal, UINT* pWidth, UINT* pHeight )
{
    CD3DEnumeration* pd3dEnum = DXUTGetEnumeration();
    CD3DEnumAdapterInfo* pAdapterInfo = pd3dEnum->GetAdapterInfo( AdapterOrdinal );                       
    DEVMODE devMode;
    ZeroMemory( &devMode, sizeof(DEVMODE) );
    devMode.dmSize = sizeof(DEVMODE);
    WCHAR strDeviceName[256];
    MultiByteToWideChar( CP_ACP, 0, pAdapterInfo->AdapterIdentifier.DeviceName, -1, strDeviceName, 256 );
    strDeviceName[255] = 0;
    EnumDisplaySettings( strDeviceName, ENUM_REGISTRY_SETTINGS, &devMode );
    
    if( pWidth )
        *pWidth = devMode.dmPelsWidth;
    if( pHeight )
        *pHeight = devMode.dmPelsHeight;
}


//--------------------------------------------------------------------------------------
IDirect3DDevice9* DXUTCreateRefDevice( HWND hWnd, bool bNullRef )
{
    HRESULT hr;
    IDirect3D9* pD3D = DXUT_Dynamic_Direct3DCreate9( D3D_SDK_VERSION );
    if( NULL == pD3D )
        return NULL;

    D3DDISPLAYMODE Mode;
    pD3D->GetAdapterDisplayMode(0, &Mode);

    D3DPRESENT_PARAMETERS pp;
    ZeroMemory( &pp, sizeof(D3DPRESENT_PARAMETERS) );
    pp.BackBufferWidth  = 1;
    pp.BackBufferHeight = 1;
    pp.BackBufferFormat = Mode.Format;
    pp.BackBufferCount  = 1;
    pp.SwapEffect       = D3DSWAPEFFECT_COPY;
    pp.Windowed         = TRUE;
    pp.hDeviceWindow    = hWnd;

    IDirect3DDevice9* pd3dDevice = NULL;
    hr = pD3D->CreateDevice( D3DADAPTER_DEFAULT, bNullRef ? D3DDEVTYPE_NULLREF : D3DDEVTYPE_REF,
                             hWnd, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &pd3dDevice );

    SAFE_RELEASE( pD3D );
    return pd3dDevice;
}


typedef DWORD (WINAPI* LPXINPUTGETSTATE)(DWORD dwUserIndex, XINPUT_STATE* pState );
typedef DWORD (WINAPI* LPXINPUTSETSTATE)(DWORD dwUserIndex, XINPUT_VIBRATION* pVibration );
typedef DWORD (WINAPI* LPXINPUTGETCAPABILITIES)( DWORD dwUserIndex, DWORD dwFlags, XINPUT_CAPABILITIES* pCapabilities );
typedef void  (WINAPI* LPXINPUTENABLE)(BOOL bEnable);



//--------------------------------------------------------------------------------------
// Does extra processing on XInput data to make it slightly more convenient to use
//--------------------------------------------------------------------------------------
HRESULT DXUTGetGamepadState( DWORD dwPort, DXUT_GAMEPAD* pGamePad, bool bThumbstickDeadZone, bool bSnapThumbstickToCardinals )
{
    if( dwPort >= DXUT_MAX_CONTROLLERS || pGamePad == NULL )
        return E_FAIL;

    static LPXINPUTGETSTATE s_pXInputGetState = NULL;
    static LPXINPUTGETCAPABILITIES s_pXInputGetCapabilities = NULL;
    if( NULL == s_pXInputGetState || NULL == s_pXInputGetCapabilities )
    {
        HINSTANCE hInst = LoadLibrary( XINPUT_DLL );
        if( hInst ) 
        {
            s_pXInputGetState = (LPXINPUTGETSTATE)GetProcAddress( hInst, "XInputGetState" );
            s_pXInputGetCapabilities = (LPXINPUTGETCAPABILITIES)GetProcAddress( hInst, "XInputGetCapabilities" );
        }
    }
    if( s_pXInputGetState == NULL )
        return E_FAIL;

    XINPUT_STATE InputState;
    DWORD dwResult = s_pXInputGetState( dwPort, &InputState );

    // Track insertion and removals
    BOOL bWasConnected = pGamePad->bConnected;
    pGamePad->bConnected = (dwResult == ERROR_SUCCESS);
    pGamePad->bRemoved  = (  bWasConnected && !pGamePad->bConnected );
    pGamePad->bInserted = ( !bWasConnected &&  pGamePad->bConnected );

    // Don't update rest of the state if not connected
    if( !pGamePad->bConnected )
        return S_OK;

    // Store the capabilities of the device
    if( pGamePad->bInserted )
    {
        ZeroMemory( pGamePad, sizeof(DXUT_GAMEPAD) );
        pGamePad->bConnected = true;
        pGamePad->bInserted  = true;
        if( s_pXInputGetCapabilities )
            s_pXInputGetCapabilities( dwPort, XINPUT_DEVTYPE_GAMEPAD, &pGamePad->caps );
    }

    // Copy gamepad to local structure (assumes that XINPUT_GAMEPAD at the front in CONTROLER_STATE)
    memcpy( pGamePad, &InputState.Gamepad, sizeof(XINPUT_GAMEPAD) );

    if( bSnapThumbstickToCardinals )
    {
        // Apply deadzone to each axis independantly to slightly snap to up/down/left/right
        if( pGamePad->sThumbLX < XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE && pGamePad->sThumbLX > -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE )
            pGamePad->sThumbLX = 0;
        if( pGamePad->sThumbLY < XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE && pGamePad->sThumbLY > -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE ) 
            pGamePad->sThumbLY = 0;
        if( pGamePad->sThumbRX < XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE && pGamePad->sThumbRX > -XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE )
            pGamePad->sThumbRX = 0;
        if( pGamePad->sThumbRY < XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE && pGamePad->sThumbRY > -XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE ) 
            pGamePad->sThumbRY = 0;
    }
    else if( bThumbstickDeadZone )
    {
        // Apply deadzone if centered
        if( (pGamePad->sThumbLX < XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE && pGamePad->sThumbLX > -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE) && 
            (pGamePad->sThumbLY < XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE && pGamePad->sThumbLY > -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE) ) 
        {   
            pGamePad->sThumbLX = 0;
            pGamePad->sThumbLY = 0;
        }
        if( (pGamePad->sThumbRX < XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE && pGamePad->sThumbRX > -XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE) && 
            (pGamePad->sThumbRY < XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE && pGamePad->sThumbRY > -XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE) ) 
        {
            pGamePad->sThumbRX = 0;
            pGamePad->sThumbRY = 0;
        }
    }

    // Convert [-1,+1] range
    pGamePad->fThumbLX = pGamePad->sThumbLX / 32767.0f;
    pGamePad->fThumbLY = pGamePad->sThumbLY / 32767.0f;
    pGamePad->fThumbRX = pGamePad->sThumbRX / 32767.0f;
    pGamePad->fThumbRY = pGamePad->sThumbRY / 32767.0f;

    // Get the boolean buttons that have been pressed since the last call. 
    // Each button is represented by one bit.
    pGamePad->wPressedButtons = ( pGamePad->wLastButtons ^ pGamePad->wButtons ) & pGamePad->wButtons;
    pGamePad->wLastButtons    = pGamePad->wButtons;

    // Figure out if the left trigger has been pressed or released
    bool bPressed = ( pGamePad->bLeftTrigger > DXUT_GAMEPAD_TRIGGER_THRESHOLD );
    pGamePad->bPressedLeftTrigger = ( bPressed ) ? !pGamePad->bLastLeftTrigger : false;
    pGamePad->bLastLeftTrigger = bPressed;

    // Figure out if the right trigger has been pressed or released
    bPressed = ( pGamePad->bRightTrigger > DXUT_GAMEPAD_TRIGGER_THRESHOLD );
    pGamePad->bPressedRightTrigger = ( bPressed ) ? !pGamePad->bLastRightTrigger : false;
    pGamePad->bLastRightTrigger = bPressed;

    return S_OK;
}


//--------------------------------------------------------------------------------------
// Don't pause the game or deactive the window without first stopping rumble otherwise 
// the controller will continue to rumble
//--------------------------------------------------------------------------------------
void DXUTEnableXInput( bool bEnable )
{
    static LPXINPUTENABLE s_pXInputEnable = NULL;
    if( NULL == s_pXInputEnable )
    {
        HINSTANCE hInst = LoadLibrary( XINPUT_DLL );
        if( hInst ) 
            s_pXInputEnable = (LPXINPUTENABLE)GetProcAddress( hInst, "XInputEnable" );
    }

    if( s_pXInputEnable )
        s_pXInputEnable( bEnable );
}


//--------------------------------------------------------------------------------------
// Don't pause the game or deactive the window without first stopping rumble otherwise 
// the controller will continue to rumble
//--------------------------------------------------------------------------------------
HRESULT DXUTStopRumbleOnAllControllers()
{
    static LPXINPUTSETSTATE s_pXInputSetState = NULL;
    if( NULL == s_pXInputSetState )
    {
        HINSTANCE hInst = LoadLibrary( XINPUT_DLL );
        if( hInst ) 
            s_pXInputSetState = (LPXINPUTSETSTATE)GetProcAddress( hInst, "XInputSetState" );
    }
    if( s_pXInputSetState == NULL )
        return E_FAIL;

    XINPUT_VIBRATION vibration;
    vibration.wLeftMotorSpeed  = 0;
    vibration.wRightMotorSpeed = 0;
    for( int iUserIndex=0; iUserIndex<DXUT_MAX_CONTROLLERS; iUserIndex++ )
        s_pXInputSetState( iUserIndex, &vibration );

    return S_OK;
}


//--------------------------------------------------------------------------------------
// Helper function to launch the Media Center UI after the program terminates
//--------------------------------------------------------------------------------------
bool DXUTReLaunchMediaCenter()
{
    // Skip if not running on a Media Center
    if( GetSystemMetrics( 87 ) == 0 ) //  SM_MEDIACENTER == 87, but is only defined if _WIN32_WINNT >= 0x0501
        return false;
 
    // Get the path to Media Center
    WCHAR szExpandedPath[MAX_PATH];
    if( !ExpandEnvironmentStrings( L"%SystemRoot%\\ehome\\ehshell.exe", szExpandedPath, MAX_PATH) )
        return false;

    // Skip if ehshell.exe doesn't exist
    if( GetFileAttributes( szExpandedPath ) == 0xFFFFFFFF )
        return false;
 
    // Launch ehshell.exe 
    INT_PTR result = (INT_PTR)ShellExecute( NULL, TEXT("open"), szExpandedPath, NULL, NULL, SW_SHOWNORMAL);
    return (result > 32);
}
