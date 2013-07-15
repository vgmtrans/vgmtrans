//--------------------------------------------------------------------------------------
// File: DXUTMisc.h
//
// Helper functions for Direct3D programming.
//
// Copyright (c) Microsoft Corporation. All rights reserved
//--------------------------------------------------------------------------------------
#pragma once
#ifndef DXUT_MISC_H
#define DXUT_MISC_H

#ifndef INT_MAX
#define INT_MAX       2147483647    /* maximum (signed) int value */
#endif
#ifndef UINT_MAX      
#define UINT_MAX      0xffffffff    /* maximum unsigned int value */
#endif


//--------------------------------------------------------------------------------------
// XInput helper state/function
// This performs extra processing on XInput gamepad data to make it slightly more convenient to use
// 
// Example usage:
//
//      DXUT_GAMEPAD gamepad[4];
//      for( DWORD iPort=0; iPort<DXUT_MAX_CONTROLLERS; iPort++ )
//          DXUTGetGamepadState( iPort, gamepad[iPort] );
//
//--------------------------------------------------------------------------------------
#define DXUT_MAX_CONTROLLERS 4  // XInput handles up to 4 controllers 

struct DXUT_GAMEPAD
{
    // From XINPUT_GAMEPAD
    WORD    wButtons;
    BYTE    bLeftTrigger;
    BYTE    bRightTrigger;
    SHORT   sThumbLX;
    SHORT   sThumbLY;
    SHORT   sThumbRX;
    SHORT   sThumbRY;

    // Device properties
    XINPUT_CAPABILITIES caps;
    bool    bConnected; // If the controller is currently connected
    bool    bInserted;  // If the controller was inserted this frame
    bool    bRemoved;   // If the controller was removed this frame

    // Thumb stick values converted to range [-1,+1]
    float   fThumbRX;
    float   fThumbRY;
    float   fThumbLX;
    float   fThumbLY;

    // Records which buttons were pressed this frame.
    // These are only set on the first frame that the button is pressed
    WORD    wPressedButtons;
    bool    bPressedLeftTrigger;
    bool    bPressedRightTrigger;

    // Last state of the buttons
    WORD    wLastButtons;
    bool    bLastLeftTrigger;
    bool    bLastRightTrigger;
};

HRESULT DXUTGetGamepadState( DWORD dwPort, DXUT_GAMEPAD* pGamePad, bool bThumbstickDeadZone = true, bool bSnapThumbstickToCardinals = true );
HRESULT DXUTStopRumbleOnAllControllers();
void    DXUTEnableXInput( bool bEnable );


//--------------------------------------------------------------------------------------
// A growable array
//--------------------------------------------------------------------------------------
template< typename TYPE >
class CGrowableArray
{
public:
    CGrowableArray()  { m_pData = NULL; m_nSize = 0; m_nMaxSize = 0; }
    CGrowableArray( const CGrowableArray<TYPE>& a ) { for( int i=0; i < a.m_nSize; i++ ) Add( a.m_pData[i] ); }
    ~CGrowableArray() { RemoveAll(); }

    const TYPE& operator[]( int nIndex ) const { return GetAt( nIndex ); }
    TYPE& operator[]( int nIndex ) { return GetAt( nIndex ); }
   
    CGrowableArray& operator=( const CGrowableArray<TYPE>& a ) { if( this == &a ) return *this; RemoveAll(); for( int i=0; i < a.m_nSize; i++ ) Add( a.m_pData[i] ); return *this; }

    HRESULT SetSize( int nNewMaxSize );
    HRESULT Add( const TYPE& value );
    HRESULT Insert( int nIndex, const TYPE& value );
    HRESULT SetAt( int nIndex, const TYPE& value );
    TYPE&   GetAt( int nIndex ) { assert( nIndex >= 0 && nIndex < m_nSize ); return m_pData[nIndex]; }
    int     GetSize() const { return m_nSize; }
    TYPE*   GetData() { return m_pData; }
    bool    Contains( const TYPE& value ){ return ( -1 != IndexOf( value ) ); }

    int     IndexOf( const TYPE& value ) { return ( m_nSize > 0 ) ? IndexOf( value, 0, m_nSize ) : -1; }
    int     IndexOf( const TYPE& value, int iStart ) { return IndexOf( value, iStart, m_nSize - iStart ); }
    int     IndexOf( const TYPE& value, int nIndex, int nNumElements );

    int     LastIndexOf( const TYPE& value ) { return ( m_nSize > 0 ) ? LastIndexOf( value, m_nSize-1, m_nSize ) : -1; }
    int     LastIndexOf( const TYPE& value, int nIndex ) { return LastIndexOf( value, nIndex, nIndex+1 ); }
    int     LastIndexOf( const TYPE& value, int nIndex, int nNumElements );

    HRESULT Remove( int nIndex );
    void    RemoveAll() { SetSize(0); }

protected:
    TYPE* m_pData;      // the actual array of data
    int m_nSize;        // # of elements (upperBound - 1)
    int m_nMaxSize;     // max allocated

    HRESULT SetSizeInternal( int nNewMaxSize );  // This version doesn't call ctor or dtor.
};


//--------------------------------------------------------------------------------------
// Performs timer operations
// Use DXUTGetGlobalTimer() to get the global instance
//--------------------------------------------------------------------------------------
class CDXUTTimer
{
public:
    CDXUTTimer();

    void Reset(); // resets the timer
    void Start(); // starts the timer
    void Stop();  // stop (or pause) the timer
    void Advance(); // advance the timer by 0.1 seconds
    double GetAbsoluteTime(); // get the absolute system time
    double GetTime(); // get the current time
    double GetElapsedTime(); // get the time that elapsed between Get*ElapsedTime() calls
    void GetTimeValues( double* pfTime, double* pfAbsoluteTime, float* pfElapsedTime ); // get all time values at once
    bool IsStopped(); // returns true if timer stopped

    // Limit the current thread to one processor (the current one). This ensures that timing code runs
    // on only one processor, and will not suffer any ill effects from power management.
    void LimitThreadAffinityToCurrentProc();

protected:
    LARGE_INTEGER GetAdjustedCurrentTime();

    bool m_bTimerStopped;
    LONGLONG m_llQPFTicksPerSec;
    
    LONGLONG m_llStopTime;
    LONGLONG m_llLastElapsedTime;
    LONGLONG m_llBaseTime;
};

