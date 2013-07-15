//--------------------------------------------------------------------------------------
// File: DXUTEnum.cpp
//
// Enumerates D3D adapters, devices, modes, etc.
//
// Copyright (c) Microsoft Corporation. All rights reserved.
//--------------------------------------------------------------------------------------
#include "stdafx.h"
#undef min // use __min instead
#undef max // use __max instead


//--------------------------------------------------------------------------------------
// Forward declarations
//--------------------------------------------------------------------------------------
static int __cdecl SortModesCallback( const void* arg1, const void* arg2 );
UINT DXUTStencilBits( D3DFORMAT fmt );
UINT DXUTDepthBits( D3DFORMAT fmt );
UINT DXUTAlphaChannelBits( D3DFORMAT fmt );
UINT DXUTColorChannelBits( D3DFORMAT fmt );
CD3DEnumeration* DXUTGetEnumeration()
{
    // Using an accessor function gives control of the construction order
    static CD3DEnumeration d3denum;
    return &d3denum;
}


//--------------------------------------------------------------------------------------
CD3DEnumeration::CD3DEnumeration()
{
    m_pD3D = NULL;
    m_IsDeviceAcceptableFunc = NULL;
    m_pIsDeviceAcceptableFuncUserContext = NULL;
    m_bRequirePostPixelShaderBlending = true;

    m_nMinWidth = 640;
    m_nMinHeight = 480;
    m_nMaxWidth = UINT_MAX;
    m_nMaxHeight = UINT_MAX;

    m_nRefreshMin = 0;
    m_nRefreshMax = UINT_MAX;

    m_nMultisampleQualityMax = 0xFFFF;

    ResetPossibleDepthStencilFormats();
    ResetPossibleMultisampleTypeList();                                   
    ResetPossiblePresentIntervalList();
    SetPossibleVertexProcessingList( true, true, true, false );
}


//--------------------------------------------------------------------------------------
CD3DEnumeration::~CD3DEnumeration()
{
    ClearAdapterInfoList();
}



