//--------------------------------------------------------------------------------------
// File: DXUTSettingsDlg.cpp
//
// Dialog for selection of device settings 
//
// Copyright (c) Microsoft Corporation. All rights reserved
//--------------------------------------------------------------------------------------
#include "stdafx.h"
#include "DXUTgui.h"
#include "DXUTsettingsDlg.h"
#undef min // use __min instead
#undef max // use __max instead


//--------------------------------------------------------------------------------------
// Internal functions forward declarations
//--------------------------------------------------------------------------------------
WCHAR* DXUTPresentIntervalToString( UINT pi );
WCHAR* DXUTMultisampleTypeToString(D3DMULTISAMPLE_TYPE MultiSampleType);
WCHAR* DXUTD3DDeviceTypeToString(D3DDEVTYPE devType);
WCHAR* DXUTVertexProcessingTypeToString(DWORD vpt);


//--------------------------------------------------------------------------------------
// Global state
//--------------------------------------------------------------------------------------
DXUTDeviceSettings g_DeviceSettings;

CD3DSettingsDlg* DXUTGetSettingsDialog()
{
    // Using an accessor function gives control of the construction order
    static CD3DSettingsDlg dlg;
    return &dlg;
}


//--------------------------------------------------------------------------------------
CD3DSettingsDlg::CD3DSettingsDlg()
{
    m_pStateBlock = NULL;
    m_bActive = false;
}


//--------------------------------------------------------------------------------------
CD3DSettingsDlg::~CD3DSettingsDlg()
{
    OnDestroyDevice();
}


//--------------------------------------------------------------------------------------
void CD3DSettingsDlg::Init( CDXUTDialogResourceManager* pManager )
{
    assert( pManager );
    m_Dialog.Init( pManager, false );  // Don't register this dialog.
    CreateControls();
}


//--------------------------------------------------------------------------------------
void CD3DSettingsDlg::Init( CDXUTDialogResourceManager* pManager, LPCWSTR szControlTextureFileName )
{
    assert( pManager );
    m_Dialog.Init( pManager, false, szControlTextureFileName );  // Don't register this dialog.
    CreateControls();
}


//--------------------------------------------------------------------------------------
void CD3DSettingsDlg::Init( CDXUTDialogResourceManager* pManager, LPCWSTR pszControlTextureResourcename, HMODULE hModule )
{
    assert( pManager );
    m_Dialog.Init( pManager, false, pszControlTextureResourcename, hModule );  // Don't register this dialog.
    CreateControls();
}


//--------------------------------------------------------------------------------------
void CD3DSettingsDlg::CreateControls()
{
    m_Dialog.EnableKeyboardInput( true );
    m_Dialog.SetFont( 0, L"Arial", 15, FW_NORMAL );
    m_Dialog.SetFont( 1, L"Arial", 28, FW_BOLD );

    // Right-justify static controls
    CDXUTElement* pElement = m_Dialog.GetDefaultElement( DXUT_CONTROL_STATIC, 0 );
    if( pElement )
    {
        pElement->dwTextFormat = DT_VCENTER | DT_RIGHT;
        
        // Title
        CDXUTStatic* pStatic = NULL;
        m_Dialog.AddStatic( DXUTSETTINGSDLG_STATIC, L"Direct3D Settings", 10, 5, 400, 50, false, &pStatic );
        pElement = pStatic->GetElement( 0 );
        pElement->iFont = 1;
        pElement->dwTextFormat = DT_TOP | DT_LEFT;
    }

    // DXUTSETTINGSDLG_ADAPTER
    m_Dialog.AddStatic( DXUTSETTINGSDLG_STATIC, L"Display Adapter", 10, 50, 180, 23 );
    m_Dialog.AddComboBox( DXUTSETTINGSDLG_ADAPTER, 200, 50, 300, 23 );

    // DXUTSETTINGSDLG_DEVICE_TYPE
    m_Dialog.AddStatic( DXUTSETTINGSDLG_STATIC, L"Render Device", 10, 75, 180, 23 );
    m_Dialog.AddComboBox( DXUTSETTINGSDLG_DEVICE_TYPE, 200, 75, 300, 23 );

    // DXUTSETTINGSDLG_WINDOWED, DXUTSETTINGSDLG_FULLSCREEN
    m_Dialog.AddRadioButton( DXUTSETTINGSDLG_WINDOWED, DXUTSETTINGSDLG_WINDOWED_GROUP, L"Windowed", 240, 105, 300, 16 );
    m_Dialog.AddCheckBox( DXUTSETTINGSDLG_DEVICECLIP, L"Clip to device when window spans across multiple monitors", 250, 126, 400, 16 );
    m_Dialog.AddRadioButton( DXUTSETTINGSDLG_FULLSCREEN, DXUTSETTINGSDLG_WINDOWED_GROUP, L"Full Screen", 240, 147, 300, 16 );

    // DXUTSETTINGSDLG_ADAPTER_FORMAT
    m_Dialog.AddStatic( DXUTSETTINGSDLG_ADAPTER_FORMAT_LABEL, L"Adapter Format", 10, 180, 180, 23 );
    m_Dialog.AddComboBox( DXUTSETTINGSDLG_ADAPTER_FORMAT, 200, 180, 300, 23 );

    // DXUTSETTINGSDLG_RESOLUTION
    m_Dialog.AddStatic( DXUTSETTINGSDLG_RESOLUTION_LABEL, L"Resolution", 10, 205, 180, 23 );
    m_Dialog.AddComboBox( DXUTSETTINGSDLG_RESOLUTION, 200, 205, 200, 23 );
    m_Dialog.GetComboBox( DXUTSETTINGSDLG_RESOLUTION )->SetDropHeight( 106 );

    // DXUTSETTINGSDLG_RES_SHOW_ALL
    m_Dialog.AddCheckBox( DXUTSETTINGSDLG_RESOLUTION_SHOW_ALL, L"Show All Aspect Ratios", 420, 205, 200, 23, false );

    // DXUTSETTINGSDLG_REFRESH_RATE
    m_Dialog.AddStatic( DXUTSETTINGSDLG_REFRESH_RATE_LABEL, L"Refresh Rate", 10, 230, 180, 23 );
    m_Dialog.AddComboBox( DXUTSETTINGSDLG_REFRESH_RATE, 200, 230, 300, 23 );

    // DXUTSETTINGSDLG_BACK_BUFFER_FORMAT
    m_Dialog.AddStatic( DXUTSETTINGSDLG_STATIC, L"Back Buffer Format", 10, 265, 180, 23 );
    m_Dialog.AddComboBox( DXUTSETTINGSDLG_BACK_BUFFER_FORMAT, 200, 265, 300, 23 );

    // DXUTSETTINGSDLG_DEPTH_STENCIL
    m_Dialog.AddStatic( DXUTSETTINGSDLG_STATIC, L"Depth/Stencil Format", 10, 290, 180, 23 );
    m_Dialog.AddComboBox( DXUTSETTINGSDLG_DEPTH_STENCIL, 200, 290, 300, 23 );

    // DXUTSETTINGSDLG_MULTISAMPLE_TYPE
    m_Dialog.AddStatic( DXUTSETTINGSDLG_STATIC, L"Multisample Type", 10, 315, 180, 23 );
    m_Dialog.AddComboBox( DXUTSETTINGSDLG_MULTISAMPLE_TYPE, 200, 315, 300, 23 );

    // DXUTSETTINGSDLG_MULTISAMPLE_QUALITY
    m_Dialog.AddStatic( DXUTSETTINGSDLG_STATIC, L"Multisample Quality", 10, 340, 180, 23 );
    m_Dialog.AddComboBox( DXUTSETTINGSDLG_MULTISAMPLE_QUALITY, 200, 340, 300, 23 );

     // DXUTSETTINGSDLG_VERTEX_PROCESSING
    m_Dialog.AddStatic( DXUTSETTINGSDLG_STATIC, L"Vertex Processing", 10, 365, 180, 23 );
    m_Dialog.AddComboBox( DXUTSETTINGSDLG_VERTEX_PROCESSING, 200, 365, 300, 23 );

     // DXUTSETTINGSDLG_PRESENT_INTERVAL
    m_Dialog.AddStatic( DXUTSETTINGSDLG_STATIC, L"Vertical Sync", 10, 390, 180, 23 );
    m_Dialog.AddComboBox( DXUTSETTINGSDLG_PRESENT_INTERVAL, 200, 390, 300, 23 );
    
    // DXUTSETTINGSDLG_OK, DXUTSETTINGSDLG_CANCEL
    m_Dialog.AddButton( DXUTSETTINGSDLG_OK, L"OK", 230, 435, 73, 31 );
    m_Dialog.AddButton( DXUTSETTINGSDLG_CANCEL, L"Cancel", 315, 435, 73, 31, 0, true );
}