CDXUTTimer* DXUTGetGlobalTimer();


//-----------------------------------------------------------------------------
// Resource cache for textures, fonts, meshs, and effects.  
// Use DXUTGetGlobalResourceCache() to access the global cache
//-----------------------------------------------------------------------------

enum DXUTCACHE_SOURCELOCATION { DXUTCACHE_LOCATION_FILE, DXUTCACHE_LOCATION_RESOURCE };

struct DXUTCache_Texture
{
    DXUTCACHE_SOURCELOCATION Location;
    WCHAR wszSource[MAX_PATH];
    HMODULE hSrcModule;
    UINT Width;
    UINT Height;
    UINT Depth;
    UINT MipLevels;
    DWORD Usage;
    D3DFORMAT Format;
    D3DPOOL Pool;
    D3DRESOURCETYPE Type;
    IDirect3DBaseTexture9 *pTexture;
};

struct DXUTCache_Font : public D3DXFONT_DESC
{
    ID3DXFont *pFont;
};

struct DXUTCache_Effect
{
    DXUTCACHE_SOURCELOCATION Location;
    WCHAR wszSource[MAX_PATH];
    HMODULE hSrcModule;
    DWORD dwFlags;
    ID3DXEffect *pEffect;
};


class CDXUTResourceCache
{
public:
    ~CDXUTResourceCache();

    HRESULT CreateTextureFromFile( LPDIRECT3DDEVICE9 pDevice, LPCTSTR pSrcFile, LPDIRECT3DTEXTURE9 *ppTexture );
    HRESULT CreateTextureFromFileEx( LPDIRECT3DDEVICE9 pDevice, LPCTSTR pSrcFile, UINT Width, UINT Height, UINT MipLevels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, DWORD Filter, DWORD MipFilter, D3DCOLOR ColorKey, D3DXIMAGE_INFO *pSrcInfo, PALETTEENTRY *pPalette, LPDIRECT3DTEXTURE9 *ppTexture );
    HRESULT CreateTextureFromResource( LPDIRECT3DDEVICE9 pDevice, HMODULE hSrcModule, LPCTSTR pSrcResource, LPDIRECT3DTEXTURE9 *ppTexture );
    HRESULT CreateTextureFromResourceEx( LPDIRECT3DDEVICE9 pDevice, HMODULE hSrcModule, LPCTSTR pSrcResource, UINT Width, UINT Height, UINT MipLevels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, DWORD Filter, DWORD MipFilter, D3DCOLOR ColorKey, D3DXIMAGE_INFO *pSrcInfo, PALETTEENTRY *pPalette, LPDIRECT3DTEXTURE9 *ppTexture );
    HRESULT CreateCubeTextureFromFile( LPDIRECT3DDEVICE9 pDevice, LPCTSTR pSrcFile, LPDIRECT3DCUBETEXTURE9 *ppCubeTexture );
    HRESULT CreateCubeTextureFromFileEx( LPDIRECT3DDEVICE9 pDevice, LPCTSTR pSrcFile, UINT Size, UINT MipLevels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, DWORD Filter, DWORD MipFilter, D3DCOLOR ColorKey, D3DXIMAGE_INFO *pSrcInfo, PALETTEENTRY *pPalette, LPDIRECT3DCUBETEXTURE9 *ppCubeTexture );
    HRESULT CreateCubeTextureFromResource( LPDIRECT3DDEVICE9 pDevice, HMODULE hSrcModule, LPCTSTR pSrcResource, LPDIRECT3DCUBETEXTURE9 *ppCubeTexture );
    HRESULT CreateCubeTextureFromResourceEx( LPDIRECT3DDEVICE9 pDevice, HMODULE hSrcModule, LPCTSTR pSrcResource, UINT Size, UINT MipLevels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, DWORD Filter, DWORD MipFilter, D3DCOLOR ColorKey, D3DXIMAGE_INFO *pSrcInfo, PALETTEENTRY *pPalette, LPDIRECT3DCUBETEXTURE9 *ppCubeTexture );
    HRESULT CreateVolumeTextureFromFile( LPDIRECT3DDEVICE9 pDevice, LPCTSTR pSrcFile, LPDIRECT3DVOLUMETEXTURE9 *ppVolumeTexture );
    HRESULT CreateVolumeTextureFromFileEx( LPDIRECT3DDEVICE9 pDevice, LPCTSTR pSrcFile, UINT Width, UINT Height, UINT Depth, UINT MipLevels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, DWORD Filter, DWORD MipFilter, D3DCOLOR ColorKey, D3DXIMAGE_INFO *pSrcInfo, PALETTEENTRY *pPalette, LPDIRECT3DVOLUMETEXTURE9 *ppTexture );
    HRESULT CreateVolumeTextureFromResource( LPDIRECT3DDEVICE9 pDevice, HMODULE hSrcModule, LPCTSTR pSrcResource, LPDIRECT3DVOLUMETEXTURE9 *ppVolumeTexture );
    HRESULT CreateVolumeTextureFromResourceEx( LPDIRECT3DDEVICE9 pDevice, HMODULE hSrcModule, LPCTSTR pSrcResource, UINT Width, UINT Height, UINT Depth, UINT MipLevels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, DWORD Filter, DWORD MipFilter, D3DCOLOR ColorKey, D3DXIMAGE_INFO *pSrcInfo, PALETTEENTRY *pPalette, LPDIRECT3DVOLUMETEXTURE9 *ppVolumeTexture );
    HRESULT CreateFont( LPDIRECT3DDEVICE9 pDevice, UINT Height, UINT Width, UINT Weight, UINT MipLevels, BOOL Italic, DWORD CharSet, DWORD OutputPrecision, DWORD Quality, DWORD PitchAndFamily, LPCTSTR pFacename, LPD3DXFONT *ppFont );
    HRESULT CreateFontIndirect( LPDIRECT3DDEVICE9 pDevice, CONST D3DXFONT_DESC *pDesc, LPD3DXFONT *ppFont );
    HRESULT CreateEffectFromFile( LPDIRECT3DDEVICE9 pDevice, LPCTSTR pSrcFile, const D3DXMACRO *pDefines, LPD3DXINCLUDE pInclude, DWORD Flags, LPD3DXEFFECTPOOL pPool, LPD3DXEFFECT *ppEffect, LPD3DXBUFFER *ppCompilationErrors );
    HRESULT CreateEffectFromResource( LPDIRECT3DDEVICE9 pDevice, HMODULE hSrcModule, LPCTSTR pSrcResource, const D3DXMACRO *pDefines, LPD3DXINCLUDE pInclude, DWORD Flags, LPD3DXEFFECTPOOL pPool, LPD3DXEFFECT *ppEffect, LPD3DXBUFFER *ppCompilationErrors );

public:
    HRESULT OnCreateDevice( IDirect3DDevice9 *pd3dDevice );
    HRESULT OnResetDevice( IDirect3DDevice9 *pd3dDevice );
    HRESULT OnLostDevice();
    HRESULT OnDestroyDevice();

protected:
    friend CDXUTResourceCache& DXUTGetGlobalResourceCache();
    friend HRESULT DXUTInitialize3DEnvironment();
    friend HRESULT DXUTReset3DEnvironment();
    friend void DXUTCleanup3DEnvironment( bool bReleaseSettings );