//--------------------------------------------------------------------------------------
// Enumerates available D3D adapters, devices, modes, etc.
//--------------------------------------------------------------------------------------
HRESULT CD3DEnumeration::Enumerate( IDirect3D9* pD3D,
                                    LPDXUTCALLBACKISDEVICEACCEPTABLE IsDeviceAcceptableFunc,
                                    void* pIsDeviceAcceptableFuncUserContext )
{
    if( pD3D == NULL )
    {
        pD3D = DXUTGetD3DObject();
        if( pD3D == NULL )
            return DXUTERR_NODIRECT3D;
    }

    m_pD3D = pD3D;
    m_IsDeviceAcceptableFunc = IsDeviceAcceptableFunc;
    m_pIsDeviceAcceptableFuncUserContext = pIsDeviceAcceptableFuncUserContext;

    HRESULT hr;
    ClearAdapterInfoList();
    CGrowableArray<D3DFORMAT> adapterFormatList;

    const D3DFORMAT allowedAdapterFormatArray[] = 
    {   
        D3DFMT_X8R8G8B8, 
        D3DFMT_X1R5G5B5, 
        D3DFMT_R5G6B5, 
        D3DFMT_A2R10G10B10
    };
    const UINT allowedAdapterFormatArrayCount  = sizeof(allowedAdapterFormatArray) / sizeof(allowedAdapterFormatArray[0]);

    UINT numAdapters = pD3D->GetAdapterCount();
    for (UINT adapterOrdinal = 0; adapterOrdinal < numAdapters; adapterOrdinal++)
    {
        CD3DEnumAdapterInfo* pAdapterInfo = new CD3DEnumAdapterInfo;
        if( pAdapterInfo == NULL )
            return E_OUTOFMEMORY;

        pAdapterInfo->AdapterOrdinal = adapterOrdinal;
        pD3D->GetAdapterIdentifier(adapterOrdinal, 0, &pAdapterInfo->AdapterIdentifier);

        // Get list of all display modes on this adapter.  
        // Also build a temporary list of all display adapter formats.
        adapterFormatList.RemoveAll();

        for( UINT iFormatList = 0; iFormatList < allowedAdapterFormatArrayCount; iFormatList++ )
        {
            D3DFORMAT allowedAdapterFormat = allowedAdapterFormatArray[iFormatList];
            UINT numAdapterModes = pD3D->GetAdapterModeCount( adapterOrdinal, allowedAdapterFormat );
            for (UINT mode = 0; mode < numAdapterModes; mode++)
            {
                D3DDISPLAYMODE displayMode;
                pD3D->EnumAdapterModes( adapterOrdinal, allowedAdapterFormat, mode, &displayMode );

                if( displayMode.Width < m_nMinWidth ||
                    displayMode.Height < m_nMinHeight || 
                    displayMode.Width > m_nMaxWidth ||
                    displayMode.Height > m_nMaxHeight || 
                    displayMode.RefreshRate < m_nRefreshMin ||
                    displayMode.RefreshRate > m_nRefreshMax )
                {
                    continue;
                }

                pAdapterInfo->displayModeList.Add( displayMode );
                
                if( !adapterFormatList.Contains(displayMode.Format) )
                    adapterFormatList.Add( displayMode.Format );
            }

        }

        D3DDISPLAYMODE displayMode;
        pD3D->GetAdapterDisplayMode( adapterOrdinal, &displayMode );
        if( !adapterFormatList.Contains(displayMode.Format) )
            adapterFormatList.Add( displayMode.Format );

        // Sort displaymode list
        ::qsort( pAdapterInfo->displayModeList.GetData(), 
               pAdapterInfo->displayModeList.GetSize(), sizeof( D3DDISPLAYMODE ),
               SortModesCallback );

        // Get info for each device on this adapter
        if( FAILED( EnumerateDevices( pAdapterInfo, &adapterFormatList ) ) )
        {
            delete pAdapterInfo;
            continue;
        }

        // If at least one device on this adapter is available and compatible
        // with the app, add the adapterInfo to the list
        if( pAdapterInfo->deviceInfoList.GetSize() > 0 )
        {
            hr = m_AdapterInfoList.Add( pAdapterInfo );
            if( FAILED(hr) )
                return hr;
        } else
            delete pAdapterInfo;
    }

    bool bUniqueDesc = true;
    CD3DEnumAdapterInfo* pAdapterInfo;
    for( int i=0; i<m_AdapterInfoList.GetSize(); i++ )
    {
        CD3DEnumAdapterInfo* pAdapterInfo1 = m_AdapterInfoList.GetAt(i);

        for( int j=i+1; j<m_AdapterInfoList.GetSize(); j++ )
        {
            CD3DEnumAdapterInfo* pAdapterInfo2 = m_AdapterInfoList.GetAt(j);
            if( _stricmp( pAdapterInfo1->AdapterIdentifier.Description, 
                          pAdapterInfo2->AdapterIdentifier.Description ) == 0 )
            {
                bUniqueDesc = false;
                break;
            }
        }

        if( !bUniqueDesc )
            break;
    }

    for( int i=0; i<m_AdapterInfoList.GetSize(); i++ )
    {
        pAdapterInfo = m_AdapterInfoList.GetAt(i);

        MultiByteToWideChar( CP_ACP, 0, 
                             pAdapterInfo->AdapterIdentifier.Description, -1, 
                             pAdapterInfo->szUniqueDescription, 100 );
        pAdapterInfo->szUniqueDescription[100] = 0;

        if( !bUniqueDesc )
        {
            WCHAR sz[100];
            StringCchPrintf( sz, 100, L" (#%d)", pAdapterInfo->AdapterOrdinal );
            StringCchCat( pAdapterInfo->szUniqueDescription, 256, sz );

        }
    }

    return S_OK;
}