//--------------------------------------------------------------------------------------
HRESULT CD3DSettingsDlg::OnCreateDevice( IDirect3DDevice9* pd3dDevice )
{
    if( pd3dDevice == NULL )
        return DXUT_ERR_MSGBOX( L"CD3DSettingsDlg::OnCreatedDevice", E_INVALIDARG );

    // Create the fonts/textures 
    m_Dialog.SetCallback( StaticOnEvent, (void*) this );
  
    return S_OK;
}


//--------------------------------------------------------------------------------------
// Changes the UI defaults to the current device settings
//--------------------------------------------------------------------------------------
HRESULT CD3DSettingsDlg::Refresh()
{
    HRESULT hr = S_OK;

    CD3DEnumeration* pD3DEnum = DXUTGetEnumeration();
    g_DeviceSettings = DXUTGetDeviceSettings();

    // Fill the UI with the current settings
    AddDeviceType( g_DeviceSettings.DeviceType );
    SetWindowed( FALSE != g_DeviceSettings.pp.Windowed );
    SetDeviceClip( 0 != (g_DeviceSettings.pp.Flags & D3DPRESENTFLAG_DEVICECLIP) );
    AddAdapterFormat( g_DeviceSettings.AdapterFormat );
    AddResolution( g_DeviceSettings.pp.BackBufferWidth, g_DeviceSettings.pp.BackBufferHeight );
    AddRefreshRate( g_DeviceSettings.pp.FullScreen_RefreshRateInHz );
    AddBackBufferFormat( g_DeviceSettings.pp.BackBufferFormat );
    AddDepthStencilBufferFormat( g_DeviceSettings.pp.AutoDepthStencilFormat );
    AddMultisampleType( g_DeviceSettings.pp.MultiSampleType );
    AddMultisampleQuality( g_DeviceSettings.pp.MultiSampleQuality );
    
    if( g_DeviceSettings.BehaviorFlags & D3DCREATE_PUREDEVICE )
        AddVertexProcessingType( D3DCREATE_PUREDEVICE );
    else if( g_DeviceSettings.BehaviorFlags & D3DCREATE_HARDWARE_VERTEXPROCESSING )
        AddVertexProcessingType( D3DCREATE_HARDWARE_VERTEXPROCESSING );
    else if( g_DeviceSettings.BehaviorFlags & D3DCREATE_SOFTWARE_VERTEXPROCESSING )
        AddVertexProcessingType( D3DCREATE_SOFTWARE_VERTEXPROCESSING );
    else if( g_DeviceSettings.BehaviorFlags & D3DCREATE_MIXED_VERTEXPROCESSING )
        AddVertexProcessingType( D3DCREATE_MIXED_VERTEXPROCESSING );

    CD3DEnumDeviceSettingsCombo* pBestDeviceSettingsCombo = pD3DEnum->GetDeviceSettingsCombo( g_DeviceSettings.AdapterOrdinal, g_DeviceSettings.DeviceType, g_DeviceSettings.AdapterFormat, g_DeviceSettings.pp.BackBufferFormat, (g_DeviceSettings.pp.Windowed != 0) );
    if( NULL == pBestDeviceSettingsCombo )
        return DXUT_ERR_MSGBOX( L"GetDeviceSettingsCombo", E_INVALIDARG );    

    // Get the adapters list from CD3DEnumeration object
    CGrowableArray<CD3DEnumAdapterInfo*>* pAdapterInfoList = pD3DEnum->GetAdapterInfoList();

    if( pAdapterInfoList->GetSize() == 0 )
        return DXUT_ERR_MSGBOX( L"CD3DSettingsDlg::OnCreatedDevice", DXUTERR_NOCOMPATIBLEDEVICES );
    
    CDXUTComboBox* pAdapterCombo = m_Dialog.GetComboBox( DXUTSETTINGSDLG_ADAPTER );
    pAdapterCombo->RemoveAllItems();

    // Add adapters
    for( int iAdapter=0; iAdapter<pAdapterInfoList->GetSize(); iAdapter++ )
    {          
        CD3DEnumAdapterInfo* pAdapterInfo = pAdapterInfoList->GetAt(iAdapter);
        AddAdapter( pAdapterInfo->szUniqueDescription, pAdapterInfo->AdapterOrdinal );
    }
    
    pAdapterCombo->SetSelectedByData( ULongToPtr( g_DeviceSettings.AdapterOrdinal ) );

    hr = OnAdapterChanged();
    if( FAILED(hr) )
        return hr;

    //m_Dialog.Refresh();
    CDXUTDialog::SetRefreshTime( (float) DXUTGetTime() );

    return S_OK;
}


//--------------------------------------------------------------------------------------
HRESULT CD3DSettingsDlg::OnResetDevice()
{
    const D3DSURFACE_DESC* pDesc = DXUTGetBackBufferSurfaceDesc();
    m_Dialog.SetLocation( 0, 0 );
    m_Dialog.SetSize( pDesc->Width, pDesc->Height );
    m_Dialog.SetBackgroundColors( D3DCOLOR_ARGB(255, 98, 138, 206), 
                                         D3DCOLOR_ARGB(255, 54, 105, 192),
                                         D3DCOLOR_ARGB(255, 54, 105, 192),
                                         D3DCOLOR_ARGB(255, 10,  73, 179) );
    
    
    IDirect3DDevice9* pd3dDevice = DXUTGetD3DDevice();
    pd3dDevice->BeginStateBlock();
    pd3dDevice->SetRenderState( D3DRS_FILLMODE, D3DFILL_SOLID ); 
    pd3dDevice->EndStateBlock( &m_pStateBlock );

    return S_OK;
}


//--------------------------------------------------------------------------------------
HRESULT CD3DSettingsDlg::OnRender( float fElapsedTime )
{
    IDirect3DDevice9* pd3dDevice = DXUTGetD3DDevice();

    // Clear the render target and the zbuffer 
    pd3dDevice->Clear(0, NULL, D3DCLEAR_TARGET, 0x00003F3F, 1.0f, 0);

    // Render the scene
    if( SUCCEEDED( pd3dDevice->BeginScene() ) )
    {
        m_pStateBlock->Capture();
        pd3dDevice->SetRenderState( D3DRS_FILLMODE, D3DFILL_SOLID ); 
        m_Dialog.OnRender( fElapsedTime );    
        m_pStateBlock->Apply();
        pd3dDevice->EndScene();
    }
    
    return S_OK;
}


//--------------------------------------------------------------------------------------
LRESULT CD3DSettingsDlg::MsgProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam )
{
    m_Dialog.MsgProc( hWnd, uMsg, wParam, lParam );
    if( uMsg == WM_KEYDOWN && wParam == VK_F2 )
        SetActive( false );
    return 0;
}


//--------------------------------------------------------------------------------------
HRESULT CD3DSettingsDlg::OnLostDevice()
{
    SAFE_RELEASE( m_pStateBlock );
    return S_OK;
}


//--------------------------------------------------------------------------------------
HRESULT CD3DSettingsDlg::OnDestroyDevice()
{
    return S_OK;
}


//--------------------------------------------------------------------------------------
void WINAPI CD3DSettingsDlg::StaticOnEvent( UINT nEvent, int nControlID, 
                                            CDXUTControl* pControl, void* pUserData )
{
    CD3DSettingsDlg* pD3DSettings = (CD3DSettingsDlg*) pUserData;
    if( pD3DSettings )
        pD3DSettings->OnEvent( nEvent, nControlID, pControl );
}