    CDXUTResourceCache() { }

    CGrowableArray< DXUTCache_Texture > m_TextureCache;
    CGrowableArray< DXUTCache_Effect > m_EffectCache;
    CGrowableArray< DXUTCache_Font > m_FontCache;
};

CDXUTResourceCache& DXUTGetGlobalResourceCache();


//--------------------------------------------------------------------------------------
class CD3DArcBall
{
public:
    CD3DArcBall();

    // Functions to change behavior
    void Reset(); 
    void SetTranslationRadius( FLOAT fRadiusTranslation ) { m_fRadiusTranslation = fRadiusTranslation; }
    void SetWindow( INT nWidth, INT nHeight, FLOAT fRadius = 0.9f ) { m_nWidth = nWidth; m_nHeight = nHeight; m_fRadius = fRadius; m_vCenter = D3DXVECTOR2(m_nWidth/2.0f,m_nHeight/2.0f); }
    void SetOffset( INT nX, INT nY ) { m_Offset.x = nX; m_Offset.y = nY; }

    // Call these from client and use GetRotationMatrix() to read new rotation matrix
    void OnBegin( int nX, int nY );  // start the rotation (pass current mouse position)
    void OnMove( int nX, int nY );   // continue the rotation (pass current mouse position)
    void OnEnd();                    // end the rotation 

    // Or call this to automatically handle left, middle, right buttons
    LRESULT     HandleMessages( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam );

    // Functions to get/set state
    const D3DXMATRIX* GetRotationMatrix()                   { return D3DXMatrixRotationQuaternion(&m_mRotation, &m_qNow); };
    const D3DXMATRIX* GetTranslationMatrix() const          { return &m_mTranslation; }
    const D3DXMATRIX* GetTranslationDeltaMatrix() const     { return &m_mTranslationDelta; }
    bool        IsBeingDragged() const                      { return m_bDrag; }
    D3DXQUATERNION GetQuatNow() const                       { return m_qNow; }
    void        SetQuatNow( D3DXQUATERNION q ) { m_qNow = q; }

    static D3DXQUATERNION QuatFromBallPoints( const D3DXVECTOR3 &vFrom, const D3DXVECTOR3 &vTo );


protected:
    D3DXMATRIXA16  m_mRotation;         // Matrix for arc ball's orientation
    D3DXMATRIXA16  m_mTranslation;      // Matrix for arc ball's position
    D3DXMATRIXA16  m_mTranslationDelta; // Matrix for arc ball's position

    POINT          m_Offset;   // window offset, or upper-left corner of window
    INT            m_nWidth;   // arc ball's window width
    INT            m_nHeight;  // arc ball's window height
    D3DXVECTOR2    m_vCenter;  // center of arc ball 
    FLOAT          m_fRadius;  // arc ball's radius in screen coords
    FLOAT          m_fRadiusTranslation; // arc ball's radius for translating the target

    D3DXQUATERNION m_qDown;             // Quaternion before button down
    D3DXQUATERNION m_qNow;              // Composite quaternion for current drag
    bool           m_bDrag;             // Whether user is dragging arc ball

    POINT          m_ptLastMouse;      // position of last mouse point
    D3DXVECTOR3    m_vDownPt;           // starting point of rotation arc
    D3DXVECTOR3    m_vCurrentPt;        // current point of rotation arc

    D3DXVECTOR3    ScreenToVector( float fScreenPtX, float fScreenPtY );
};


//--------------------------------------------------------------------------------------
// used by CCamera to map WM_KEYDOWN keys
//--------------------------------------------------------------------------------------
enum D3DUtil_CameraKeys
{
    CAM_STRAFE_LEFT = 0,
    CAM_STRAFE_RIGHT,
    CAM_MOVE_FORWARD,
    CAM_MOVE_BACKWARD,
    CAM_MOVE_UP,
    CAM_MOVE_DOWN,
    CAM_RESET,
    CAM_CONTROLDOWN,
    CAM_MAX_KEYS,
    CAM_UNKNOWN = 0xFF
};

#define KEY_WAS_DOWN_MASK 0x80
#define KEY_IS_DOWN_MASK  0x01

#define MOUSE_LEFT_BUTTON   0x01
#define MOUSE_MIDDLE_BUTTON 0x02
#define MOUSE_RIGHT_BUTTON  0x04
#define MOUSE_WHEEL         0x08


//--------------------------------------------------------------------------------------
// Simple base camera class that moves and rotates.  The base class
//       records mouse and keyboard input for use by a derived class, and 
//       keeps common state.
//--------------------------------------------------------------------------------------
class CBaseCamera
{
public:
    CBaseCamera();

    // Call these from client and use Get*Matrix() to read new matrices
    virtual LRESULT HandleMessages( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam );
    virtual void    FrameMove( FLOAT fElapsedTime ) = 0;

    // Functions to change camera matrices
    virtual void Reset(); 
    virtual void SetViewParams( D3DXVECTOR3* pvEyePt, D3DXVECTOR3* pvLookatPt );
    virtual void SetProjParams( FLOAT fFOV, FLOAT fAspect, FLOAT fNearPlane, FLOAT fFarPlane );