//--------------------------------------------------------------------------------------
// Enumerates D3D devices for a particular adapter.
//--------------------------------------------------------------------------------------
HRESULT CD3DEnumeration::EnumerateDevices( CD3DEnumAdapterInfo* pAdapterInfo, CGrowableArray<D3DFORMAT>* pAdapterFormatList )
{
    HRESULT hr;

    const D3DDEVTYPE devTypeArray[] = 
    { 
        D3DDEVTYPE_HAL, 
        D3DDEVTYPE_SW, 
        D3DDEVTYPE_REF 
    };
    const UINT devTypeArrayCount = sizeof(devTypeArray) / sizeof(devTypeArray[0]);

    // Enumerate each Direct3D device type
    for( UINT iDeviceType = 0; iDeviceType < devTypeArrayCount; iDeviceType++ )
    {
        CD3DEnumDeviceInfo* pDeviceInfo = new CD3DEnumDeviceInfo;
        if( pDeviceInfo == NULL )
            return E_OUTOFMEMORY;

        // Fill struct w/ AdapterOrdinal and D3DDEVTYPE
        pDeviceInfo->DeviceType = devTypeArray[iDeviceType];

        // Store device caps
        if( FAILED( hr = m_pD3D->GetDeviceCaps( pAdapterInfo->AdapterOrdinal, pDeviceInfo->DeviceType, 
                                                &pDeviceInfo->Caps ) ) )
        {
            delete pDeviceInfo;
            continue;
        }

        // Create a temp device to verify that it is really possible to create a REF device 
        // [the developer DirectX redist has to be installed]
        D3DDISPLAYMODE Mode;
        m_pD3D->GetAdapterDisplayMode(0, &Mode);
        D3DPRESENT_PARAMETERS pp;
        ZeroMemory( &pp, sizeof(D3DPRESENT_PARAMETERS) );
        pp.BackBufferWidth  = 1;
        pp.BackBufferHeight = 1;
        pp.BackBufferFormat = Mode.Format;
        pp.BackBufferCount  = 1;
        pp.SwapEffect       = D3DSWAPEFFECT_COPY;
        pp.Windowed         = TRUE;
        pp.hDeviceWindow    = DXUTGetHWNDFocus();
        IDirect3DDevice9 *pDevice = NULL;
        if( FAILED( hr = m_pD3D->CreateDevice( pAdapterInfo->AdapterOrdinal, pDeviceInfo->DeviceType, DXUTGetHWNDFocus(),
                                          D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &pDevice ) ) )
        {
            if( hr == D3DERR_NOTAVAILABLE )
            {
                delete pDeviceInfo;
                continue;
            }
        }
        SAFE_RELEASE( pDevice );

        // Get info for each devicecombo on this device
        if( FAILED( hr = EnumerateDeviceCombos( pAdapterInfo, pDeviceInfo, pAdapterFormatList ) ) )
        {
            delete pDeviceInfo;
            continue;
        }

        // If at least one devicecombo for this device is found, 
        // add the deviceInfo to the list
        if (pDeviceInfo->deviceSettingsComboList.GetSize() > 0 )
            pAdapterInfo->deviceInfoList.Add( pDeviceInfo );
        else
            delete pDeviceInfo;
    }

    return S_OK;
}