//--------------------------------------------------------------------------------------
void CD3DSettingsDlg::OnEvent( UINT nEvent, int nControlID, 
                               CDXUTControl* pControl )
{
    switch( nControlID )
    {
        case DXUTSETTINGSDLG_ADAPTER:               OnAdapterChanged(); break;
        case DXUTSETTINGSDLG_DEVICE_TYPE:           OnDeviceTypeChanged(); break;
        case DXUTSETTINGSDLG_WINDOWED:              OnWindowedFullScreenChanged(); break;
        case DXUTSETTINGSDLG_FULLSCREEN:            OnWindowedFullScreenChanged(); break;
        case DXUTSETTINGSDLG_ADAPTER_FORMAT:        OnAdapterFormatChanged(); break;
        case DXUTSETTINGSDLG_RESOLUTION_SHOW_ALL:   OnAdapterFormatChanged(); break;
        case DXUTSETTINGSDLG_RESOLUTION:            OnResolutionChanged(); break;
        case DXUTSETTINGSDLG_REFRESH_RATE:          OnRefreshRateChanged(); break;
        case DXUTSETTINGSDLG_BACK_BUFFER_FORMAT:    OnBackBufferFormatChanged(); break;
        case DXUTSETTINGSDLG_DEPTH_STENCIL:         OnDepthStencilBufferFormatChanged(); break;
        case DXUTSETTINGSDLG_MULTISAMPLE_TYPE:      OnMultisampleTypeChanged(); break;
        case DXUTSETTINGSDLG_MULTISAMPLE_QUALITY:   OnMultisampleQualityChanged(); break;
        case DXUTSETTINGSDLG_VERTEX_PROCESSING:     OnVertexProcessingChanged(); break;
        case DXUTSETTINGSDLG_PRESENT_INTERVAL:      OnPresentIntervalChanged(); break;
        case DXUTSETTINGSDLG_DEVICECLIP:            OnDeviceClipChanged(); break;

        case DXUTSETTINGSDLG_OK:
        {
            if( g_DeviceSettings.pp.Windowed )
            {
                g_DeviceSettings.pp.FullScreen_RefreshRateInHz = 0;

                RECT rcClient;
                if( DXUTIsWindowed() )
                    GetClientRect( DXUTGetHWND(), &rcClient );
                else
                    rcClient = DXUTGetWindowClientRectAtModeChange();
                DWORD dwWindowWidth  = rcClient.right - rcClient.left;
                DWORD dwWindowHeight = rcClient.bottom - rcClient.top;

                g_DeviceSettings.pp.BackBufferWidth = dwWindowWidth;
                g_DeviceSettings.pp.BackBufferHeight = dwWindowHeight;
            }

            if( g_DeviceSettings.pp.MultiSampleType != D3DMULTISAMPLE_NONE )
            {
                g_DeviceSettings.pp.Flags &= ~D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;
            }

            DXUTCreateDeviceFromSettings( &g_DeviceSettings );

            SetActive( false );
            break;
        }

        case DXUTSETTINGSDLG_CANCEL:                
        {
            SetActive( false );
            break;
        }

    }
}


//-------------------------------------------------------------------------------------
HRESULT CD3DSettingsDlg::SetDeviceSettingsFromUI()
{
    CDXUTComboBox* pComboBox;
    CDXUTRadioButton* pRadioButton;

    // DXUTSETTINGSDLG_DEVICE_TYPE
    pComboBox = m_Dialog.GetComboBox( DXUTSETTINGSDLG_DEVICE_TYPE );
    g_DeviceSettings.DeviceType = (D3DDEVTYPE) PtrToUlong( pComboBox->GetSelectedData() );
    
    // DXUTSETTINGSDLG_WINDOWED
    pRadioButton = m_Dialog.GetRadioButton( DXUTSETTINGSDLG_WINDOWED );
    g_DeviceSettings.pp.Windowed = pRadioButton->GetChecked();

    // DXUTSETTINGSDLG_ADAPTER_FORMAT
    pComboBox = m_Dialog.GetComboBox( DXUTSETTINGSDLG_ADAPTER_FORMAT );
    g_DeviceSettings.AdapterFormat = (D3DFORMAT) PtrToUlong( pComboBox->GetSelectedData() );
    

    if( g_DeviceSettings.pp.Windowed )
    {
        g_DeviceSettings.pp.BackBufferFormat = D3DFMT_UNKNOWN;
        g_DeviceSettings.pp.FullScreen_RefreshRateInHz = 0;
    }
    else
    {
        // DXUTSETTINGSDLG_BACK_BUFFER_FORMAT
        pComboBox = m_Dialog.GetComboBox( DXUTSETTINGSDLG_BACK_BUFFER_FORMAT );
        g_DeviceSettings.pp.BackBufferFormat = (D3DFORMAT) PtrToUlong( pComboBox->GetSelectedData() );
    
        // DXUTSETTINGSDLG_RESOLUTION
        pComboBox = m_Dialog.GetComboBox( DXUTSETTINGSDLG_RESOLUTION );
        DWORD dwResolution = PtrToUlong( pComboBox->GetSelectedData() );
        g_DeviceSettings.pp.BackBufferWidth = HIWORD( dwResolution );
        g_DeviceSettings.pp.BackBufferHeight = LOWORD( dwResolution );
        
        // DXUTSETTINGSDLG_REFRESH_RATE
        pComboBox = m_Dialog.GetComboBox( DXUTSETTINGSDLG_REFRESH_RATE );
        g_DeviceSettings.pp.FullScreen_RefreshRateInHz = PtrToUlong( pComboBox->GetSelectedData() );
    }

    // DXUTSETTINGSDLG_DEPTH_STENCIL
    pComboBox = m_Dialog.GetComboBox( DXUTSETTINGSDLG_DEPTH_STENCIL );
    g_DeviceSettings.pp.AutoDepthStencilFormat = (D3DFORMAT) PtrToUlong( pComboBox->GetSelectedData() );
    
    return S_OK;
}


//-------------------------------------------------------------------------------------
CD3DEnumAdapterInfo* CD3DSettingsDlg::GetCurrentAdapterInfo()
{
    CD3DEnumeration* pD3DEnum = DXUTGetEnumeration();
    return pD3DEnum->GetAdapterInfo( g_DeviceSettings.AdapterOrdinal );
}


//-------------------------------------------------------------------------------------
CD3DEnumDeviceInfo* CD3DSettingsDlg::GetCurrentDeviceInfo()
{
    CD3DEnumeration* pD3DEnum = DXUTGetEnumeration();
    return pD3DEnum->GetDeviceInfo( g_DeviceSettings.AdapterOrdinal,
                                      g_DeviceSettings.DeviceType );
}


//-------------------------------------------------------------------------------------
CD3DEnumDeviceSettingsCombo* CD3DSettingsDlg::GetCurrentDeviceSettingsCombo()
{
    CD3DEnumeration* pD3DEnum = DXUTGetEnumeration();
    return pD3DEnum->GetDeviceSettingsCombo( g_DeviceSettings.AdapterOrdinal,
                                             g_DeviceSettings.DeviceType,
                                             g_DeviceSettings.AdapterFormat,
                                             g_DeviceSettings.pp.BackBufferFormat,
                                             (g_DeviceSettings.pp.Windowed == TRUE) );
}


//-------------------------------------------------------------------------------------
HRESULT CD3DSettingsDlg::OnAdapterChanged()
{
    HRESULT hr = S_OK;

    // Store the adapter index
    g_DeviceSettings.AdapterOrdinal = GetSelectedAdapter();
    
    // DXUTSETTINGSDLG_DEVICE_TYPE
    CDXUTComboBox* pDeviceTypeComboBox = m_Dialog.GetComboBox( DXUTSETTINGSDLG_DEVICE_TYPE );
    pDeviceTypeComboBox->RemoveAllItems();
 
    CD3DEnumAdapterInfo* pAdapterInfo = GetCurrentAdapterInfo();
    if( pAdapterInfo == NULL )
        return E_FAIL;

    for( int iDeviceInfo=0; iDeviceInfo < pAdapterInfo->deviceInfoList.GetSize(); iDeviceInfo++ )
    {
        CD3DEnumDeviceInfo* pDeviceInfo = pAdapterInfo->deviceInfoList.GetAt(iDeviceInfo);
        AddDeviceType( pDeviceInfo->DeviceType );
    }

    pDeviceTypeComboBox->SetSelectedByData( ULongToPtr(g_DeviceSettings.DeviceType) );

    hr = OnDeviceTypeChanged();
    if( FAILED(hr) )
        return hr;

    return S_OK; 
}