    // Functions to change behavior
    virtual void SetDragRect( RECT &rc ) { m_rcDrag = rc; }
    void SetInvertPitch( bool bInvertPitch ) { m_bInvertPitch = bInvertPitch; }
    void SetDrag( bool bMovementDrag, FLOAT fTotalDragTimeToZero = 0.25f ) { m_bMovementDrag = bMovementDrag; m_fTotalDragTimeToZero = fTotalDragTimeToZero; }
    void SetEnableYAxisMovement( bool bEnableYAxisMovement ) { m_bEnableYAxisMovement = bEnableYAxisMovement; }
    void SetEnablePositionMovement( bool bEnablePositionMovement ) { m_bEnablePositionMovement = bEnablePositionMovement; }
    void SetClipToBoundary( bool bClipToBoundary, D3DXVECTOR3* pvMinBoundary, D3DXVECTOR3* pvMaxBoundary ) { m_bClipToBoundary = bClipToBoundary; if( pvMinBoundary ) m_vMinBoundary = *pvMinBoundary; if( pvMaxBoundary ) m_vMaxBoundary = *pvMaxBoundary; }
    void SetScalers( FLOAT fRotationScaler = 0.01f, FLOAT fMoveScaler = 5.0f )  { m_fRotationScaler = fRotationScaler; m_fMoveScaler = fMoveScaler; }
    void SetNumberOfFramesToSmoothMouseData( int nFrames ) { if( nFrames > 0 ) m_fFramesToSmoothMouseData = (float)nFrames; }

    // Functions to get state
    const D3DXMATRIX*  GetViewMatrix() const { return &m_mView; }
    const D3DXMATRIX*  GetProjMatrix() const { return &m_mProj; }
    const D3DXVECTOR3* GetEyePt() const      { return &m_vEye; }
    const D3DXVECTOR3* GetLookAtPt() const   { return &m_vLookAt; }
    float GetNearClip() const { return m_fNearPlane; }
    float GetFarClip() const { return m_fFarPlane; }

    bool IsBeingDragged() const         { return (m_bMouseLButtonDown || m_bMouseMButtonDown || m_bMouseRButtonDown); }
    bool IsMouseLButtonDown() const     { return m_bMouseLButtonDown; } 
    bool IsMouseMButtonDown() const     { return m_bMouseMButtonDown; } 
    bool IsMouseRButtonDown() const     { return m_bMouseRButtonDown; } 

protected:
    // Functions to map a WM_KEYDOWN key to a D3DUtil_CameraKeys enum
    virtual D3DUtil_CameraKeys MapKey( UINT nKey );    
    bool IsKeyDown( BYTE key ) const { return( (key & KEY_IS_DOWN_MASK) == KEY_IS_DOWN_MASK ); }
    bool WasKeyDown( BYTE key ) const { return( (key & KEY_WAS_DOWN_MASK) == KEY_WAS_DOWN_MASK ); }

    void ConstrainToBoundary( D3DXVECTOR3* pV );
    void UpdateVelocity( float fElapsedTime );
    void GetInput( bool bGetKeyboardInput, bool bGetMouseInput, bool bGetGamepadInput, bool bResetCursorAfterMove );

    D3DXMATRIX            m_mView;              // View matrix 
    D3DXMATRIX            m_mProj;              // Projection matrix

    DXUT_GAMEPAD          m_GamePad[DXUT_MAX_CONTROLLERS]; // XInput controller state
    D3DXVECTOR3           m_vGamePadLeftThumb;
    D3DXVECTOR3           m_vGamePadRightThumb;
    double                m_GamePadLastActive[DXUT_MAX_CONTROLLERS];

    int                   m_cKeysDown;            // Number of camera keys that are down.
    BYTE                  m_aKeys[CAM_MAX_KEYS];  // State of input - KEY_WAS_DOWN_MASK|KEY_IS_DOWN_MASK
    D3DXVECTOR3           m_vKeyboardDirection;   // Direction vector of keyboard input
    POINT                 m_ptLastMousePosition;  // Last absolute position of mouse cursor
    bool                  m_bMouseLButtonDown;    // True if left button is down 
    bool                  m_bMouseMButtonDown;    // True if middle button is down 
    bool                  m_bMouseRButtonDown;    // True if right button is down 
    int                   m_nCurrentButtonMask;   // mask of which buttons are down
    int                   m_nMouseWheelDelta;     // Amount of middle wheel scroll (+/-) 
    D3DXVECTOR2           m_vMouseDelta;          // Mouse relative delta smoothed over a few frames
    float                 m_fFramesToSmoothMouseData; // Number of frames to smooth mouse data over

    D3DXVECTOR3           m_vDefaultEye;          // Default camera eye position
    D3DXVECTOR3           m_vDefaultLookAt;       // Default LookAt position
    D3DXVECTOR3           m_vEye;                 // Camera eye position
    D3DXVECTOR3           m_vLookAt;              // LookAt position
    float                 m_fCameraYawAngle;      // Yaw angle of camera
    float                 m_fCameraPitchAngle;    // Pitch angle of camera

    RECT                  m_rcDrag;               // Rectangle within which a drag can be initiated.
    D3DXVECTOR3           m_vVelocity;            // Velocity of camera
    bool                  m_bMovementDrag;        // If true, then camera movement will slow to a stop otherwise movement is instant
    D3DXVECTOR3           m_vVelocityDrag;        // Velocity drag force
    FLOAT                 m_fDragTimer;           // Countdown timer to apply drag
    FLOAT                 m_fTotalDragTimeToZero; // Time it takes for velocity to go from full to 0
    D3DXVECTOR2           m_vRotVelocity;         // Velocity of camera

    float                 m_fFOV;                 // Field of view
    float                 m_fAspect;              // Aspect ratio
    float                 m_fNearPlane;           // Near plane
    float                 m_fFarPlane;            // Far plane

    float                 m_fRotationScaler;      // Scaler for rotation
    float                 m_fMoveScaler;          // Scaler for movement

    bool                  m_bInvertPitch;         // Invert the pitch axis
    bool                  m_bEnablePositionMovement; // If true, then the user can translate the camera/model 
    bool                  m_bEnableYAxisMovement; // If true, then camera can move in the y-axis

    bool                  m_bClipToBoundary;      // If true, then the camera will be clipped to the boundary
    D3DXVECTOR3           m_vMinBoundary;         // Min point in clip boundary
    D3DXVECTOR3           m_vMaxBoundary;         // Max point in clip boundary
};