//--------------------------------------------------------------------------------------
// Enumerates DeviceCombos for a particular device.
//--------------------------------------------------------------------------------------
HRESULT CD3DEnumeration::EnumerateDeviceCombos( CD3DEnumAdapterInfo* pAdapterInfo, CD3DEnumDeviceInfo* pDeviceInfo, CGrowableArray<D3DFORMAT>* pAdapterFormatList )
{
    const D3DFORMAT backBufferFormatArray[] = 
    {   
        D3DFMT_A8R8G8B8, 
        D3DFMT_X8R8G8B8, 
        D3DFMT_A2R10G10B10, 
        D3DFMT_R5G6B5, 
        D3DFMT_A1R5G5B5, 
        D3DFMT_X1R5G5B5 
    };
    const UINT backBufferFormatArrayCount = sizeof(backBufferFormatArray) / sizeof(backBufferFormatArray[0]);

    // See which adapter formats are supported by this device
    for( int iFormat=0; iFormat<pAdapterFormatList->GetSize(); iFormat++ )
    {
        D3DFORMAT adapterFormat = pAdapterFormatList->GetAt(iFormat);

        for( UINT iBackBufferFormat = 0; iBackBufferFormat < backBufferFormatArrayCount; iBackBufferFormat++ )
        {
            D3DFORMAT backBufferFormat = backBufferFormatArray[iBackBufferFormat];

            for( int nWindowed = 0; nWindowed < 2; nWindowed++)
            {
                if( !nWindowed && pAdapterInfo->displayModeList.GetSize() == 0 )
                    continue;

                if (FAILED( m_pD3D->CheckDeviceType( pAdapterInfo->AdapterOrdinal, pDeviceInfo->DeviceType, 
                                                     adapterFormat, backBufferFormat, nWindowed )))
                {
                    continue;
                }

                if( m_bRequirePostPixelShaderBlending )
                {
                    // If the backbuffer format doesn't support D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING
                    // then alpha test, pixel fog, render-target blending, color write enable, and dithering. 
                    // are not supported.
                    if( FAILED( m_pD3D->CheckDeviceFormat( pAdapterInfo->AdapterOrdinal, pDeviceInfo->DeviceType,
                                                           adapterFormat, D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING, 
                                                           D3DRTYPE_TEXTURE, backBufferFormat ) ) )
                    {
                        continue;
                    }
                }

                // If an application callback function has been provided, make sure this device
                // is acceptable to the app.
                if( m_IsDeviceAcceptableFunc != NULL )
                {
                    if( !m_IsDeviceAcceptableFunc( &pDeviceInfo->Caps, adapterFormat, backBufferFormat, FALSE != nWindowed, m_pIsDeviceAcceptableFuncUserContext ) )
                        continue;
                }
                
                // At this point, we have an adapter/device/adapterformat/backbufferformat/iswindowed
                // DeviceCombo that is supported by the system and acceptable to the app. We still 
                // need to find one or more suitable depth/stencil buffer format,
                // multisample type, and present interval.
                CD3DEnumDeviceSettingsCombo* pDeviceCombo = new CD3DEnumDeviceSettingsCombo;
                if( pDeviceCombo == NULL )
                    return E_OUTOFMEMORY;

                pDeviceCombo->AdapterOrdinal = pAdapterInfo->AdapterOrdinal;
                pDeviceCombo->DeviceType = pDeviceInfo->DeviceType;
                pDeviceCombo->AdapterFormat = adapterFormat;
                pDeviceCombo->BackBufferFormat = backBufferFormat;
                pDeviceCombo->Windowed = (nWindowed != 0);
               
                BuildDepthStencilFormatList( pDeviceCombo );
                BuildMultiSampleTypeList( pDeviceCombo );
                if (pDeviceCombo->multiSampleTypeList.GetSize() == 0)
                {
                    delete pDeviceCombo;
                    continue;
                }
                BuildDSMSConflictList( pDeviceCombo );
                BuildPresentIntervalList(pDeviceInfo, pDeviceCombo );
                pDeviceCombo->pAdapterInfo = pAdapterInfo;
                pDeviceCombo->pDeviceInfo = pDeviceInfo;

                pDeviceInfo->deviceSettingsComboList.Add( pDeviceCombo );
            
            }
        }
    }

    return S_OK;
}



//--------------------------------------------------------------------------------------
// Adds all depth/stencil formats that are compatible with the device 
//       and app to the given D3DDeviceCombo.
//--------------------------------------------------------------------------------------
void CD3DEnumeration::BuildDepthStencilFormatList( CD3DEnumDeviceSettingsCombo* pDeviceCombo )
{
    D3DFORMAT depthStencilFmt;
    for( int idsf = 0; idsf < m_DepthStecilPossibleList.GetSize(); idsf++ )
    {
        depthStencilFmt = m_DepthStecilPossibleList.GetAt(idsf);
        if (SUCCEEDED(m_pD3D->CheckDeviceFormat(pDeviceCombo->AdapterOrdinal, 
                pDeviceCombo->DeviceType, pDeviceCombo->AdapterFormat, 
                D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_SURFACE, depthStencilFmt)))
        {
            if (SUCCEEDED(m_pD3D->CheckDepthStencilMatch(pDeviceCombo->AdapterOrdinal, 
                    pDeviceCombo->DeviceType, pDeviceCombo->AdapterFormat, 
                    pDeviceCombo->BackBufferFormat, depthStencilFmt)))
            {
                pDeviceCombo->depthStencilFormatList.Add( depthStencilFmt );
            }
        }
    }
}




//--------------------------------------------------------------------------------------
// Adds all multisample types that are compatible with the device and app to
//       the given D3DDeviceCombo.
//--------------------------------------------------------------------------------------
void CD3DEnumeration::BuildMultiSampleTypeList( CD3DEnumDeviceSettingsCombo* pDeviceCombo )
{
    D3DMULTISAMPLE_TYPE msType;
    DWORD msQuality;
    for( int imst = 0; imst < m_MultiSampleTypeList.GetSize(); imst++ )
    {
        msType = m_MultiSampleTypeList.GetAt(imst);
        if( SUCCEEDED( m_pD3D->CheckDeviceMultiSampleType( pDeviceCombo->AdapterOrdinal, 
                pDeviceCombo->DeviceType, pDeviceCombo->BackBufferFormat, 
                pDeviceCombo->Windowed, msType, &msQuality ) ) )
        {
            pDeviceCombo->multiSampleTypeList.Add( msType );
            if( msQuality > m_nMultisampleQualityMax+1 )
                msQuality = m_nMultisampleQualityMax+1;
            pDeviceCombo->multiSampleQualityList.Add( msQuality );
        }
    }
}