//-------------------------------------------------------------------------------------
HRESULT CD3DSettingsDlg::OnDeviceTypeChanged()
{
    HRESULT hr = S_OK;
    
    g_DeviceSettings.DeviceType = GetSelectedDeviceType();
   
    // Update windowed/full screen radio buttons
    bool bHasWindowedDeviceCombo = false;
    bool bHasFullScreenDeviceCombo = false;

    CD3DEnumDeviceInfo* pDeviceInfo = GetCurrentDeviceInfo();
    if( pDeviceInfo == NULL )
        return E_FAIL;
            
    for( int idc = 0; idc < pDeviceInfo->deviceSettingsComboList.GetSize(); idc++ )
    {
        CD3DEnumDeviceSettingsCombo* pDeviceSettingsCombo = pDeviceInfo->deviceSettingsComboList.GetAt( idc );

        if( pDeviceSettingsCombo->Windowed )
            bHasWindowedDeviceCombo = true;
        else
            bHasFullScreenDeviceCombo = true;
    }

    // DXUTSETTINGSDLG_WINDOWED, DXUTSETTINGSDLG_FULLSCREEN
    m_Dialog.SetControlEnabled( DXUTSETTINGSDLG_WINDOWED, bHasWindowedDeviceCombo );
    m_Dialog.SetControlEnabled( DXUTSETTINGSDLG_FULLSCREEN, bHasFullScreenDeviceCombo );

    SetWindowed( g_DeviceSettings.pp.Windowed && bHasWindowedDeviceCombo );

    hr = OnWindowedFullScreenChanged();
    if( FAILED(hr) )
        return hr;

    return S_OK;
}



//-------------------------------------------------------------------------------------
HRESULT CD3DSettingsDlg::OnWindowedFullScreenChanged()
{
    HRESULT hr = S_OK;

    bool bWindowed = IsWindowed();
    g_DeviceSettings.pp.Windowed = bWindowed;

    m_Dialog.SetControlEnabled( DXUTSETTINGSDLG_ADAPTER_FORMAT_LABEL, !bWindowed );
    m_Dialog.SetControlEnabled( DXUTSETTINGSDLG_RESOLUTION_LABEL, !bWindowed );
    m_Dialog.SetControlEnabled( DXUTSETTINGSDLG_REFRESH_RATE_LABEL, !bWindowed );

    m_Dialog.SetControlEnabled( DXUTSETTINGSDLG_ADAPTER_FORMAT, !bWindowed );
    m_Dialog.SetControlEnabled( DXUTSETTINGSDLG_RESOLUTION, !bWindowed );
    m_Dialog.SetControlEnabled( DXUTSETTINGSDLG_RESOLUTION_SHOW_ALL, !bWindowed );
    m_Dialog.SetControlEnabled( DXUTSETTINGSDLG_REFRESH_RATE, !bWindowed );
    m_Dialog.SetControlEnabled( DXUTSETTINGSDLG_DEVICECLIP, bWindowed );

    bool bDeviceClip = ( 0x0 != (g_DeviceSettings.pp.Flags & D3DPRESENTFLAG_DEVICECLIP) );

    // If windowed, get the appropriate adapter format from Direct3D
    if( g_DeviceSettings.pp.Windowed )
    {
        IDirect3D9* pD3D = DXUTGetD3DObject();
        if( pD3D == NULL )
            return DXTRACE_ERR( L"DXUTGetD3DObject", E_FAIL );

        D3DDISPLAYMODE mode;
        hr = pD3D->GetAdapterDisplayMode( g_DeviceSettings.AdapterOrdinal, &mode );
        if( FAILED(hr) )
            return DXTRACE_ERR( L"GetAdapterDisplayMode", hr );

        // Default resolution to the fullscreen res that was last used
        RECT rc = DXUTGetFullsceenClientRectAtModeChange();
        if( rc.right == 0 || rc.bottom == 0 )
        {
            // If nothing last used, then default to the adapter desktop res
            g_DeviceSettings.pp.BackBufferWidth = mode.Width;
            g_DeviceSettings.pp.BackBufferHeight = mode.Height;
        }
        else
        {
            g_DeviceSettings.pp.BackBufferWidth = rc.right;
            g_DeviceSettings.pp.BackBufferHeight = rc.bottom;
        }

        g_DeviceSettings.AdapterFormat = mode.Format;
        g_DeviceSettings.pp.FullScreen_RefreshRateInHz = mode.RefreshRate;
    }

    const D3DFORMAT adapterFormat = g_DeviceSettings.AdapterFormat;
    const DWORD dwWidth = g_DeviceSettings.pp.BackBufferWidth;
    const DWORD dwHeight = g_DeviceSettings.pp.BackBufferHeight;
    const DWORD dwRefreshRate = g_DeviceSettings.pp.FullScreen_RefreshRateInHz;

    // DXUTSETTINGSDLG_DEVICECLIP
    SetDeviceClip( bDeviceClip );
    
    // DXUTSETTINGSDLG_ADAPTER_FORMAT
    CDXUTComboBox* pAdapterFormatComboBox = m_Dialog.GetComboBox( DXUTSETTINGSDLG_ADAPTER_FORMAT );
    if( pAdapterFormatComboBox == NULL )
        return E_FAIL;
    pAdapterFormatComboBox->RemoveAllItems();

    CD3DEnumDeviceInfo* pDeviceInfo = GetCurrentDeviceInfo();
    if( pDeviceInfo == NULL )
        return E_FAIL;

    if( bWindowed )
    {
        AddAdapterFormat( adapterFormat );
    }
    else
    {
        for( int iSettingsCombo=0; iSettingsCombo < pDeviceInfo->deviceSettingsComboList.GetSize(); iSettingsCombo++ )
        {
            CD3DEnumDeviceSettingsCombo* pSettingsCombo = pDeviceInfo->deviceSettingsComboList.GetAt(iSettingsCombo);
            AddAdapterFormat( pSettingsCombo->AdapterFormat );
        }    
    }

    pAdapterFormatComboBox->SetSelectedByData( ULongToPtr(adapterFormat) );

    hr = OnAdapterFormatChanged();
    if( FAILED(hr) )
        return hr;

    // DXUTSETTINGSDLG_RESOLUTION
    CDXUTComboBox* pResolutionComboBox = m_Dialog.GetComboBox( DXUTSETTINGSDLG_RESOLUTION );
    
    if( bWindowed )
    {
        pResolutionComboBox->RemoveAllItems();
        AddResolution( dwWidth, dwHeight );
    }

    pResolutionComboBox->SetSelectedByData( ULongToPtr( MAKELONG(dwWidth, dwHeight) ) );
    
    hr = OnResolutionChanged();
    if( FAILED(hr) )
        return hr;

    // DXUTSETTINGSDLG_REFRESH_RATE
    CDXUTComboBox* pRefreshRateComboBox = m_Dialog.GetComboBox( DXUTSETTINGSDLG_REFRESH_RATE );
    
    if( bWindowed )
    {
        pRefreshRateComboBox->RemoveAllItems();
        AddRefreshRate( dwRefreshRate );
    }
    
    pRefreshRateComboBox->SetSelectedByData( ULongToPtr(dwRefreshRate) ); 

    hr = OnRefreshRateChanged();
    if( FAILED(hr) )
        return hr;

    return S_OK;
}