//--------------------------------------------------------------------------------------
// Simple first person camera class that moves and rotates.
//       It allows yaw and pitch but not roll.  It uses WM_KEYDOWN and 
//       GetCursorPos() to respond to keyboard and mouse input and updates the 
//       view matrix based on input.  
//--------------------------------------------------------------------------------------
class CFirstPersonCamera : public CBaseCamera
{
public:
    CFirstPersonCamera();

    // Call these from client and use Get*Matrix() to read new matrices
    virtual void FrameMove( FLOAT fElapsedTime );

    // Functions to change behavior
    void SetRotateButtons( bool bLeft, bool bMiddle, bool bRight, bool bRotateWithoutButtonDown = false );

    void SetResetCursorAfterMove( bool bResetCursorAfterMove ) { m_bResetCursorAfterMove = bResetCursorAfterMove; }

    // Functions to get state
    D3DXMATRIX*  GetWorldMatrix()            { return &m_mCameraWorld; }

    const D3DXVECTOR3* GetWorldRight() const { return (D3DXVECTOR3*)&m_mCameraWorld._11; } 
    const D3DXVECTOR3* GetWorldUp() const    { return (D3DXVECTOR3*)&m_mCameraWorld._21; }
    const D3DXVECTOR3* GetWorldAhead() const { return (D3DXVECTOR3*)&m_mCameraWorld._31; }
    const D3DXVECTOR3* GetEyePt() const      { return (D3DXVECTOR3*)&m_mCameraWorld._41; }

protected:
    D3DXMATRIX m_mCameraWorld;       // World matrix of the camera (inverse of the view matrix)

    int        m_nActiveButtonMask;  // Mask to determine which button to enable for rotation
    bool       m_bRotateWithoutButtonDown;

    bool       m_bResetCursorAfterMove;// If true, the class will reset the cursor position so that the cursor always has space to move 
};


//--------------------------------------------------------------------------------------
// Simple model viewing camera class that rotates around the object.
//--------------------------------------------------------------------------------------
class CModelViewerCamera : public CBaseCamera
{
public:
    CModelViewerCamera();

    // Call these from client and use Get*Matrix() to read new matrices
    virtual LRESULT HandleMessages( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam );
    virtual void FrameMove( FLOAT fElapsedTime );

   
    // Functions to change behavior
    virtual void SetDragRect( RECT &rc );
    void Reset(); 
    void SetViewParams( D3DXVECTOR3* pvEyePt, D3DXVECTOR3* pvLookatPt );
    void SetButtonMasks( int nRotateModelButtonMask = MOUSE_LEFT_BUTTON, int nZoomButtonMask = MOUSE_WHEEL, int nRotateCameraButtonMask = MOUSE_RIGHT_BUTTON ) { m_nRotateModelButtonMask = nRotateModelButtonMask, m_nZoomButtonMask = nZoomButtonMask; m_nRotateCameraButtonMask = nRotateCameraButtonMask; }
    
    void SetAttachCameraToModel( bool bEnable = false ) { m_bAttachCameraToModel = bEnable; }
    void SetWindow( int nWidth, int nHeight, float fArcballRadius=0.9f ) { m_WorldArcBall.SetWindow( nWidth, nHeight, fArcballRadius ); m_ViewArcBall.SetWindow( nWidth, nHeight, fArcballRadius ); }
    void SetRadius( float fDefaultRadius=5.0f, float fMinRadius=1.0f, float fMaxRadius=FLT_MAX  ) { m_fDefaultRadius = m_fRadius = fDefaultRadius; m_fMinRadius = fMinRadius; m_fMaxRadius = fMaxRadius; m_bDragSinceLastUpdate = true; }
    void SetModelCenter( D3DXVECTOR3 vModelCenter ) { m_vModelCenter = vModelCenter; }
    void SetLimitPitch( bool bLimitPitch ) { m_bLimitPitch = bLimitPitch; }
    void SetViewQuat( D3DXQUATERNION q ) { m_ViewArcBall.SetQuatNow( q ); m_bDragSinceLastUpdate = true; }
    void SetWorldQuat( D3DXQUATERNION q ) { m_WorldArcBall.SetQuatNow( q ); m_bDragSinceLastUpdate = true; }

    // Functions to get state
    const D3DXMATRIX* GetWorldMatrix() const { return &m_mWorld; }
    void SetWorldMatrix( D3DXMATRIX &mWorld ) { m_mWorld = mWorld; m_bDragSinceLastUpdate = true; }

protected:
    CD3DArcBall  m_WorldArcBall;
    CD3DArcBall  m_ViewArcBall;
    D3DXVECTOR3  m_vModelCenter;
    D3DXMATRIX   m_mModelLastRot;        // Last arcball rotation matrix for model 
    D3DXMATRIX   m_mModelRot;            // Rotation matrix of model
    D3DXMATRIX   m_mWorld;               // World matrix of model

    int          m_nRotateModelButtonMask;
    int          m_nZoomButtonMask;
    int          m_nRotateCameraButtonMask;

    bool         m_bAttachCameraToModel;
    bool         m_bLimitPitch;
    float        m_fRadius;              // Distance from the camera to model 
    float        m_fDefaultRadius;       // Distance from the camera to model 
    float        m_fMinRadius;           // Min radius
    float        m_fMaxRadius;           // Max radius
    bool         m_bDragSinceLastUpdate; // True if mouse drag has happened since last time FrameMove is called.

    D3DXMATRIX   m_mCameraRotLast;

};


//--------------------------------------------------------------------------------------
// Manages the intertion point when drawing text
//--------------------------------------------------------------------------------------
class CDXUTTextHelper
{
public:
    CDXUTTextHelper( ID3DXFont* pFont, ID3DXSprite* pSprite, int nLineHeight );

    void SetInsertionPos( int x, int y ) { m_pt.x = x; m_pt.y = y; }
    void SetForegroundColor( D3DXCOLOR clr ) { m_clr = clr; }

    void Begin();
    HRESULT DrawFormattedTextLine( const WCHAR* strMsg, ... );
    HRESULT DrawTextLine( const WCHAR* strMsg );
    HRESULT DrawFormattedTextLine( RECT &rc, DWORD dwFlags, const WCHAR* strMsg, ... );
    HRESULT DrawTextLine( RECT &rc, DWORD dwFlags, const WCHAR* strMsg );
    void End();

protected:
    ID3DXFont*   m_pFont;
    ID3DXSprite* m_pSprite;
    D3DXCOLOR    m_clr;
    POINT        m_pt;
    int          m_nLineHeight;
};


