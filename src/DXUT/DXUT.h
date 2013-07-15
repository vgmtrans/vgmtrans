//--------------------------------------------------------------------------------------
// File: DXUT.h
//
// Copyright (c) Microsoft Corporation. All rights reserved.
//--------------------------------------------------------------------------------------
#pragma once
#ifndef DXUT_H
#define DXUT_H

#ifndef UNICODE
#error "DXUT requires a Unicode build. See the nearby comments for details"
//
// If you are using Microsoft Visual C++ .NET, under the General tab of the project 
// properties change the Character Set to 'Use Unicode Character Set'.  
//
// Windows XP and later are native Unicode so Unicode applications will perform better.  
// For Windows 98 and Windows Me support, consider using the Microsoft Layer for Unicode (MSLU).  
//
// To use MSLU, link against a set of libraries similar to this
//      /nod:kernel32.lib /nod:advapi32.lib /nod:user32.lib /nod:gdi32.lib /nod:shell32.lib /nod:comdlg32.lib /nod:version.lib /nod:mpr.lib /nod:rasapi32.lib /nod:winmm.lib /nod:winspool.lib /nod:vfw32.lib /nod:secur32.lib /nod:oleacc.lib /nod:oledlg.lib /nod:sensapi.lib UnicoWS.lib kernel32.lib advapi32.lib user32.lib gdi32.lib shell32.lib comdlg32.lib version.lib mpr.lib rasapi32.lib winmm.lib winspool.lib vfw32.lib secur32.lib oleacc.lib oledlg.lib sensapi.lib dxerr.lib dxguid.lib d3dx9d.lib d3d9.lib comctl32.lib
// and put the unicows.dll (available for download from msdn.microsoft.com) in the exe's folder.
// 
// For more details see the MSDN article titled:
// "MSLU: Develop Unicode Applications for Windows 9x Platforms with the Microsoft Layer for Unicode"
// at http://msdn.microsoft.com/msdnmag/issues/01/10/MSLU/default.aspx 
//
#endif

//--------------------------------------------------------------------------------------
// Structs
//--------------------------------------------------------------------------------------
class CD3DEnumeration;

struct DXUTDeviceSettings
{
    UINT AdapterOrdinal;
    D3DDEVTYPE DeviceType;
    D3DFORMAT AdapterFormat;
    DWORD BehaviorFlags;
    D3DPRESENT_PARAMETERS pp;
};


//--------------------------------------------------------------------------------------
// Error codes
//--------------------------------------------------------------------------------------
#define DXUTERR_NODIRECT3D              MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x0901)
#define DXUTERR_NOCOMPATIBLEDEVICES     MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x0902)
#define DXUTERR_MEDIANOTFOUND           MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x0903)
#define DXUTERR_NONZEROREFCOUNT         MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x0904)
#define DXUTERR_CREATINGDEVICE          MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x0905)
#define DXUTERR_RESETTINGDEVICE         MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x0906)
#define DXUTERR_CREATINGDEVICEOBJECTS   MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x0907)
#define DXUTERR_RESETTINGDEVICEOBJECTS  MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x0908)
#define DXUTERR_INCORRECTVERSION        MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x0909)


//--------------------------------------------------------------------------------------
// Callback registration 
//--------------------------------------------------------------------------------------
typedef bool    (CALLBACK *LPDXUTCALLBACKISDEVICEACCEPTABLE)( D3DCAPS9* pCaps, D3DFORMAT AdapterFormat, D3DFORMAT BackBufferFormat, bool bWindowed, void* pUserContext );
typedef bool    (CALLBACK *LPDXUTCALLBACKMODIFYDEVICESETTINGS)( DXUTDeviceSettings* pDeviceSettings, const D3DCAPS9* pCaps, void* pUserContext );
typedef HRESULT (CALLBACK *LPDXUTCALLBACKDEVICECREATED)( IDirect3DDevice9* pd3dDevice, const D3DSURFACE_DESC* pBackBufferSurfaceDesc, void* pUserContext );
typedef HRESULT (CALLBACK *LPDXUTCALLBACKDEVICERESET)( IDirect3DDevice9* pd3dDevice, const D3DSURFACE_DESC* pBackBufferSurfaceDesc, void* pUserContext );
typedef void    (CALLBACK *LPDXUTCALLBACKDEVICEDESTROYED)( void* pUserContext );
typedef void    (CALLBACK *LPDXUTCALLBACKDEVICELOST)( void* pUserContext );
typedef void    (CALLBACK *LPDXUTCALLBACKFRAMEMOVE)( IDirect3DDevice9* pd3dDevice, double fTime, float fElapsedTime, void* pUserContext );
typedef void    (CALLBACK *LPDXUTCALLBACKFRAMERENDER)( IDirect3DDevice9* pd3dDevice, double fTime, float fElapsedTime, void* pUserContext );
typedef void    (CALLBACK *LPDXUTCALLBACKKEYBOARD)( UINT nChar, bool bKeyDown, bool bAltDown, void* pUserContext );
typedef void    (CALLBACK *LPDXUTCALLBACKMOUSE)( bool bLeftButtonDown, bool bRightButtonDown, bool bMiddleButtonDown, bool bSideButton1Down, bool bSideButton2Down, int nMouseWheelDelta, int xPos, int yPos, void* pUserContext );
typedef LRESULT (CALLBACK *LPDXUTCALLBACKMSGPROC)( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, bool* pbNoFurtherProcessing, void* pUserContext );
typedef void    (CALLBACK *LPDXUTCALLBACKTIMER)( UINT idEvent, void* pUserContext );