//-------------------------------------------------------------------------------------
HRESULT CD3DSettingsDlg::OnAdapterFormatChanged()
{ 
    HRESULT hr = S_OK;

    // DXUTSETTINGSDLG_ADAPTER_FORMAT
    D3DFORMAT adapterFormat = GetSelectedAdapterFormat();
    g_DeviceSettings.AdapterFormat = adapterFormat;

    // DXUTSETTINGSDLG_RESOLUTION
    CDXUTComboBox* pResolutionComboBox = m_Dialog.GetComboBox( DXUTSETTINGSDLG_RESOLUTION );
    pResolutionComboBox->RemoveAllItems();

    CD3DEnumAdapterInfo* pAdapterInfo = GetCurrentAdapterInfo();
    if( pAdapterInfo == NULL )
        return E_FAIL;

    bool bShowAll = m_Dialog.GetCheckBox( DXUTSETTINGSDLG_RESOLUTION_SHOW_ALL )->GetChecked();

    // Get the desktop aspect ratio
    D3DDISPLAYMODE dmDesktop;
    DXUTGetDesktopResolution( g_DeviceSettings.AdapterOrdinal, &dmDesktop.Width, &dmDesktop.Height );
    float fDesktopAspectRatio = dmDesktop.Width / (float)dmDesktop.Height;

    for( int idm = 0; idm < pAdapterInfo->displayModeList.GetSize(); idm++ )
    {
        D3DDISPLAYMODE DisplayMode = pAdapterInfo->displayModeList.GetAt( idm );
        float fAspect = (float)DisplayMode.Width / (float)DisplayMode.Height;

        if( DisplayMode.Format == adapterFormat )
        {
            // If "Show All" is not checked, then hide all resolutions
            // that don't match the aspect ratio of the desktop resolution
            if( bShowAll || (!bShowAll && fabsf(fDesktopAspectRatio - fAspect) < 0.05f) )
            {
                AddResolution( DisplayMode.Width, DisplayMode.Height );    
            }
        }
    }

    const DWORD dwCurResolution = MAKELONG( g_DeviceSettings.pp.BackBufferWidth, 
                                            g_DeviceSettings.pp.BackBufferHeight );

    pResolutionComboBox->SetSelectedByData( ULongToPtr(dwCurResolution) );

    hr = OnResolutionChanged();
    if( FAILED(hr) )
        return hr;

    // DXUTSETTINGSDLG_BACK_BUFFER_FORMAT
    CDXUTComboBox* pBackBufferFormatComboBox = m_Dialog.GetComboBox( DXUTSETTINGSDLG_BACK_BUFFER_FORMAT );
    pBackBufferFormatComboBox->RemoveAllItems();

    CD3DEnumDeviceInfo* pDeviceInfo = GetCurrentDeviceInfo();
    if( pDeviceInfo == NULL )
        return E_FAIL;

    const BOOL bWindowed = IsWindowed();
    bool bHasWindowedBackBuffer = false;

    for( int idc = 0; idc < pDeviceInfo->deviceSettingsComboList.GetSize(); idc++ )
    {
        CD3DEnumDeviceSettingsCombo* pDeviceCombo = pDeviceInfo->deviceSettingsComboList.GetAt( idc );
        if( pDeviceCombo->Windowed == bWindowed &&
            pDeviceCombo->AdapterFormat == g_DeviceSettings.AdapterFormat )
        {
            AddBackBufferFormat( pDeviceCombo->BackBufferFormat );
            bHasWindowedBackBuffer = true;
        }
    }

    pBackBufferFormatComboBox->SetSelectedByData( ULongToPtr(g_DeviceSettings.pp.BackBufferFormat) );

    hr = OnBackBufferFormatChanged();
    if( FAILED(hr) )
        return hr;

    if( !bHasWindowedBackBuffer )
    {
        m_Dialog.SetControlEnabled( DXUTSETTINGSDLG_WINDOWED, false );

        if( g_DeviceSettings.pp.Windowed )
        {
            SetWindowed( false );

            hr = OnWindowedFullScreenChanged();
            if( FAILED(hr) )
                return hr;
        }
    }

    return S_OK;
}


//-------------------------------------------------------------------------------------
HRESULT CD3DSettingsDlg::OnResolutionChanged()
{
    HRESULT hr = S_OK;

    CD3DEnumAdapterInfo* pAdapterInfo = GetCurrentAdapterInfo();
    if( pAdapterInfo == NULL )
        return E_FAIL;

    // Set resolution
    DWORD dwWidth, dwHeight;
    GetSelectedResolution( &dwWidth, &dwHeight );
    g_DeviceSettings.pp.BackBufferWidth = dwWidth;
    g_DeviceSettings.pp.BackBufferHeight = dwHeight;

    DWORD dwRefreshRate = g_DeviceSettings.pp.FullScreen_RefreshRateInHz;

    // Update the refresh rate list
    CDXUTComboBox* pRefreshRateComboBox = m_Dialog.GetComboBox( DXUTSETTINGSDLG_REFRESH_RATE );
    pRefreshRateComboBox->RemoveAllItems();

    D3DFORMAT adapterFormat = g_DeviceSettings.AdapterFormat;
    for( int idm = 0; idm < pAdapterInfo->displayModeList.GetSize(); idm++ )
    {
        D3DDISPLAYMODE displayMode = pAdapterInfo->displayModeList.GetAt( idm );

        if( displayMode.Format == adapterFormat &&
            displayMode.Width == dwWidth &&
            displayMode.Height == dwHeight )
        {
            AddRefreshRate( displayMode.RefreshRate );
        }
    }

    pRefreshRateComboBox->SetSelectedByData( ULongToPtr(dwRefreshRate) );

    hr = OnRefreshRateChanged();
    if( FAILED(hr) )
        return hr;

    return S_OK;
}


//-------------------------------------------------------------------------------------
HRESULT CD3DSettingsDlg::OnRefreshRateChanged()
{
    // Set refresh rate
    g_DeviceSettings.pp.FullScreen_RefreshRateInHz = GetSelectedRefreshRate();

    return S_OK;
}