//--------------------------------------------------------------------------------------
// Find any conflicts between the available depth/stencil formats and
//       multisample types.
//--------------------------------------------------------------------------------------
void CD3DEnumeration::BuildDSMSConflictList( CD3DEnumDeviceSettingsCombo* pDeviceCombo )
{
    CD3DEnumDSMSConflict DSMSConflict;

    for( int iDS=0; iDS<pDeviceCombo->depthStencilFormatList.GetSize(); iDS++ )
    {
        D3DFORMAT dsFmt = pDeviceCombo->depthStencilFormatList.GetAt(iDS);

        for( int iMS=0; iMS<pDeviceCombo->multiSampleTypeList.GetSize(); iMS++ )
        {
            D3DMULTISAMPLE_TYPE msType = pDeviceCombo->multiSampleTypeList.GetAt(iMS);

            if( FAILED( m_pD3D->CheckDeviceMultiSampleType( pDeviceCombo->AdapterOrdinal, pDeviceCombo->DeviceType,
                                                            dsFmt, pDeviceCombo->Windowed, msType, NULL ) ) )
            {
                DSMSConflict.DSFormat = dsFmt;
                DSMSConflict.MSType = msType;
                pDeviceCombo->DSMSConflictList.Add( DSMSConflict );
            }
        }
    }
}



//--------------------------------------------------------------------------------------
// Adds all present intervals that are compatible with the device and app 
//       to the given D3DDeviceCombo.
//--------------------------------------------------------------------------------------
void CD3DEnumeration::BuildPresentIntervalList( CD3DEnumDeviceInfo* pDeviceInfo, 
                                                CD3DEnumDeviceSettingsCombo* pDeviceCombo )
{
    UINT pi;
    for( int ipi = 0; ipi < m_PresentIntervalList.GetSize(); ipi++ )
    {
        pi = m_PresentIntervalList.GetAt(ipi);
        if( pDeviceCombo->Windowed )
        {
            if( pi == D3DPRESENT_INTERVAL_TWO ||
                pi == D3DPRESENT_INTERVAL_THREE ||
                pi == D3DPRESENT_INTERVAL_FOUR )
            {
                // These intervals are not supported in windowed mode.
                continue;
            }
        }
        // Note that D3DPRESENT_INTERVAL_DEFAULT is zero, so you
        // can't do a caps check for it -- it is always available.
        if( pi == D3DPRESENT_INTERVAL_DEFAULT ||
            (pDeviceInfo->Caps.PresentationIntervals & pi) )
        {
            pDeviceCombo->presentIntervalList.Add( pi );
        }
    }
}



//--------------------------------------------------------------------------------------
// Release all the allocated CD3DEnumAdapterInfo objects and empty the list
//--------------------------------------------------------------------------------------
void CD3DEnumeration::ClearAdapterInfoList()
{
    CD3DEnumAdapterInfo* pAdapterInfo;
    for( int i=0; i<m_AdapterInfoList.GetSize(); i++ )
    {
        pAdapterInfo = m_AdapterInfoList.GetAt(i);
        delete pAdapterInfo;
    }

    m_AdapterInfoList.RemoveAll();
}



//--------------------------------------------------------------------------------------
// Call GetAdapterInfoList() after Enumerate() to get a STL vector of 
//       CD3DEnumAdapterInfo* 
//--------------------------------------------------------------------------------------
CGrowableArray<CD3DEnumAdapterInfo*>* CD3DEnumeration::GetAdapterInfoList()
{
    return &m_AdapterInfoList;
}



//--------------------------------------------------------------------------------------
CD3DEnumAdapterInfo* CD3DEnumeration::GetAdapterInfo( UINT AdapterOrdinal )
{
    for( int iAdapter=0; iAdapter<m_AdapterInfoList.GetSize(); iAdapter++ )
    {
        CD3DEnumAdapterInfo* pAdapterInfo = m_AdapterInfoList.GetAt(iAdapter);
        if( pAdapterInfo->AdapterOrdinal == AdapterOrdinal )
            return pAdapterInfo;
    }

    return NULL;
}