//--------------------------------------------------------------------------------------
// Manages a persistent list of lines and draws them using ID3DXLine
//--------------------------------------------------------------------------------------
class CDXUTLineManager
{
public:
    CDXUTLineManager();
    ~CDXUTLineManager();

    HRESULT OnCreatedDevice( IDirect3DDevice9* pd3dDevice );
    HRESULT OnResetDevice();
    HRESULT OnRender();
    HRESULT OnLostDevice();
    HRESULT OnDeletedDevice();

    HRESULT AddLine( int* pnLineID, D3DXVECTOR2* pVertexList, DWORD dwVertexListCount, D3DCOLOR Color, float fWidth, float fScaleRatio, bool bAntiAlias );
    HRESULT AddRect( int* pnLineID, RECT rc, D3DCOLOR Color, float fWidth, float fScaleRatio, bool bAntiAlias );
    HRESULT RemoveLine( int nLineID );
    HRESULT RemoveAllLines();

protected:
    struct LINE_NODE
    {
        int      nLineID;
        D3DCOLOR Color;
        float    fWidth;
        bool     bAntiAlias;
        float    fScaleRatio;
        D3DXVECTOR2* pVertexList;
        DWORD    dwVertexListCount;
    };

    CGrowableArray<LINE_NODE*> m_LinesList;
    IDirect3DDevice9* m_pd3dDevice;
    ID3DXLine* m_pD3DXLine;
};


//--------------------------------------------------------------------------------------
// Manages the mesh, direction, mouse events of a directional arrow that 
// rotates around a radius controlled by an arcball 
//--------------------------------------------------------------------------------------
class CDXUTDirectionWidget
{
public:
    CDXUTDirectionWidget();

    static HRESULT StaticOnCreateDevice( IDirect3DDevice9* pd3dDevice );
    HRESULT OnResetDevice( const D3DSURFACE_DESC* pBackBufferSurfaceDesc );
    HRESULT OnRender( D3DXCOLOR color, const D3DXMATRIX* pmView, const D3DXMATRIX* pmProj, const D3DXVECTOR3* pEyePt );
    LRESULT HandleMessages( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam );
    static void StaticOnLostDevice();
    static void StaticOnDestroyDevice();

    D3DXVECTOR3 GetLightDirection()         { return m_vCurrentDir; };
    void        SetLightDirection( D3DXVECTOR3 vDir ) { m_vDefaultDir = m_vCurrentDir = vDir; };
    void        SetButtonMask( int nRotate = MOUSE_RIGHT_BUTTON ) { m_nRotateMask = nRotate; }

    float GetRadius()                 { return m_fRadius; };
    void  SetRadius( float fRadius )  { m_fRadius = fRadius; };

    bool  IsBeingDragged() { return m_ArcBall.IsBeingDragged(); };

protected:
    HRESULT UpdateLightDir();

    D3DXMATRIXA16  m_mRot;
    D3DXMATRIXA16  m_mRotSnapshot;
    static IDirect3DDevice9* s_pd3dDevice;
    static ID3DXEffect* s_pEffect;       
    static ID3DXMesh*   s_pMesh;    

    float          m_fRadius;
    int            m_nRotateMask;
    CD3DArcBall    m_ArcBall;
    D3DXVECTOR3    m_vDefaultDir;
    D3DXVECTOR3    m_vCurrentDir;
    D3DXMATRIX     m_mView;
};


//--------------------------------------------------------------------------------------
// Shared code for samples to ask user if they want to use a REF device or quit
//--------------------------------------------------------------------------------------
void DXUTDisplaySwitchingToREFWarning();

//--------------------------------------------------------------------------------------
// Tries to finds a media file by searching in common locations
//--------------------------------------------------------------------------------------
HRESULT DXUTFindDXSDKMediaFileCch( WCHAR* strDestPath, int cchDest, LPCWSTR strFilename );
HRESULT DXUTSetMediaSearchPath( LPCWSTR strPath );
LPCWSTR DXUTGetMediaSearchPath();


//--------------------------------------------------------------------------------------
// Returns the string for the given D3DFORMAT.
//       bWithPrefix determines whether the string should include the "D3DFMT_"
//--------------------------------------------------------------------------------------
LPCWSTR DXUTD3DFormatToString( D3DFORMAT format, bool bWithPrefix );


//--------------------------------------------------------------------------------------
// Returns a view matrix for rendering to a face of a cubemap.
//--------------------------------------------------------------------------------------
D3DXMATRIX DXUTGetCubeMapViewMatrix( DWORD dwFace );


//--------------------------------------------------------------------------------------
// Helper function to launch the Media Center UI after the program terminates
//--------------------------------------------------------------------------------------
bool DXUTReLaunchMediaCenter();


//--------------------------------------------------------------------------------------
// Debug printing support
// See dxerr.h for more debug printing support
//--------------------------------------------------------------------------------------
void DXUTOutputDebugStringW( LPCWSTR strMsg, ... );
void DXUTOutputDebugStringA( LPCSTR strMsg, ... );
HRESULT WINAPI DXUTTrace( const CHAR* strFile, DWORD dwLine, HRESULT hr, const WCHAR* strMsg, bool bPopMsgBox );
void DXUTTraceDecl( D3DVERTEXELEMENT9 decl[MAX_FVF_DECL_SIZE] );
WCHAR* DXUTTraceD3DDECLUSAGEtoString( BYTE u );
WCHAR* DXUTTraceD3DDECLMETHODtoString( BYTE m );
WCHAR* DXUTTraceD3DDECLTYPEtoString( BYTE t );

#ifdef UNICODE
#define DXUTOutputDebugString DXUTOutputDebugStringW
#else
#define DXUTOutputDebugString DXUTOutputDebugStringA
#endif

// These macros are very similar to dxerr's but it special cases the HRESULT defined
// by DXUT to pop better message boxes. 
#if defined(DEBUG) || defined(_DEBUG)
#define DXUT_ERR(str,hr)           DXUTTrace( __FILE__, (DWORD)__LINE__, hr, str, false )
#define DXUT_ERR_MSGBOX(str,hr)    DXUTTrace( __FILE__, (DWORD)__LINE__, hr, str, true )
#define DXUTTRACE                  DXUTOutputDebugString
#else
#define DXUT_ERR(str,hr)           (hr)
#define DXUT_ERR_MSGBOX(str,hr)    (hr)
#define DXUTTRACE                  (__noop)
#endif