//-------------------------------------------------------------------------------------
HRESULT CD3DSettingsDlg::OnBackBufferFormatChanged()
{
    HRESULT hr = S_OK;

    g_DeviceSettings.pp.BackBufferFormat = GetSelectedBackBufferFormat();
    
    D3DFORMAT adapterFormat = g_DeviceSettings.AdapterFormat;
    D3DFORMAT backBufferFormat = g_DeviceSettings.pp.BackBufferFormat;

    CD3DEnumDeviceInfo* pDeviceInfo = GetCurrentDeviceInfo();
    if( pDeviceInfo == NULL )
        return E_FAIL;

    bool bAllowSoftwareVP, bAllowHardwareVP, bAllowPureHardwareVP, bAllowMixedVP;
    DXUTGetEnumeration()->GetPossibleVertexProcessingList( &bAllowSoftwareVP, &bAllowHardwareVP, 
                                                           &bAllowPureHardwareVP, &bAllowMixedVP );
    
    for( int idc=0; idc < pDeviceInfo->deviceSettingsComboList.GetSize(); idc++ )
    {
        CD3DEnumDeviceSettingsCombo* pDeviceCombo = pDeviceInfo->deviceSettingsComboList.GetAt( idc );

        if( pDeviceCombo->Windowed == (g_DeviceSettings.pp.Windowed == TRUE) &&
            pDeviceCombo->AdapterFormat == adapterFormat &&
            pDeviceCombo->BackBufferFormat == backBufferFormat )
        {
            CDXUTComboBox* pDepthStencilComboBox = m_Dialog.GetComboBox( DXUTSETTINGSDLG_DEPTH_STENCIL );
            pDepthStencilComboBox->RemoveAllItems();
            pDepthStencilComboBox->SetEnabled( (g_DeviceSettings.pp.EnableAutoDepthStencil == TRUE) ); 

            if( g_DeviceSettings.pp.EnableAutoDepthStencil )
            {
                for( int ifmt=0; ifmt < pDeviceCombo->depthStencilFormatList.GetSize(); ifmt++ )
                {
                    D3DFORMAT fmt = pDeviceCombo->depthStencilFormatList.GetAt( ifmt );

                    AddDepthStencilBufferFormat( fmt );
                }

                pDepthStencilComboBox->SetSelectedByData( ULongToPtr(g_DeviceSettings.pp.AutoDepthStencilFormat) );
            }
            else
            {
                if( !pDepthStencilComboBox->ContainsItem( L"(not used)" ) )
                    pDepthStencilComboBox->AddItem( L"(not used)", NULL );
            }

            hr = OnDepthStencilBufferFormatChanged();
            if( FAILED(hr) )
                return hr;

            CDXUTComboBox* pVertexProcessingComboBox = m_Dialog.GetComboBox( DXUTSETTINGSDLG_VERTEX_PROCESSING );
            pVertexProcessingComboBox->RemoveAllItems();

            // Add valid vertex processing types
            if( bAllowSoftwareVP )
                AddVertexProcessingType( D3DCREATE_SOFTWARE_VERTEXPROCESSING );

            if( bAllowHardwareVP && pDeviceInfo->Caps.DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT )
                AddVertexProcessingType( D3DCREATE_HARDWARE_VERTEXPROCESSING );

            if( bAllowPureHardwareVP && pDeviceInfo->Caps.DevCaps & D3DDEVCAPS_PUREDEVICE )
                AddVertexProcessingType( D3DCREATE_PUREDEVICE );

            if( bAllowMixedVP && pDeviceInfo->Caps.DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT )
                AddVertexProcessingType( D3DCREATE_MIXED_VERTEXPROCESSING );

            if( g_DeviceSettings.BehaviorFlags & D3DCREATE_PUREDEVICE )
                pVertexProcessingComboBox->SetSelectedByData( ULongToPtr(D3DCREATE_PUREDEVICE) );
            else if( g_DeviceSettings.BehaviorFlags & D3DCREATE_SOFTWARE_VERTEXPROCESSING )
                pVertexProcessingComboBox->SetSelectedByData( ULongToPtr(D3DCREATE_SOFTWARE_VERTEXPROCESSING) );
            else if( g_DeviceSettings.BehaviorFlags & D3DCREATE_HARDWARE_VERTEXPROCESSING )
                pVertexProcessingComboBox->SetSelectedByData( ULongToPtr(D3DCREATE_HARDWARE_VERTEXPROCESSING) );
            else if( g_DeviceSettings.BehaviorFlags & D3DCREATE_MIXED_VERTEXPROCESSING )
                pVertexProcessingComboBox->SetSelectedByData( ULongToPtr(D3DCREATE_MIXED_VERTEXPROCESSING) );

            hr = OnVertexProcessingChanged();
            if( FAILED(hr) )
                return hr;

            CDXUTComboBox* pPresentIntervalComboBox = m_Dialog.GetComboBox( DXUTSETTINGSDLG_PRESENT_INTERVAL );
            pPresentIntervalComboBox->RemoveAllItems();
            pPresentIntervalComboBox->AddItem( L"On", ULongToPtr(D3DPRESENT_INTERVAL_DEFAULT) );
            pPresentIntervalComboBox->AddItem( L"Off", ULongToPtr(D3DPRESENT_INTERVAL_IMMEDIATE) );

            pPresentIntervalComboBox->SetSelectedByData( ULongToPtr( g_DeviceSettings.pp.PresentationInterval ) );
        
            hr = OnPresentIntervalChanged();
            if( FAILED(hr) )
                return hr;
        }
    }

    return S_OK;
}


//-------------------------------------------------------------------------------------
HRESULT CD3DSettingsDlg::OnDepthStencilBufferFormatChanged()
{
    HRESULT hr = S_OK;

    D3DFORMAT depthStencilBufferFormat = GetSelectedDepthStencilBufferFormat();

    if( g_DeviceSettings.pp.EnableAutoDepthStencil )
        g_DeviceSettings.pp.AutoDepthStencilFormat = depthStencilBufferFormat;

    CD3DEnumDeviceSettingsCombo* pDeviceSettingsCombo = GetCurrentDeviceSettingsCombo();
    if( pDeviceSettingsCombo == NULL )
        return E_FAIL;
    
    CDXUTComboBox* pMultisampleTypeCombo = m_Dialog.GetComboBox( DXUTSETTINGSDLG_MULTISAMPLE_TYPE );
    pMultisampleTypeCombo->RemoveAllItems();

    for( int ims=0; ims < pDeviceSettingsCombo->multiSampleTypeList.GetSize(); ims++ )
    {
        D3DMULTISAMPLE_TYPE msType = pDeviceSettingsCombo->multiSampleTypeList.GetAt( ims );

        bool bConflictFound = false;
        for( int iConf = 0; iConf < pDeviceSettingsCombo->DSMSConflictList.GetSize(); iConf++ )
        {
            CD3DEnumDSMSConflict DSMSConf = pDeviceSettingsCombo->DSMSConflictList.GetAt( iConf );
            if( DSMSConf.DSFormat == depthStencilBufferFormat &&
                DSMSConf.MSType == msType )
            {
                bConflictFound = true;
                break;
            }
        }

        if( !bConflictFound )
            AddMultisampleType( msType );
    }

    CDXUTComboBox* pMultisampleQualityCombo = m_Dialog.GetComboBox( DXUTSETTINGSDLG_MULTISAMPLE_TYPE );
    pMultisampleQualityCombo->SetSelectedByData( ULongToPtr(g_DeviceSettings.pp.MultiSampleType) );

    hr = OnMultisampleTypeChanged();
    if( FAILED(hr) )
        return hr;

    return S_OK;
}


//-------------------------------------------------------------------------------------
HRESULT CD3DSettingsDlg::OnMultisampleTypeChanged()
{
    HRESULT hr = S_OK;

    D3DMULTISAMPLE_TYPE multisampleType = GetSelectedMultisampleType();
    g_DeviceSettings.pp.MultiSampleType = multisampleType;

    CD3DEnumDeviceSettingsCombo* pDeviceSettingsCombo = GetCurrentDeviceSettingsCombo();
    if( pDeviceSettingsCombo == NULL )
        return E_FAIL;

    DWORD dwMaxQuality = 0;
    for( int iType = 0; iType < pDeviceSettingsCombo->multiSampleTypeList.GetSize(); iType++ )
    {
        D3DMULTISAMPLE_TYPE msType = pDeviceSettingsCombo->multiSampleTypeList.GetAt( iType );
        if( msType == multisampleType )
        {
            dwMaxQuality = pDeviceSettingsCombo->multiSampleQualityList.GetAt( iType );
            break;
        }
    }
   
    // DXUTSETTINGSDLG_MULTISAMPLE_QUALITY
    CDXUTComboBox* pMultisampleQualityCombo = m_Dialog.GetComboBox( DXUTSETTINGSDLG_MULTISAMPLE_QUALITY );
    pMultisampleQualityCombo->RemoveAllItems();

    for( UINT iQuality = 0; iQuality < dwMaxQuality; iQuality++ )
    {
        AddMultisampleQuality( iQuality );
    }

    pMultisampleQualityCombo->SetSelectedByData( ULongToPtr(g_DeviceSettings.pp.MultiSampleQuality) );

    hr = OnMultisampleQualityChanged();
    if( FAILED(hr) )
        return hr;

    return S_OK;
}


//-------------------------------------------------------------------------------------
HRESULT CD3DSettingsDlg::OnMultisampleQualityChanged()
{
    g_DeviceSettings.pp.MultiSampleQuality = GetSelectedMultisampleQuality();

    return S_OK;
}


//-------------------------------------------------------------------------------------
HRESULT CD3DSettingsDlg::OnVertexProcessingChanged()
{
    DWORD dwBehavior = g_DeviceSettings.BehaviorFlags;

    // Clear vertex processing flags
    dwBehavior &= ~D3DCREATE_HARDWARE_VERTEXPROCESSING;
    dwBehavior &= ~D3DCREATE_SOFTWARE_VERTEXPROCESSING;
    dwBehavior &= ~D3DCREATE_MIXED_VERTEXPROCESSING;
    dwBehavior &= ~D3DCREATE_PUREDEVICE;

    // Determine new flags
    DWORD dwNewFlags = GetSelectedVertexProcessingType();
    if( dwNewFlags & D3DCREATE_PUREDEVICE )
        dwNewFlags |= D3DCREATE_HARDWARE_VERTEXPROCESSING;

    // Make changes
    g_DeviceSettings.BehaviorFlags = dwBehavior | dwNewFlags;

    return S_OK;
}