// Device callbacks
void DXUTSetCallbackDeviceCreated( LPDXUTCALLBACKDEVICECREATED pCallbackDeviceCreated, void* pUserContext = NULL );
void DXUTSetCallbackDeviceReset( LPDXUTCALLBACKDEVICERESET pCallbackDeviceReset, void* pUserContext = NULL );
void DXUTSetCallbackDeviceLost( LPDXUTCALLBACKDEVICELOST pCallbackDeviceLost, void* pUserContext = NULL );
void DXUTSetCallbackDeviceDestroyed( LPDXUTCALLBACKDEVICEDESTROYED pCallbackDeviceDestroyed, void* pUserContext = NULL );
void DXUTSetCallbackDeviceChanging( LPDXUTCALLBACKMODIFYDEVICESETTINGS pCallbackModifyDeviceSettings, void* pUserContext = NULL );

// Frame callbacks
void DXUTSetCallbackFrameMove( LPDXUTCALLBACKFRAMEMOVE pCallbackFrameMove, void* pUserContext = NULL );
void DXUTSetCallbackFrameRender( LPDXUTCALLBACKFRAMERENDER pCallbackFrameRender, void* pUserContext = NULL );

// Message callbacks
void DXUTSetCallbackKeyboard( LPDXUTCALLBACKKEYBOARD pCallbackKeyboard, void* pUserContext = NULL );
void DXUTSetCallbackMouse( LPDXUTCALLBACKMOUSE pCallbackMouse, bool bIncludeMouseMove = false, void* pUserContext = NULL );
void DXUTSetCallbackMsgProc( LPDXUTCALLBACKMSGPROC pCallbackMsgProc, void* pUserContext = NULL );



void DXUTHandleTimers();
void	DXUTSetTime(double t);
void	DXUTSetAbsoluteTime(double t);
void	DXUTSetElapsedTime(float t);

//--------------------------------------------------------------------------------------
// Initialization
//--------------------------------------------------------------------------------------
HRESULT DXUTInit( bool bParseCommandLine = true, bool bHandleDefaultHotkeys = true, bool bShowMsgBoxOnError = true, bool bHandleAltEnter = true );

// Choose either DXUTCreateWindow or DXUTSetWindow.  If using DXUTSetWindow, consider using DXUTStaticWndProc
HRESULT DXUTCreateWindow( const WCHAR* strWindowTitle = L"Direct3D Window", 
                          HINSTANCE hInstance = NULL, HICON hIcon = NULL, HMENU hMenu = NULL,
                          int x = CW_USEDEFAULT, int y = CW_USEDEFAULT );
HRESULT DXUTSetWindow( HWND hWndFocus, HWND hWndDeviceFullScreen, HWND hWndDeviceWindowed, bool bHandleMessages = true );
LRESULT CALLBACK DXUTStaticWndProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam );

// Choose either DXUTCreateDevice or DXUTSetDevice or DXUTCreateDeviceFromSettings
HRESULT DXUTCreateDevice( UINT AdapterOrdinal = D3DADAPTER_DEFAULT, bool bWindowed = true, 
                          int nSuggestedWidth = 0, int nSuggestedHeight = 0,
                          LPDXUTCALLBACKISDEVICEACCEPTABLE pCallbackIsDeviceAcceptable = NULL,
                          LPDXUTCALLBACKMODIFYDEVICESETTINGS pCallbackModifyDeviceSettings = NULL, 
                          void* pUserContext = NULL );
HRESULT DXUTCreateDeviceFromSettings( DXUTDeviceSettings* pDeviceSettings, bool bPreserveInput = false, bool bClipWindowToSingleAdapter = true );
HRESULT DXUTSetDevice( IDirect3DDevice9* pd3dDevice );

// Choose either DXUTMainLoop or implement your own main loop 
HRESULT DXUTMainLoop( HACCEL hAccel = NULL );

// If not using DXUTMainLoop consider using DXUTRender3DEnvironment
void DXUTRender3DEnvironment(); 