//--------------------------------------------------------------------------------------
CD3DEnumDeviceInfo* CD3DEnumeration::GetDeviceInfo( UINT AdapterOrdinal, D3DDEVTYPE DeviceType )
{
    CD3DEnumAdapterInfo* pAdapterInfo = GetAdapterInfo( AdapterOrdinal );
    if( pAdapterInfo )
    {
        for( int iDeviceInfo=0; iDeviceInfo<pAdapterInfo->deviceInfoList.GetSize(); iDeviceInfo++ )
        {
            CD3DEnumDeviceInfo* pDeviceInfo = pAdapterInfo->deviceInfoList.GetAt(iDeviceInfo);
            if( pDeviceInfo->DeviceType == DeviceType )
                return pDeviceInfo;
        }
    }

    return NULL;
}


//--------------------------------------------------------------------------------------
// 
//--------------------------------------------------------------------------------------
CD3DEnumDeviceSettingsCombo* CD3DEnumeration::GetDeviceSettingsCombo( UINT AdapterOrdinal, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat, D3DFORMAT BackBufferFormat, BOOL bWindowed )
{
    CD3DEnumDeviceInfo* pDeviceInfo = GetDeviceInfo( AdapterOrdinal, DeviceType );
    if( pDeviceInfo )
    {
        for( int iDeviceCombo=0; iDeviceCombo<pDeviceInfo->deviceSettingsComboList.GetSize(); iDeviceCombo++ )
        {
            CD3DEnumDeviceSettingsCombo* pDeviceSettingsCombo = pDeviceInfo->deviceSettingsComboList.GetAt(iDeviceCombo);
            if( pDeviceSettingsCombo->AdapterFormat == AdapterFormat &&
                pDeviceSettingsCombo->BackBufferFormat == BackBufferFormat &&
                pDeviceSettingsCombo->Windowed == bWindowed )
                return pDeviceSettingsCombo;
        }
    }

    return NULL;
}


//--------------------------------------------------------------------------------------
// Returns the number of color channel bits in the specified D3DFORMAT
//--------------------------------------------------------------------------------------
UINT DXUTColorChannelBits( D3DFORMAT fmt )
{
    switch( fmt )
    {
        case D3DFMT_R8G8B8:
            return 8;
        case D3DFMT_A8R8G8B8:
            return 8;
        case D3DFMT_X8R8G8B8:
            return 8;
        case D3DFMT_R5G6B5:
            return 5;
        case D3DFMT_X1R5G5B5:
            return 5;
        case D3DFMT_A1R5G5B5:
            return 5;
        case D3DFMT_A4R4G4B4:
            return 4;
        case D3DFMT_R3G3B2:
            return 2;
        case D3DFMT_A8R3G3B2:
            return 2;
        case D3DFMT_X4R4G4B4:
            return 4;
        case D3DFMT_A2B10G10R10:
            return 10;
        case D3DFMT_A8B8G8R8:
            return 8;
        case D3DFMT_A2R10G10B10:
            return 10;
        case D3DFMT_A16B16G16R16:
            return 16;
        default:
            return 0;
    }
}




//--------------------------------------------------------------------------------------
// Returns the number of alpha channel bits in the specified D3DFORMAT
//--------------------------------------------------------------------------------------
UINT DXUTAlphaChannelBits( D3DFORMAT fmt )
{
    switch( fmt )
    {
        case D3DFMT_R8G8B8:
            return 0;
        case D3DFMT_A8R8G8B8:
            return 8;
        case D3DFMT_X8R8G8B8:
            return 0;
        case D3DFMT_R5G6B5:
            return 0;
        case D3DFMT_X1R5G5B5:
            return 0;
        case D3DFMT_A1R5G5B5:
            return 1;
        case D3DFMT_A4R4G4B4:
            return 4;
        case D3DFMT_R3G3B2:
            return 0;
        case D3DFMT_A8R3G3B2:
            return 8;
        case D3DFMT_X4R4G4B4:
            return 0;
        case D3DFMT_A2B10G10R10:
            return 2;
        case D3DFMT_A8B8G8R8:
            return 8;
        case D3DFMT_A2R10G10B10:
            return 2;
        case D3DFMT_A16B16G16R16:
            return 16;
        default:
            return 0;
    }
}