//-------------------------------------------------------------------------------------
HRESULT CD3DSettingsDlg::OnPresentIntervalChanged()
{
    g_DeviceSettings.pp.PresentationInterval = GetSelectedPresentInterval();

    return S_OK;
}


//-------------------------------------------------------------------------------------
HRESULT CD3DSettingsDlg::OnDeviceClipChanged()
{
    if( IsDeviceClip() )
        g_DeviceSettings.pp.Flags |= D3DPRESENTFLAG_DEVICECLIP;
    else
        g_DeviceSettings.pp.Flags &= ~D3DPRESENTFLAG_DEVICECLIP;

    return S_OK;
}


//-------------------------------------------------------------------------------------
void CD3DSettingsDlg::AddAdapter( const WCHAR* strDescription, UINT iAdapter )
{
    CDXUTComboBox* pComboBox = m_Dialog.GetComboBox( DXUTSETTINGSDLG_ADAPTER );
 
    if( !pComboBox->ContainsItem( strDescription ) )
        pComboBox->AddItem( strDescription, ULongToPtr(iAdapter) );
}


//-------------------------------------------------------------------------------------
UINT CD3DSettingsDlg::GetSelectedAdapter()
{
    CDXUTComboBox* pComboBox = m_Dialog.GetComboBox( DXUTSETTINGSDLG_ADAPTER );

    return PtrToUlong( pComboBox->GetSelectedData() );  
}


//-------------------------------------------------------------------------------------
void CD3DSettingsDlg::AddDeviceType( D3DDEVTYPE devType )
{
    CDXUTComboBox* pComboBox = m_Dialog.GetComboBox( DXUTSETTINGSDLG_DEVICE_TYPE );

    if( !pComboBox->ContainsItem( DXUTD3DDeviceTypeToString(devType) ) )
        pComboBox->AddItem( DXUTD3DDeviceTypeToString(devType), ULongToPtr(devType) );
}


//-------------------------------------------------------------------------------------
D3DDEVTYPE CD3DSettingsDlg::GetSelectedDeviceType()
{
    CDXUTComboBox* pComboBox = m_Dialog.GetComboBox( DXUTSETTINGSDLG_DEVICE_TYPE );

    return (D3DDEVTYPE) PtrToUlong( pComboBox->GetSelectedData() );
}


//-------------------------------------------------------------------------------------
void CD3DSettingsDlg::SetWindowed( bool bWindowed )
{
    CDXUTRadioButton* pRadioButton = m_Dialog.GetRadioButton( DXUTSETTINGSDLG_WINDOWED );
    pRadioButton->SetChecked( bWindowed );

    pRadioButton = m_Dialog.GetRadioButton( DXUTSETTINGSDLG_FULLSCREEN );
    pRadioButton->SetChecked( !bWindowed );
}


//-------------------------------------------------------------------------------------
bool CD3DSettingsDlg::IsWindowed()
{
    CDXUTRadioButton* pRadioButton = m_Dialog.GetRadioButton( DXUTSETTINGSDLG_WINDOWED );
    return pRadioButton->GetChecked();
}


//-------------------------------------------------------------------------------------
void CD3DSettingsDlg::AddAdapterFormat( D3DFORMAT format )
{
    CDXUTComboBox* pComboBox = m_Dialog.GetComboBox( DXUTSETTINGSDLG_ADAPTER_FORMAT );
    
    if( !pComboBox->ContainsItem( DXUTD3DFormatToString(format, TRUE) ) )
        pComboBox->AddItem( DXUTD3DFormatToString(format, TRUE), ULongToPtr( format ) );
}


//-------------------------------------------------------------------------------------
D3DFORMAT CD3DSettingsDlg::GetSelectedAdapterFormat()
{
    CDXUTComboBox* pComboBox = m_Dialog.GetComboBox( DXUTSETTINGSDLG_ADAPTER_FORMAT );
  
    return (D3DFORMAT) PtrToUlong( pComboBox->GetSelectedData() ); 
}


//-------------------------------------------------------------------------------------
void CD3DSettingsDlg::AddResolution( DWORD dwWidth, DWORD dwHeight )
{
    CDXUTComboBox* pComboBox = m_Dialog.GetComboBox( DXUTSETTINGSDLG_RESOLUTION );
  
    DWORD dwResolutionData;
    WCHAR strResolution[50];
    dwResolutionData = MAKELONG( dwWidth, dwHeight );
    StringCchPrintf( strResolution, 50, L"%d by %d", dwWidth, dwHeight );

    if( !pComboBox->ContainsItem( strResolution ) )
        pComboBox->AddItem( strResolution, ULongToPtr( dwResolutionData ) );
}


//-------------------------------------------------------------------------------------
void CD3DSettingsDlg::GetSelectedResolution( DWORD* pdwWidth, DWORD* pdwHeight )
{
    CDXUTComboBox* pComboBox = m_Dialog.GetComboBox( DXUTSETTINGSDLG_RESOLUTION );

    DWORD dwResolution = PtrToUlong( pComboBox->GetSelectedData() );

    *pdwWidth = LOWORD( dwResolution );
    *pdwHeight = HIWORD( dwResolution );
}


//-------------------------------------------------------------------------------------
void CD3DSettingsDlg::AddRefreshRate( DWORD dwRate )
{
    CDXUTComboBox* pComboBox = m_Dialog.GetComboBox( DXUTSETTINGSDLG_REFRESH_RATE );
        
    WCHAR strRefreshRate[50];

    if( dwRate == 0 )
        StringCchCopy( strRefreshRate, 50, L"Default Rate" );
    else
        StringCchPrintf( strRefreshRate, 50, L"%d Hz", dwRate );

    if( !pComboBox->ContainsItem( strRefreshRate ) )
        pComboBox->AddItem( strRefreshRate, ULongToPtr(dwRate) );
}


//-------------------------------------------------------------------------------------
DWORD CD3DSettingsDlg::GetSelectedRefreshRate()
{
    CDXUTComboBox* pComboBox = m_Dialog.GetComboBox( DXUTSETTINGSDLG_REFRESH_RATE );
    
    return PtrToUlong( pComboBox->GetSelectedData() );
}


//-------------------------------------------------------------------------------------
void CD3DSettingsDlg::AddBackBufferFormat( D3DFORMAT format )
{
    CDXUTComboBox* pComboBox = m_Dialog.GetComboBox( DXUTSETTINGSDLG_BACK_BUFFER_FORMAT );
    
    if( !pComboBox->ContainsItem( DXUTD3DFormatToString(format, TRUE) ) )
        pComboBox->AddItem( DXUTD3DFormatToString(format, TRUE), ULongToPtr( format ) );
}


//-------------------------------------------------------------------------------------
D3DFORMAT CD3DSettingsDlg::GetSelectedBackBufferFormat()
{
    CDXUTComboBox* pComboBox = m_Dialog.GetComboBox( DXUTSETTINGSDLG_BACK_BUFFER_FORMAT );
    
    return (D3DFORMAT) PtrToUlong( pComboBox->GetSelectedData() ); 
}


//-------------------------------------------------------------------------------------
void CD3DSettingsDlg::AddDepthStencilBufferFormat( D3DFORMAT format )
{
    CDXUTComboBox* pComboBox = m_Dialog.GetComboBox( DXUTSETTINGSDLG_DEPTH_STENCIL );
    
    if( !pComboBox->ContainsItem( DXUTD3DFormatToString(format, TRUE) ) )
        pComboBox->AddItem( DXUTD3DFormatToString(format, TRUE), ULongToPtr(format) );
}


//-------------------------------------------------------------------------------------
D3DFORMAT CD3DSettingsDlg::GetSelectedDepthStencilBufferFormat()
{
    CDXUTComboBox* pComboBox = m_Dialog.GetComboBox( DXUTSETTINGSDLG_DEPTH_STENCIL );
    
    return (D3DFORMAT) PtrToUlong( pComboBox->GetSelectedData() ); 
}


//-------------------------------------------------------------------------------------
void CD3DSettingsDlg::AddMultisampleType( D3DMULTISAMPLE_TYPE type )
{
    CDXUTComboBox* pComboBox = m_Dialog.GetComboBox( DXUTSETTINGSDLG_MULTISAMPLE_TYPE );
    
    if( !pComboBox->ContainsItem( DXUTMultisampleTypeToString(type) ) )
        pComboBox->AddItem( DXUTMultisampleTypeToString(type), ULongToPtr(type) );
}