//--------------------------------------------------------------------------------------
// Direct3D9 dynamic linking support -- calls top-level D3D9 APIs with graceful
// failure if APIs are not present.
//--------------------------------------------------------------------------------------

IDirect3D9 * WINAPI DXUT_Dynamic_Direct3DCreate9(UINT SDKVersion);
int WINAPI DXUT_Dynamic_D3DPERF_BeginEvent( D3DCOLOR col, LPCWSTR wszName );
int WINAPI DXUT_Dynamic_D3DPERF_EndEvent( void );
void WINAPI DXUT_Dynamic_D3DPERF_SetMarker( D3DCOLOR col, LPCWSTR wszName );
void WINAPI DXUT_Dynamic_D3DPERF_SetRegion( D3DCOLOR col, LPCWSTR wszName );
BOOL WINAPI DXUT_Dynamic_D3DPERF_QueryRepeatFrame( void );
void WINAPI DXUT_Dynamic_D3DPERF_SetOptions( DWORD dwOptions );
DWORD WINAPI DXUT_Dynamic_D3DPERF_GetStatus( void );


//--------------------------------------------------------------------------------------
// Profiling/instrumentation support
//--------------------------------------------------------------------------------------

//--------------------------------------------------------------------------------------
// Some D3DPERF APIs take a color that can be used when displaying user events in 
// performance analysis tools.  The following constants are provided for your 
// convenience, but you can use any colors you like.
//--------------------------------------------------------------------------------------
const D3DCOLOR DXUT_PERFEVENTCOLOR  = D3DCOLOR_XRGB(200,100,100);
const D3DCOLOR DXUT_PERFEVENTCOLOR2 = D3DCOLOR_XRGB(100,200,100);
const D3DCOLOR DXUT_PERFEVENTCOLOR3 = D3DCOLOR_XRGB(100,100,200);

//--------------------------------------------------------------------------------------
// The following macros provide a convenient way for your code to call the D3DPERF 
// functions only when PROFILE is defined.  If PROFILE is not defined (as for the final 
// release version of a program), these macros evaluate to nothing, so no detailed event
// information is embedded in your shipping program.  It is recommended that you create
// and use three build configurations for your projects:
//     Debug (nonoptimized code, asserts active, PROFILE defined to assist debugging)
//     Profile (optimized code, asserts disabled, PROFILE defined to assist optimization)
//     Release (optimized code, asserts disabled, PROFILE not defined)
//--------------------------------------------------------------------------------------
#ifdef PROFILE
// PROFILE is defined, so these macros call the D3DPERF functions
#define DXUT_BeginPerfEvent( color, pstrMessage )   DXUT_Dynamic_D3DPERF_BeginEvent( color, pstrMessage )
#define DXUT_EndPerfEvent()                         DXUT_Dynamic_D3DPERF_EndEvent()
#define DXUT_SetPerfMarker( color, pstrMessage )    DXUT_Dynamic_D3DPERF_SetMarker( color, pstrMessage )
#else
// PROFILE is not defined, so these macros do nothing
#define DXUT_BeginPerfEvent( color, pstrMessage )   (__noop)
#define DXUT_EndPerfEvent()                         (__noop)
#define DXUT_SetPerfMarker( color, pstrMessage )    (__noop)
#endif

//--------------------------------------------------------------------------------------
// CDXUTPerfEventGenerator is a helper class that makes it easy to attach begin and end
// events to a block of code.  Simply define a CDXUTPerfEventGenerator variable anywhere 
// in a block of code, and the class's constructor will call DXUT_BeginPerfEvent when 
// the block of code begins, and the class's destructor will call DXUT_EndPerfEvent when 
// the block ends.
//--------------------------------------------------------------------------------------
class CDXUTPerfEventGenerator
{
public:
    CDXUTPerfEventGenerator( D3DCOLOR color, LPCWSTR pstrMessage ) { DXUT_BeginPerfEvent( color, pstrMessage ); }
    ~CDXUTPerfEventGenerator( void ) { DXUT_EndPerfEvent(); }
};


//--------------------------------------------------------------------------------------
// Multimon handling to support OSes with or without multimon API support.  
// Purposely avoiding the use of multimon.h so DXUT.lib doesn't require 
// COMPILE_MULTIMON_STUBS and cause complication with MFC or other users of multimon.h
//--------------------------------------------------------------------------------------
#ifndef MONITOR_DEFAULTTOPRIMARY
    #define MONITORINFOF_PRIMARY        0x00000001
    #define MONITOR_DEFAULTTONULL       0x00000000
    #define MONITOR_DEFAULTTOPRIMARY    0x00000001
    #define MONITOR_DEFAULTTONEAREST    0x00000002
    typedef struct tagMONITORINFO
    {
        DWORD   cbSize;
        RECT    rcMonitor;
        RECT    rcWork;
        DWORD   dwFlags;
    } MONITORINFO, *LPMONITORINFO;
    typedef struct tagMONITORINFOEXW : public tagMONITORINFO
    {
        WCHAR       szDevice[CCHDEVICENAME];
    } MONITORINFOEXW, *LPMONITORINFOEXW;
    typedef MONITORINFOEXW MONITORINFOEX;
    typedef LPMONITORINFOEXW LPMONITORINFOEX;
#endif

HMONITOR DXUTMonitorFromWindow( HWND hWnd, DWORD dwFlags );
BOOL     DXUTGetMonitorInfo( HMONITOR hMonitor, LPMONITORINFO lpMonitorInfo );
void     DXUTGetDesktopResolution( UINT AdapterOrdinal, UINT* pWidth, UINT* pHeight );


//--------------------------------------------------------------------------------------
// Implementation of CGrowableArray
//--------------------------------------------------------------------------------------