//--------------------------------------------------------------------------------------
// Returns the number of depth bits in the specified D3DFORMAT
//--------------------------------------------------------------------------------------
UINT DXUTDepthBits( D3DFORMAT fmt )
{
    switch( fmt )
    {
        case D3DFMT_D32F_LOCKABLE:
        case D3DFMT_D32:
            return 32;

        case D3DFMT_D24X8:
        case D3DFMT_D24S8:
        case D3DFMT_D24X4S4:
        case D3DFMT_D24FS8:
            return 24;

        case D3DFMT_D16_LOCKABLE:
        case D3DFMT_D16:
            return 16;

        case D3DFMT_D15S1:
            return 15;

        default:
            return 0;
    }
}




//--------------------------------------------------------------------------------------
// Returns the number of stencil bits in the specified D3DFORMAT
//--------------------------------------------------------------------------------------
UINT DXUTStencilBits( D3DFORMAT fmt )
{
    switch( fmt )
    {
        case D3DFMT_D16_LOCKABLE:
        case D3DFMT_D16:
        case D3DFMT_D32F_LOCKABLE:
        case D3DFMT_D32:
        case D3DFMT_D24X8:
            return 0;

        case D3DFMT_D15S1:
            return 1;

        case D3DFMT_D24X4S4:
            return 4;

        case D3DFMT_D24S8:
        case D3DFMT_D24FS8:
            return 8;

        default:
            return 0;
    }
}



//--------------------------------------------------------------------------------------
// Used to sort D3DDISPLAYMODEs
//--------------------------------------------------------------------------------------
static int __cdecl SortModesCallback( const void* arg1, const void* arg2 )
{
    D3DDISPLAYMODE* pdm1 = (D3DDISPLAYMODE*)arg1;
    D3DDISPLAYMODE* pdm2 = (D3DDISPLAYMODE*)arg2;

    if (pdm1->Width > pdm2->Width)
        return 1;
    if (pdm1->Width < pdm2->Width)
        return -1;
    if (pdm1->Height > pdm2->Height)
        return 1;
    if (pdm1->Height < pdm2->Height)
        return -1;
    if (pdm1->Format > pdm2->Format)
        return 1;
    if (pdm1->Format < pdm2->Format)
        return -1;
    if (pdm1->RefreshRate > pdm2->RefreshRate)
        return 1;
    if (pdm1->RefreshRate < pdm2->RefreshRate)
        return -1;
    return 0;
}



//--------------------------------------------------------------------------------------
CD3DEnumAdapterInfo::~CD3DEnumAdapterInfo( void )
{
    CD3DEnumDeviceInfo* pDeviceInfo;
    for( int i=0; i<deviceInfoList.GetSize(); i++ )
    {
        pDeviceInfo = deviceInfoList.GetAt(i);
        delete pDeviceInfo;
    }
    deviceInfoList.RemoveAll();
}




//--------------------------------------------------------------------------------------
CD3DEnumDeviceInfo::~CD3DEnumDeviceInfo( void )
{
    CD3DEnumDeviceSettingsCombo* pDeviceCombo;
    for( int i=0; i<deviceSettingsComboList.GetSize(); i++ )
    {
        pDeviceCombo = deviceSettingsComboList.GetAt(i);
        delete pDeviceCombo;
    }
    deviceSettingsComboList.RemoveAll();
}


//--------------------------------------------------------------------------------------
void CD3DEnumeration::ResetPossibleDepthStencilFormats()
{
    m_DepthStecilPossibleList.RemoveAll();
    m_DepthStecilPossibleList.Add( D3DFMT_D16 );
    m_DepthStecilPossibleList.Add( D3DFMT_D15S1 );
    m_DepthStecilPossibleList.Add( D3DFMT_D24X8 );
    m_DepthStecilPossibleList.Add( D3DFMT_D24S8 );
    m_DepthStecilPossibleList.Add( D3DFMT_D24X4S4 );
    m_DepthStecilPossibleList.Add( D3DFMT_D32 );
}


//--------------------------------------------------------------------------------------
CGrowableArray<D3DFORMAT>* CD3DEnumeration::GetPossibleDepthStencilFormatList() 
{
    return &m_DepthStecilPossibleList;
}


//--------------------------------------------------------------------------------------
CGrowableArray<D3DMULTISAMPLE_TYPE>* CD3DEnumeration::GetPossibleMultisampleTypeList()
{
    return &m_MultiSampleTypeList;
}