//-------------------------------------------------------------------------------------
D3DMULTISAMPLE_TYPE CD3DSettingsDlg::GetSelectedMultisampleType()
{
    CDXUTComboBox* pComboBox = m_Dialog.GetComboBox( DXUTSETTINGSDLG_MULTISAMPLE_TYPE );
    
    return (D3DMULTISAMPLE_TYPE) PtrToUlong( pComboBox->GetSelectedData() ); 
}


//-------------------------------------------------------------------------------------
void CD3DSettingsDlg::AddMultisampleQuality( DWORD dwQuality )
{
    CDXUTComboBox* pComboBox = m_Dialog.GetComboBox( DXUTSETTINGSDLG_MULTISAMPLE_QUALITY );
        
    WCHAR strQuality[50];
    StringCchPrintf( strQuality, 50, L"%d", dwQuality );

    if( !pComboBox->ContainsItem( strQuality ) )
        pComboBox->AddItem( strQuality, ULongToPtr(dwQuality) );
}


//-------------------------------------------------------------------------------------
DWORD CD3DSettingsDlg::GetSelectedMultisampleQuality()
{
    CDXUTComboBox* pComboBox = m_Dialog.GetComboBox( DXUTSETTINGSDLG_MULTISAMPLE_QUALITY );
    
    return PtrToUlong( pComboBox->GetSelectedData() ); 
}


//-------------------------------------------------------------------------------------
void CD3DSettingsDlg::AddVertexProcessingType( DWORD dwType )
{
    CDXUTComboBox* pComboBox = m_Dialog.GetComboBox( DXUTSETTINGSDLG_VERTEX_PROCESSING );
    
    if( !pComboBox->ContainsItem( DXUTVertexProcessingTypeToString(dwType) ) )
        pComboBox->AddItem( DXUTVertexProcessingTypeToString(dwType), ULongToPtr(dwType) );
}


//-------------------------------------------------------------------------------------
DWORD CD3DSettingsDlg::GetSelectedVertexProcessingType()
{
    CDXUTComboBox* pComboBox = m_Dialog.GetComboBox( DXUTSETTINGSDLG_VERTEX_PROCESSING );
    
    return PtrToUlong( pComboBox->GetSelectedData() ); 
}


//-------------------------------------------------------------------------------------
DWORD CD3DSettingsDlg::GetSelectedPresentInterval()
{
    CDXUTComboBox* pComboBox = m_Dialog.GetComboBox( DXUTSETTINGSDLG_PRESENT_INTERVAL );
    
    return PtrToUlong( pComboBox->GetSelectedData() ); 
}


//-------------------------------------------------------------------------------------
void CD3DSettingsDlg::SetDeviceClip( bool bDeviceClip )
{
    CDXUTCheckBox* pCheckBox = m_Dialog.GetCheckBox( DXUTSETTINGSDLG_DEVICECLIP );
    pCheckBox->SetChecked( bDeviceClip );
}


//-------------------------------------------------------------------------------------
bool CD3DSettingsDlg::IsDeviceClip()
{
    CDXUTCheckBox* pCheckBox = m_Dialog.GetCheckBox( DXUTSETTINGSDLG_DEVICECLIP );
    return pCheckBox->GetChecked();
}


//--------------------------------------------------------------------------------------
// Returns the string for the given D3DDEVTYPE.
//--------------------------------------------------------------------------------------
WCHAR* DXUTD3DDeviceTypeToString(D3DDEVTYPE devType)
{
    switch (devType)
    {
        case D3DDEVTYPE_HAL:        return L"D3DDEVTYPE_HAL";
        case D3DDEVTYPE_SW:         return L"D3DDEVTYPE_SW";
        case D3DDEVTYPE_REF:        return L"D3DDEVTYPE_REF";
        default:                    return L"Unknown devType";
    }
}


//--------------------------------------------------------------------------------------
// Returns the string for the given D3DMULTISAMPLE_TYPE.
//--------------------------------------------------------------------------------------
WCHAR* DXUTMultisampleTypeToString(D3DMULTISAMPLE_TYPE MultiSampleType)
{
    switch (MultiSampleType)
    {
    case D3DMULTISAMPLE_NONE:       return L"D3DMULTISAMPLE_NONE";
    case D3DMULTISAMPLE_NONMASKABLE: return L"D3DMULTISAMPLE_NONMASKABLE";
    case D3DMULTISAMPLE_2_SAMPLES:  return L"D3DMULTISAMPLE_2_SAMPLES";
    case D3DMULTISAMPLE_3_SAMPLES:  return L"D3DMULTISAMPLE_3_SAMPLES";
    case D3DMULTISAMPLE_4_SAMPLES:  return L"D3DMULTISAMPLE_4_SAMPLES";
    case D3DMULTISAMPLE_5_SAMPLES:  return L"D3DMULTISAMPLE_5_SAMPLES";
    case D3DMULTISAMPLE_6_SAMPLES:  return L"D3DMULTISAMPLE_6_SAMPLES";
    case D3DMULTISAMPLE_7_SAMPLES:  return L"D3DMULTISAMPLE_7_SAMPLES";
    case D3DMULTISAMPLE_8_SAMPLES:  return L"D3DMULTISAMPLE_8_SAMPLES";
    case D3DMULTISAMPLE_9_SAMPLES:  return L"D3DMULTISAMPLE_9_SAMPLES";
    case D3DMULTISAMPLE_10_SAMPLES: return L"D3DMULTISAMPLE_10_SAMPLES";
    case D3DMULTISAMPLE_11_SAMPLES: return L"D3DMULTISAMPLE_11_SAMPLES";
    case D3DMULTISAMPLE_12_SAMPLES: return L"D3DMULTISAMPLE_12_SAMPLES";
    case D3DMULTISAMPLE_13_SAMPLES: return L"D3DMULTISAMPLE_13_SAMPLES";
    case D3DMULTISAMPLE_14_SAMPLES: return L"D3DMULTISAMPLE_14_SAMPLES";
    case D3DMULTISAMPLE_15_SAMPLES: return L"D3DMULTISAMPLE_15_SAMPLES";
    case D3DMULTISAMPLE_16_SAMPLES: return L"D3DMULTISAMPLE_16_SAMPLES";
    default:                        return L"Unknown Multisample Type";
    }
}


//--------------------------------------------------------------------------------------
// Returns the string for the given vertex processing type
//--------------------------------------------------------------------------------------
WCHAR* DXUTVertexProcessingTypeToString(DWORD vpt)
{
    switch (vpt)
    {
    case D3DCREATE_SOFTWARE_VERTEXPROCESSING: return L"Software vertex processing";
    case D3DCREATE_MIXED_VERTEXPROCESSING:    return L"Mixed vertex processing";
    case D3DCREATE_HARDWARE_VERTEXPROCESSING: return L"Hardware vertex processing";
    case D3DCREATE_PUREDEVICE:                return L"Pure hardware vertex processing";
    default:                                  return L"Unknown vertex processing type";
    }
}


//--------------------------------------------------------------------------------------
// Returns the string for the given present interval.
//--------------------------------------------------------------------------------------
WCHAR* DXUTPresentIntervalToString( UINT pi )
{
    switch( pi )
    {
    case D3DPRESENT_INTERVAL_IMMEDIATE: return L"D3DPRESENT_INTERVAL_IMMEDIATE";
    case D3DPRESENT_INTERVAL_DEFAULT:   return L"D3DPRESENT_INTERVAL_DEFAULT";
    case D3DPRESENT_INTERVAL_ONE:       return L"D3DPRESENT_INTERVAL_ONE";
    case D3DPRESENT_INTERVAL_TWO:       return L"D3DPRESENT_INTERVAL_TWO";
    case D3DPRESENT_INTERVAL_THREE:     return L"D3DPRESENT_INTERVAL_THREE";
    case D3DPRESENT_INTERVAL_FOUR:      return L"D3DPRESENT_INTERVAL_FOUR";
    default:                            return L"Unknown PresentInterval";
    }
}