//--------------------------------------------------------------------------------------
// Finding valid device settings
//--------------------------------------------------------------------------------------
enum DXUT_MATCH_TYPE
{
    DXUTMT_IGNORE_INPUT = 0,  // Use the closest valid value to a default 
    DXUTMT_PRESERVE_INPUT,    // Use input without change, but may cause no valid device to be found
    DXUTMT_CLOSEST_TO_INPUT   // Use the closest valid value to the input 
};

struct DXUTMatchOptions
{
    DXUT_MATCH_TYPE eAdapterOrdinal;
    DXUT_MATCH_TYPE eDeviceType;
    DXUT_MATCH_TYPE eWindowed;
    DXUT_MATCH_TYPE eAdapterFormat;
    DXUT_MATCH_TYPE eVertexProcessing;
    DXUT_MATCH_TYPE eResolution;
    DXUT_MATCH_TYPE eBackBufferFormat;
    DXUT_MATCH_TYPE eBackBufferCount;
    DXUT_MATCH_TYPE eMultiSample;
    DXUT_MATCH_TYPE eSwapEffect;
    DXUT_MATCH_TYPE eDepthFormat;
    DXUT_MATCH_TYPE eStencilFormat;
    DXUT_MATCH_TYPE ePresentFlags;
    DXUT_MATCH_TYPE eRefreshRate;
    DXUT_MATCH_TYPE ePresentInterval;
};

HRESULT DXUTFindValidDeviceSettings( DXUTDeviceSettings* pOut, DXUTDeviceSettings* pIn = NULL, DXUTMatchOptions* pMatchOptions = NULL );


//--------------------------------------------------------------------------------------
// Common Tasks 
//--------------------------------------------------------------------------------------
void    DXUTSetCursorSettings( bool bShowCursorWhenFullScreen, bool bClipCursorWhenFullScreen );
void    DXUTSetMultimonSettings( bool bAutoChangeAdapter );
void    DXUTSetShortcutKeySettings( bool bAllowWhenFullscreen = false, bool bAllowWhenWindowed = true ); // Controls the Windows key, and accessibility shortcut keys
void    DXUTSetWindowSettings( bool bCallDefWindowProc = true );
void    DXUTSetConstantFrameTime( bool bConstantFrameTime, float fTimePerFrame = 0.0333f );
HRESULT DXUTSetTimer( LPDXUTCALLBACKTIMER pCallbackTimer, float fTimeoutInSecs = 1.0f, UINT* pnIDEvent = NULL, void* pCallbackUserContext = NULL );
HRESULT DXUTKillTimer( UINT nIDEvent );
HRESULT DXUTToggleFullScreen();
HRESULT DXUTToggleREF();
void    DXUTPause( bool bPauseTime, bool bPauseRendering );
void    DXUTResetFrameworkState();
void    DXUTShutdown( int nExitCode = 0 );


//--------------------------------------------------------------------------------------
// State Retrieval  
//--------------------------------------------------------------------------------------
IDirect3D9*             DXUTGetD3DObject(); // Does not addref unlike typical Get* APIs
IDirect3DDevice9*       DXUTGetD3DDevice(); // Does not addref unlike typical Get* APIs
DXUTDeviceSettings      DXUTGetDeviceSettings(); 
D3DPRESENT_PARAMETERS   DXUTGetPresentParameters();
const D3DSURFACE_DESC*  DXUTGetBackBufferSurfaceDesc();
const D3DCAPS9*         DXUTGetDeviceCaps();
HINSTANCE               DXUTGetHINSTANCE();
HWND                    DXUTGetHWND();
HWND                    DXUTGetHWNDFocus();
HWND                    DXUTGetHWNDDeviceFullScreen();
HWND                    DXUTGetHWNDDeviceWindowed();
RECT                    DXUTGetWindowClientRect();
RECT                    DXUTGetWindowClientRectAtModeChange(); // Useful for returning to windowed mode with the same resolution as before toggle to full screen mode
RECT                    DXUTGetFullsceenClientRectAtModeChange(); // Useful for returning to full screen mode with the same resolution as before toggle to windowed mode
double                  DXUTGetTime();
float                   DXUTGetElapsedTime();
bool                    DXUTIsWindowed();
float                   DXUTGetFPS();
LPCWSTR                 DXUTGetWindowTitle();
LPCWSTR                 DXUTGetFrameStats( bool bIncludeFPS = false );
LPCWSTR                 DXUTGetDeviceStats();
bool                    DXUTIsRenderingPaused();
bool                    DXUTIsTimePaused();
bool                    DXUTIsActive();
int                     DXUTGetExitCode();
bool                    DXUTGetShowMsgBoxOnError();
bool                    DXUTGetHandleDefaultHotkeys();
bool                    DXUTIsKeyDown( BYTE vKey ); // Pass a virtual-key code, ex. VK_F1, 'A', VK_RETURN, VK_LSHIFT, etc
bool                    DXUTIsMouseButtonDown( BYTE vButton ); // Pass a virtual-key code: VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2
bool                    DXUTGetAutomation();  // Returns true if -automation parameter is used to launch the app

#endif