//--------------------------------------------------------------------------------------
void CD3DEnumeration::ResetPossibleMultisampleTypeList()
{
    m_MultiSampleTypeList.RemoveAll();
    m_MultiSampleTypeList.Add( D3DMULTISAMPLE_NONE );
    m_MultiSampleTypeList.Add( D3DMULTISAMPLE_NONMASKABLE );
    m_MultiSampleTypeList.Add( D3DMULTISAMPLE_2_SAMPLES );
    m_MultiSampleTypeList.Add( D3DMULTISAMPLE_3_SAMPLES );
    m_MultiSampleTypeList.Add( D3DMULTISAMPLE_4_SAMPLES );
    m_MultiSampleTypeList.Add( D3DMULTISAMPLE_5_SAMPLES );
    m_MultiSampleTypeList.Add( D3DMULTISAMPLE_6_SAMPLES );
    m_MultiSampleTypeList.Add( D3DMULTISAMPLE_7_SAMPLES );
    m_MultiSampleTypeList.Add( D3DMULTISAMPLE_8_SAMPLES );
    m_MultiSampleTypeList.Add( D3DMULTISAMPLE_9_SAMPLES );
    m_MultiSampleTypeList.Add( D3DMULTISAMPLE_10_SAMPLES );
    m_MultiSampleTypeList.Add( D3DMULTISAMPLE_11_SAMPLES );
    m_MultiSampleTypeList.Add( D3DMULTISAMPLE_12_SAMPLES );
    m_MultiSampleTypeList.Add( D3DMULTISAMPLE_13_SAMPLES );
    m_MultiSampleTypeList.Add( D3DMULTISAMPLE_14_SAMPLES );
    m_MultiSampleTypeList.Add( D3DMULTISAMPLE_15_SAMPLES );
    m_MultiSampleTypeList.Add( D3DMULTISAMPLE_16_SAMPLES );
}


//--------------------------------------------------------------------------------------
void CD3DEnumeration::GetPossibleVertexProcessingList( bool* pbSoftwareVP, bool* pbHardwareVP, bool* pbPureHarewareVP, bool* pbMixedVP )
{
    *pbSoftwareVP = m_bSoftwareVP;
    *pbHardwareVP = m_bHardwareVP;
    *pbPureHarewareVP = m_bPureHarewareVP;
    *pbMixedVP = m_bMixedVP;
}


//--------------------------------------------------------------------------------------
void CD3DEnumeration::SetPossibleVertexProcessingList( bool bSoftwareVP, bool bHardwareVP, bool bPureHarewareVP, bool bMixedVP )
{
    m_bSoftwareVP = bSoftwareVP;
    m_bHardwareVP = bHardwareVP;
    m_bPureHarewareVP = bPureHarewareVP;
    m_bMixedVP = bMixedVP;
}


//--------------------------------------------------------------------------------------
CGrowableArray<UINT>* CD3DEnumeration::GetPossiblePresentIntervalList()
{
    return &m_PresentIntervalList;
}


//--------------------------------------------------------------------------------------
void CD3DEnumeration::ResetPossiblePresentIntervalList()
{
    m_PresentIntervalList.RemoveAll();
    m_PresentIntervalList.Add( D3DPRESENT_INTERVAL_IMMEDIATE );
    m_PresentIntervalList.Add( D3DPRESENT_INTERVAL_DEFAULT );
    m_PresentIntervalList.Add( D3DPRESENT_INTERVAL_ONE );
    m_PresentIntervalList.Add( D3DPRESENT_INTERVAL_TWO );
    m_PresentIntervalList.Add( D3DPRESENT_INTERVAL_THREE );
    m_PresentIntervalList.Add( D3DPRESENT_INTERVAL_FOUR );
}


//--------------------------------------------------------------------------------------
void CD3DEnumeration::SetResolutionMinMax( UINT nMinWidth, UINT nMinHeight, 
                                           UINT nMaxWidth, UINT nMaxHeight )
{
    m_nMinWidth = nMinWidth;
    m_nMinHeight = nMinHeight;
    m_nMaxWidth = nMaxWidth;
    m_nMaxHeight = nMaxHeight;
}


//--------------------------------------------------------------------------------------
void CD3DEnumeration::SetRefreshMinMax( UINT nMin, UINT nMax )
{
    m_nRefreshMin = nMin;
    m_nRefreshMax = nMax;
}


//--------------------------------------------------------------------------------------
void CD3DEnumeration::SetMultisampleQualityMax( UINT nMax )
{
    if( nMax > 0xFFFF )
        nMax = 0xFFFF;
    m_nMultisampleQualityMax = nMax;
}