// This version doesn't call ctor or dtor.
template< typename TYPE >
HRESULT CGrowableArray<TYPE>::SetSizeInternal( int nNewMaxSize )
{
    if( nNewMaxSize < 0 )
    {
        assert( false );
        return E_INVALIDARG;
    }

    if( nNewMaxSize == 0 )
    {
        // Shrink to 0 size & cleanup
        if( m_pData )
        {
            free( m_pData );
            m_pData = NULL;
        }

        m_nMaxSize = 0;
        m_nSize = 0;
    }
    else if( m_pData == NULL || nNewMaxSize > m_nMaxSize )
    {
        // Grow array
        int nGrowBy = ( m_nMaxSize == 0 ) ? 16 : m_nMaxSize;

        // Limit nGrowBy to keep m_nMaxSize less than INT_MAX
        if( (UINT)m_nMaxSize + (UINT)nGrowBy > (UINT)INT_MAX )
            nGrowBy = INT_MAX - m_nMaxSize;

        nNewMaxSize = __max( nNewMaxSize, m_nMaxSize + nGrowBy );

        // Verify that (nNewMaxSize * sizeof(TYPE)) is not greater than UINT_MAX or the realloc will overrun
        if( sizeof(TYPE) > UINT_MAX / (UINT)nNewMaxSize )
            return E_INVALIDARG;

        TYPE* pDataNew = (TYPE*) realloc( m_pData, nNewMaxSize * sizeof(TYPE) );
        if( pDataNew == NULL )
            return E_OUTOFMEMORY;

        m_pData = pDataNew;
        m_nMaxSize = nNewMaxSize;
    }

    return S_OK;
}


//--------------------------------------------------------------------------------------
template< typename TYPE >
HRESULT CGrowableArray<TYPE>::SetSize( int nNewMaxSize )
{
    int nOldSize = m_nSize;

    if( nOldSize > nNewMaxSize )
    {
        // Removing elements. Call dtor.

        for( int i = nNewMaxSize; i < nOldSize; ++i )
            m_pData[i].~TYPE();
    }

    // Adjust buffer.  Note that there's no need to check for error
    // since if it happens, nOldSize == nNewMaxSize will be true.)
    HRESULT hr = SetSizeInternal( nNewMaxSize );

    if( nOldSize < nNewMaxSize )
    {
        // Adding elements. Call ctor.

        for( int i = nOldSize; i < nNewMaxSize; ++i )
            ::new (&m_pData[i]) TYPE;
    }

    return hr;
}


//--------------------------------------------------------------------------------------
template< typename TYPE >
HRESULT CGrowableArray<TYPE>::Add( const TYPE& value )
{
    HRESULT hr;
    if( FAILED( hr = SetSizeInternal( m_nSize + 1 ) ) )
        return hr;

    // Construct the new element
    ::new (&m_pData[m_nSize]) TYPE;

    // Assign
    m_pData[m_nSize] = value;
    ++m_nSize;

    return S_OK;
}


//--------------------------------------------------------------------------------------
template< typename TYPE >
HRESULT CGrowableArray<TYPE>::Insert( int nIndex, const TYPE& value )
{
    HRESULT hr;

    // Validate index
    if( nIndex < 0 || 
        nIndex > m_nSize )
    {
        assert( false );
        return E_INVALIDARG;
    }

    // Prepare the buffer
    if( FAILED( hr = SetSizeInternal( m_nSize + 1 ) ) )
        return hr;

    // Shift the array
    MoveMemory( &m_pData[nIndex+1], &m_pData[nIndex], sizeof(TYPE) * (m_nSize - nIndex) );

    // Construct the new element
    ::new (&m_pData[nIndex]) TYPE;

    // Set the value and increase the size
    m_pData[nIndex] = value;
    ++m_nSize;

    return S_OK;
}


//--------------------------------------------------------------------------------------
template< typename TYPE >
HRESULT CGrowableArray<TYPE>::SetAt( int nIndex, const TYPE& value )
{
    // Validate arguments
    if( nIndex < 0 ||
        nIndex >= m_nSize )
    {
        assert( false );
        return E_INVALIDARG;
    }

    m_pData[nIndex] = value;
    return S_OK;
}


//--------------------------------------------------------------------------------------
// Searches for the specified value and returns the index of the first occurrence
// within the section of the data array that extends from iStart and contains the 
// specified number of elements. Returns -1 if value is not found within the given 
// section.
//--------------------------------------------------------------------------------------
template< typename TYPE >
int CGrowableArray<TYPE>::IndexOf( const TYPE& value, int iStart, int nNumElements )
{
    // Validate arguments
    if( iStart < 0 || 
        iStart >= m_nSize ||
        nNumElements < 0 ||
        iStart + nNumElements > m_nSize )
    {
        assert( false );
        return -1;
    }

    // Search
    for( int i = iStart; i < (iStart + nNumElements); i++ )
    {
        if( value == m_pData[i] )
            return i;
    }

    // Not found
    return -1;
}


//--------------------------------------------------------------------------------------
// Searches for the specified value and returns the index of the last occurrence
// within the section of the data array that contains the specified number of elements
// and ends at iEnd. Returns -1 if value is not found within the given section.
//--------------------------------------------------------------------------------------
template< typename TYPE >
int CGrowableArray<TYPE>::LastIndexOf( const TYPE& value, int iEnd, int nNumElements )
{
    // Validate arguments
    if( iEnd < 0 || 
        iEnd >= m_nSize ||
        nNumElements < 0 ||
        iEnd - nNumElements < 0 )
    {
        assert( false );
        return -1;
    }

    // Search
    for( int i = iEnd; i > (iEnd - nNumElements); i-- )
    {
        if( value == m_pData[i] )
            return i;
    }

    // Not found
    return -1;
}



//--------------------------------------------------------------------------------------
template< typename TYPE >
HRESULT CGrowableArray<TYPE>::Remove( int nIndex )
{
    if( nIndex < 0 || 
        nIndex >= m_nSize )
    {
        assert( false );
        return E_INVALIDARG;
    }

    // Destruct the element to be removed
    m_pData[nIndex].~TYPE();

    // Compact the array and decrease the size
    MoveMemory( &m_pData[nIndex], &m_pData[nIndex+1], sizeof(TYPE) * (m_nSize - (nIndex+1)) );
    --m_nSize;

    return S_OK;
}


//--------------------------------------------------------------------------------------
// Creates a REF or NULLREF device and returns that device.  The caller should call
// Release() when done with the device.
//--------------------------------------------------------------------------------------
IDirect3DDevice9* DXUTCreateRefDevice( HWND hWnd, bool bNullRef = true );


#endif
