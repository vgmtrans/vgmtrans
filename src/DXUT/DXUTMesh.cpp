//-----------------------------------------------------------------------------
// File: DXUTMesh.cpp
//
// Desc: Support code for loading DirectX .X files.
//
// Copyright (c) Microsoft Corporation. All rights reserved.
//-----------------------------------------------------------------------------
#include "dxstdafx.h"
#include <dxfile.h>
#include <rmxfguid.h>
#include <rmxftmpl.h>
#include "DXUTMesh.h"
#undef min // use __min instead
#undef max // use __max instead




//-----------------------------------------------------------------------------
CDXUTMesh::CDXUTMesh( LPCWSTR strName )
{
    StringCchCopy( m_strName, 512, strName );
    m_pMesh              = NULL;
    m_pMaterials         = NULL;
    m_pTextures          = NULL;
    m_bUseMaterials      = TRUE;
    m_pVB                = NULL;
    m_pIB                = NULL;
    m_pDecl              = NULL;
    m_strMaterials       = NULL;
    m_dwNumMaterials     = 0;
    m_dwNumVertices      = 0;
    m_dwNumFaces         = 0;
    m_dwBytesPerVertex   = 0;
}




//-----------------------------------------------------------------------------
CDXUTMesh::~CDXUTMesh()
{
    Destroy();
}




//-----------------------------------------------------------------------------
HRESULT CDXUTMesh::Create( LPDIRECT3DDEVICE9 pd3dDevice, LPCWSTR strFilename )
{
    WCHAR        strPath[MAX_PATH];
    LPD3DXBUFFER pAdjacencyBuffer = NULL;
    LPD3DXBUFFER pMtrlBuffer = NULL;
    HRESULT      hr;

    // Cleanup previous mesh if any
    Destroy();

    // Find the path for the file, and convert it to ANSI (for the D3DX API)
    DXUTFindDXSDKMediaFileCch( strPath, sizeof(strPath) / sizeof(WCHAR), strFilename );

    // Load the mesh
    if( FAILED( hr = D3DXLoadMeshFromX( strPath, D3DXMESH_MANAGED, pd3dDevice, 
                                        &pAdjacencyBuffer, &pMtrlBuffer, NULL,
                                        &m_dwNumMaterials, &m_pMesh ) ) )
    {
        return hr;
    }

    // Optimize the mesh for performance
    if( FAILED( hr = m_pMesh->OptimizeInplace(
                        D3DXMESHOPT_COMPACT | D3DXMESHOPT_ATTRSORT | D3DXMESHOPT_VERTEXCACHE,
                        (DWORD*)pAdjacencyBuffer->GetBufferPointer(), NULL, NULL, NULL ) ) )
    {
        SAFE_RELEASE( pAdjacencyBuffer );
        SAFE_RELEASE( pMtrlBuffer );
        return hr;
    }

    // Set strPath to the path of the mesh file
    WCHAR *pLastBSlash = wcsrchr( strPath, L'\\' );
    if( pLastBSlash )
        *(pLastBSlash + 1) = L'\0';
    else
        *strPath = L'\0';

    D3DXMATERIAL* d3dxMtrls = (D3DXMATERIAL*)pMtrlBuffer->GetBufferPointer();
    hr = CreateMaterials( strPath, pd3dDevice, d3dxMtrls, m_dwNumMaterials );

    SAFE_RELEASE( pAdjacencyBuffer );
    SAFE_RELEASE( pMtrlBuffer );

    // Extract data from m_pMesh for easy access
    D3DVERTEXELEMENT9 decl[MAX_FVF_DECL_SIZE];
    m_dwNumVertices    = m_pMesh->GetNumVertices();
    m_dwNumFaces       = m_pMesh->GetNumFaces();
    m_dwBytesPerVertex = m_pMesh->GetNumBytesPerVertex();
    m_pMesh->GetIndexBuffer( &m_pIB );
    m_pMesh->GetVertexBuffer( &m_pVB );
    m_pMesh->GetDeclaration( decl );
    pd3dDevice->CreateVertexDeclaration( decl, &m_pDecl );

    return hr;
}


//-----------------------------------------------------------------------------
HRESULT CDXUTMesh::Create( LPDIRECT3DDEVICE9 pd3dDevice,
                           LPD3DXFILEDATA pFileData )
{
    LPD3DXBUFFER pMtrlBuffer = NULL;
    LPD3DXBUFFER pAdjacencyBuffer = NULL;
    HRESULT      hr;

    // Cleanup previous mesh if any
    Destroy();

    // Load the mesh from the DXFILEDATA object
    if( FAILED( hr = D3DXLoadMeshFromXof( pFileData, D3DXMESH_MANAGED, pd3dDevice,
                                          &pAdjacencyBuffer, &pMtrlBuffer, NULL,
                                          &m_dwNumMaterials, &m_pMesh ) ) )
    {
        return hr;
    }

    // Optimize the mesh for performance
    if( FAILED( hr = m_pMesh->OptimizeInplace(
                        D3DXMESHOPT_COMPACT | D3DXMESHOPT_ATTRSORT | D3DXMESHOPT_VERTEXCACHE,
                        (DWORD*)pAdjacencyBuffer->GetBufferPointer(), NULL, NULL, NULL ) ) )
    {
        SAFE_RELEASE( pAdjacencyBuffer );
        SAFE_RELEASE( pMtrlBuffer );
        return hr;
    }

    D3DXMATERIAL* d3dxMtrls = (D3DXMATERIAL*)pMtrlBuffer->GetBufferPointer();
    hr = CreateMaterials( L"", pd3dDevice, d3dxMtrls, m_dwNumMaterials );

    SAFE_RELEASE( pAdjacencyBuffer );
    SAFE_RELEASE( pMtrlBuffer );

    // Extract data from m_pMesh for easy access
    D3DVERTEXELEMENT9 decl[MAX_FVF_DECL_SIZE];
    m_dwNumVertices    = m_pMesh->GetNumVertices();
    m_dwNumFaces       = m_pMesh->GetNumFaces();
    m_dwBytesPerVertex = m_pMesh->GetNumBytesPerVertex();
    m_pMesh->GetIndexBuffer( &m_pIB );
    m_pMesh->GetVertexBuffer( &m_pVB );
    m_pMesh->GetDeclaration( decl );
    pd3dDevice->CreateVertexDeclaration( decl, &m_pDecl );

    return hr;
}


//-----------------------------------------------------------------------------
HRESULT CDXUTMesh::Create( LPDIRECT3DDEVICE9 pd3dDevice, ID3DXMesh* pInMesh, 
                           D3DXMATERIAL* pd3dxMaterials, DWORD dwMaterials )
{
    // Cleanup previous mesh if any
    Destroy();

    // Optimize the mesh for performance
    DWORD *rgdwAdjacency = NULL;
    rgdwAdjacency = new DWORD[pInMesh->GetNumFaces() * 3];
    if( rgdwAdjacency == NULL )
        return E_OUTOFMEMORY;
    pInMesh->GenerateAdjacency(1e-6f,rgdwAdjacency);

    D3DVERTEXELEMENT9 decl[MAX_FVF_DECL_SIZE];
    pInMesh->GetDeclaration( decl );

    DWORD dwOptions = pInMesh->GetOptions();
    dwOptions &= ~(D3DXMESH_32BIT | D3DXMESH_SYSTEMMEM | D3DXMESH_WRITEONLY);
    dwOptions |= D3DXMESH_MANAGED;
    dwOptions |= D3DXMESHOPT_COMPACT | D3DXMESHOPT_ATTRSORT | D3DXMESHOPT_VERTEXCACHE;

    ID3DXMesh* pTempMesh = NULL;
    if( FAILED( pInMesh->Optimize( dwOptions, rgdwAdjacency, NULL, NULL, NULL, &pTempMesh ) ) )
    {
        SAFE_DELETE_ARRAY( rgdwAdjacency );
        return E_FAIL;
    }

    SAFE_DELETE_ARRAY( rgdwAdjacency );
    SAFE_RELEASE( m_pMesh );
    m_pMesh = pTempMesh;

    HRESULT hr;
    hr = CreateMaterials( L"", pd3dDevice, pd3dxMaterials, dwMaterials );

    // Extract data from m_pMesh for easy access
    m_dwNumVertices    = m_pMesh->GetNumVertices();
    m_dwNumFaces       = m_pMesh->GetNumFaces();
    m_dwBytesPerVertex = m_pMesh->GetNumBytesPerVertex();
    m_pMesh->GetIndexBuffer( &m_pIB );
    m_pMesh->GetVertexBuffer( &m_pVB );
    m_pMesh->GetDeclaration( decl );
    pd3dDevice->CreateVertexDeclaration( decl, &m_pDecl );

    return hr;
}


//-----------------------------------------------------------------------------
HRESULT CDXUTMesh::CreateMaterials( LPCWSTR strPath, IDirect3DDevice9 *pd3dDevice, D3DXMATERIAL* d3dxMtrls, DWORD dwNumMaterials )
{
    // Get material info for the mesh
    // Get the array of materials out of the buffer
    m_dwNumMaterials = dwNumMaterials;
    if( d3dxMtrls && m_dwNumMaterials > 0 )
    {
        // Allocate memory for the materials and textures
        m_pMaterials = new D3DMATERIAL9[m_dwNumMaterials];
        if( m_pMaterials == NULL )
            return E_OUTOFMEMORY;
        m_pTextures = new LPDIRECT3DBASETEXTURE9[m_dwNumMaterials];
        if( m_pTextures == NULL )
            return E_OUTOFMEMORY;
        m_strMaterials = new CHAR[m_dwNumMaterials][MAX_PATH];
        if( m_strMaterials == NULL )
            return E_OUTOFMEMORY;

        // Copy each material and create its texture
        for( DWORD i=0; i<m_dwNumMaterials; i++ )
        {
            // Copy the material
            m_pMaterials[i]         = d3dxMtrls[i].MatD3D;
            m_pTextures[i]          = NULL;

            // Create a texture
            if( d3dxMtrls[i].pTextureFilename )
            {
                StringCchCopyA( m_strMaterials[i], MAX_PATH, d3dxMtrls[i].pTextureFilename );

                WCHAR strTexture[MAX_PATH];
                WCHAR strTextureTemp[MAX_PATH];
                D3DXIMAGE_INFO ImgInfo;

                // First attempt to look for texture in the same folder as the input folder.
                MultiByteToWideChar( CP_ACP, 0, d3dxMtrls[i].pTextureFilename, -1, strTextureTemp, MAX_PATH );
                strTextureTemp[MAX_PATH-1] = 0;

                StringCchCopy( strTexture, MAX_PATH, strPath );
                StringCchCat( strTexture, MAX_PATH, strTextureTemp );

                // Inspect the texture file to determine the texture type.
                if( FAILED( D3DXGetImageInfoFromFile( strTexture, &ImgInfo ) ) )
                {
                    // Search the media folder
                    if( FAILED( DXUTFindDXSDKMediaFileCch( strTexture, MAX_PATH, strTextureTemp ) ) )
                        continue;  // Can't find. Skip.

                    D3DXGetImageInfoFromFile( strTexture, &ImgInfo );
                }

                // Call the appropriate loader according to the texture type.
                switch( ImgInfo.ResourceType )
                {
                    case D3DRTYPE_TEXTURE:
                    {
                        IDirect3DTexture9 *pTex;
                        if( SUCCEEDED( D3DXCreateTextureFromFile( pd3dDevice, strTexture, &pTex ) ) )
                        {
                            // Obtain the base texture interface
                            pTex->QueryInterface( IID_IDirect3DBaseTexture9, (LPVOID*)&m_pTextures[i] );
                            // Release the specialized instance
                            pTex->Release();
                        }
                        break;
                    }
                    case D3DRTYPE_CUBETEXTURE:
                    {
                        IDirect3DCubeTexture9 *pTex;
                        if( SUCCEEDED( D3DXCreateCubeTextureFromFile( pd3dDevice, strTexture, &pTex ) ) )
                        {
                            // Obtain the base texture interface
                            pTex->QueryInterface( IID_IDirect3DBaseTexture9, (LPVOID*)&m_pTextures[i] );
                            // Release the specialized instance
                            pTex->Release();
                        }
                        break;
                    }
                    case D3DRTYPE_VOLUMETEXTURE:
                    {
                        IDirect3DVolumeTexture9 *pTex;
                        if( SUCCEEDED( D3DXCreateVolumeTextureFromFile( pd3dDevice, strTexture, &pTex ) ) )
                        {
                            // Obtain the base texture interface
                            pTex->QueryInterface( IID_IDirect3DBaseTexture9, (LPVOID*)&m_pTextures[i] );
                            // Release the specialized instance
                            pTex->Release();
                        }
                        break;
                    }
                }
            }
        }
    }
    return S_OK;
}


//-----------------------------------------------------------------------------
HRESULT CDXUTMesh::SetFVF( LPDIRECT3DDEVICE9 pd3dDevice, DWORD dwFVF )
{
    LPD3DXMESH pTempMesh = NULL;

    if( m_pMesh )
    {
        if( FAILED( m_pMesh->CloneMeshFVF( m_pMesh->GetOptions(), dwFVF,
                                           pd3dDevice, &pTempMesh ) ) )
        {
            SAFE_RELEASE( pTempMesh );
            return E_FAIL;
        }

        DWORD dwOldFVF = 0;
        dwOldFVF = m_pMesh->GetFVF();
        SAFE_RELEASE( m_pMesh );
        m_pMesh = pTempMesh;

        // Compute normals if they are being requested and
        // the old mesh does not have them.
        if( !(dwOldFVF & D3DFVF_NORMAL) && dwFVF & D3DFVF_NORMAL )
        {
            D3DXComputeNormals( m_pMesh, NULL );
        }
    }

    return S_OK;
}




//-----------------------------------------------------------------------------
// Convert the mesh to the format specified by the given vertex declarations.
//-----------------------------------------------------------------------------
HRESULT CDXUTMesh::SetVertexDecl( LPDIRECT3DDEVICE9 pd3dDevice, const D3DVERTEXELEMENT9 *pDecl, 
                                  bool bAutoComputeNormals, bool bAutoComputeTangents, 
                                  bool bSplitVertexForOptimalTangents )
{
    LPD3DXMESH pTempMesh = NULL;

    if( m_pMesh )
    {
        if( FAILED( m_pMesh->CloneMesh( m_pMesh->GetOptions(), pDecl,
                                        pd3dDevice, &pTempMesh ) ) )
        {
            SAFE_RELEASE( pTempMesh );
            return E_FAIL;
        }
    }


    // Check if the old declaration contains a normal.
    bool bHadNormal = false;
    bool bHadTangent = false;
    D3DVERTEXELEMENT9 aOldDecl[MAX_FVF_DECL_SIZE];
    if( m_pMesh && SUCCEEDED( m_pMesh->GetDeclaration( aOldDecl ) ) )
    {
        for( UINT index = 0; index < D3DXGetDeclLength( aOldDecl ); ++index )
        {
            if( aOldDecl[index].Usage == D3DDECLUSAGE_NORMAL )
            {
                bHadNormal = true;
            }
            if( aOldDecl[index].Usage == D3DDECLUSAGE_TANGENT )
            {
                bHadTangent = true;
            }
        }
    }

    // Check if the new declaration contains a normal.
    bool bHaveNormalNow = false;
    bool bHaveTangentNow = false;
    D3DVERTEXELEMENT9 aNewDecl[MAX_FVF_DECL_SIZE];
    if( pTempMesh && SUCCEEDED( pTempMesh->GetDeclaration( aNewDecl ) ) )
    {
        for( UINT index = 0; index < D3DXGetDeclLength( aNewDecl ); ++index )
        {
            if( aNewDecl[index].Usage == D3DDECLUSAGE_NORMAL )
            {
                bHaveNormalNow = true;
            }
            if( aNewDecl[index].Usage == D3DDECLUSAGE_TANGENT )
            {
                bHaveTangentNow = true;
            }
        }
    }

    SAFE_RELEASE( m_pMesh );

    if( pTempMesh )
    {
        m_pMesh = pTempMesh;

        if( !bHadNormal && bHaveNormalNow && bAutoComputeNormals )
        {
            // Compute normals in case the meshes have them
            D3DXComputeNormals( m_pMesh, NULL );
        }

        if( bHaveNormalNow && !bHadTangent && bHaveTangentNow && bAutoComputeTangents )
        {
            ID3DXMesh* pNewMesh;
            HRESULT hr;

            DWORD *rgdwAdjacency = NULL;
            rgdwAdjacency = new DWORD[m_pMesh->GetNumFaces() * 3];
            if( rgdwAdjacency == NULL )
                return E_OUTOFMEMORY;
            V( m_pMesh->GenerateAdjacency(1e-6f,rgdwAdjacency) );

            float fPartialEdgeThreshold;
            float fSingularPointThreshold;
            float fNormalEdgeThreshold;
            if( bSplitVertexForOptimalTangents )
            {
                fPartialEdgeThreshold = 0.01f;
                fSingularPointThreshold = 0.25f;
                fNormalEdgeThreshold = 0.01f;
            }
            else
            {
                fPartialEdgeThreshold = -1.01f;
                fSingularPointThreshold = 0.01f;
                fNormalEdgeThreshold = -1.01f;
            }

            // Compute tangents, which are required for normal mapping
            hr = D3DXComputeTangentFrameEx( m_pMesh, 
                                            D3DDECLUSAGE_TEXCOORD, 0, 
                                            D3DDECLUSAGE_TANGENT, 0,
                                            D3DX_DEFAULT, 0, 
                                            D3DDECLUSAGE_NORMAL, 0,
                                            0, rgdwAdjacency, 
                                            fPartialEdgeThreshold, fSingularPointThreshold, fNormalEdgeThreshold, 
                                            &pNewMesh, NULL );

            SAFE_DELETE_ARRAY( rgdwAdjacency );
            if( FAILED(hr) )
                return hr;

            SAFE_RELEASE( m_pMesh );
            m_pMesh = pNewMesh;
        }
    }

    return S_OK;
}




//-----------------------------------------------------------------------------
HRESULT CDXUTMesh::RestoreDeviceObjects( LPDIRECT3DDEVICE9 pd3dDevice )
{
    return S_OK;
}




//-----------------------------------------------------------------------------
HRESULT CDXUTMesh::InvalidateDeviceObjects()
{
    SAFE_RELEASE( m_pIB );
    SAFE_RELEASE( m_pVB );
    SAFE_RELEASE( m_pDecl );

    return S_OK;
}




//-----------------------------------------------------------------------------
HRESULT CDXUTMesh::Destroy()
{
    InvalidateDeviceObjects();
    for( UINT i=0; i<m_dwNumMaterials; i++ )
        SAFE_RELEASE( m_pTextures[i] );
    SAFE_DELETE_ARRAY( m_pTextures );
    SAFE_DELETE_ARRAY( m_pMaterials );
    SAFE_DELETE_ARRAY( m_strMaterials );

    SAFE_RELEASE( m_pMesh );

    m_dwNumMaterials = 0L;

    return S_OK;
}




//-----------------------------------------------------------------------------
HRESULT CDXUTMesh::Render( LPDIRECT3DDEVICE9 pd3dDevice, bool bDrawOpaqueSubsets,
                          bool bDrawAlphaSubsets )
{
    if( NULL == m_pMesh )
        return E_FAIL;

    // Frist, draw the subsets without alpha
    if( bDrawOpaqueSubsets )
    {
        for( DWORD i=0; i<m_dwNumMaterials; i++ )
        {
            if( m_bUseMaterials )
            {
                if( m_pMaterials[i].Diffuse.a < 1.0f )
                    continue;
                pd3dDevice->SetMaterial( &m_pMaterials[i] );
                pd3dDevice->SetTexture( 0, m_pTextures[i] );
            }
            m_pMesh->DrawSubset( i );
        }
    }

    // Then, draw the subsets with alpha
    if( bDrawAlphaSubsets && m_bUseMaterials )
    {
        for( DWORD i=0; i<m_dwNumMaterials; i++ )
        {
            if( m_pMaterials[i].Diffuse.a == 1.0f )
                continue;

            // Set the material and texture
            pd3dDevice->SetMaterial( &m_pMaterials[i] );
            pd3dDevice->SetTexture( 0, m_pTextures[i] );
            m_pMesh->DrawSubset( i );
        }
    }

    return S_OK;
}




//-----------------------------------------------------------------------------
HRESULT CDXUTMesh::Render( ID3DXEffect *pEffect,
                           D3DXHANDLE hTexture,
                           D3DXHANDLE hDiffuse,
                           D3DXHANDLE hAmbient,
                           D3DXHANDLE hSpecular,
                           D3DXHANDLE hEmissive,
                           D3DXHANDLE hPower,
                           bool bDrawOpaqueSubsets,
                           bool bDrawAlphaSubsets )
{
    if( NULL == m_pMesh )
        return E_FAIL;

    UINT cPasses;
    // Frist, draw the subsets without alpha
    if( bDrawOpaqueSubsets )
    {
        pEffect->Begin( &cPasses, 0 );
        for( UINT p = 0; p < cPasses; ++p )
        {
            pEffect->BeginPass( p );
            for( DWORD i=0; i<m_dwNumMaterials; i++ )
            {
                if( m_bUseMaterials )
                {
                    if( m_pMaterials[i].Diffuse.a < 1.0f )
                        continue;
                    if( hTexture )
                        pEffect->SetTexture( hTexture, m_pTextures[i] );
                    // D3DCOLORVALUE and D3DXVECTOR4 are data-wise identical.
                    // No conversion is needed.
                    if( hDiffuse )
                        pEffect->SetVector( hDiffuse, (D3DXVECTOR4*)&m_pMaterials[i].Diffuse );
                    if( hAmbient )
                        pEffect->SetVector( hAmbient, (D3DXVECTOR4*)&m_pMaterials[i].Ambient );
                    if( hSpecular )
                        pEffect->SetVector( hSpecular, (D3DXVECTOR4*)&m_pMaterials[i].Specular );
                    if( hEmissive )
                        pEffect->SetVector( hEmissive, (D3DXVECTOR4*)&m_pMaterials[i].Emissive );
                    if( hPower )
                        pEffect->SetFloat( hPower, m_pMaterials[i].Power );
                    pEffect->CommitChanges();
                }
                m_pMesh->DrawSubset( i );
            }
            pEffect->EndPass();
        }
        pEffect->End();
    }

    // Then, draw the subsets with alpha
    if( bDrawAlphaSubsets && m_bUseMaterials )
    {
        pEffect->Begin( &cPasses, 0 );
        for( UINT p = 0; p < cPasses; ++p )
        {
            pEffect->BeginPass( p );
            for( DWORD i=0; i<m_dwNumMaterials; i++ )
            {
                if( m_bUseMaterials )
                {
                    if( m_pMaterials[i].Diffuse.a == 1.0f )
                        continue;
                    if( hTexture )
                        pEffect->SetTexture( hTexture, m_pTextures[i] );
                    // D3DCOLORVALUE and D3DXVECTOR4 are data-wise identical.
                    // No conversion is needed.
                    if( hDiffuse )
                        pEffect->SetVector( hDiffuse, (D3DXVECTOR4*)&m_pMaterials[i].Diffuse );
                    if( hAmbient )
                        pEffect->SetVector( hAmbient, (D3DXVECTOR4*)&m_pMaterials[i].Ambient );
                    if( hSpecular )
                        pEffect->SetVector( hSpecular, (D3DXVECTOR4*)&m_pMaterials[i].Specular );
                    if( hEmissive )
                        pEffect->SetVector( hEmissive, (D3DXVECTOR4*)&m_pMaterials[i].Emissive );
                    if( hPower )
                        pEffect->SetFloat( hPower, m_pMaterials[i].Power );
                    pEffect->CommitChanges();
                }
                m_pMesh->DrawSubset( i );
            }
            pEffect->EndPass();
        }
        pEffect->End();
    }

    return S_OK;
}




//-----------------------------------------------------------------------------
CDXUTMeshFrame::CDXUTMeshFrame( LPCWSTR strName )
{
    StringCchCopy( m_strName, 512, strName );
    D3DXMatrixIdentity( &m_mat );
    m_pMesh  = NULL;

    m_pChild = NULL;
    m_pNext  = NULL;
}




//-----------------------------------------------------------------------------
CDXUTMeshFrame::~CDXUTMeshFrame()
{
    SAFE_DELETE( m_pChild );
    SAFE_DELETE( m_pNext );
}




//-----------------------------------------------------------------------------
bool CDXUTMeshFrame::EnumMeshes( bool (*EnumMeshCB)(CDXUTMesh*,void*),
                            void* pContext )
{
    if( m_pMesh )
        EnumMeshCB( m_pMesh, pContext );
    if( m_pChild )
        m_pChild->EnumMeshes( EnumMeshCB, pContext );
    if( m_pNext )
        m_pNext->EnumMeshes( EnumMeshCB, pContext );

    return TRUE;
}




//-----------------------------------------------------------------------------
CDXUTMesh* CDXUTMeshFrame::FindMesh( LPCWSTR strMeshName )
{
    CDXUTMesh* pMesh;

    if( m_pMesh )
        if( !lstrcmpi( m_pMesh->m_strName, strMeshName ) )
            return m_pMesh;

    if( m_pChild )
        if( NULL != ( pMesh = m_pChild->FindMesh( strMeshName ) ) )
            return pMesh;

    if( m_pNext )
        if( NULL != ( pMesh = m_pNext->FindMesh( strMeshName ) ) )
            return pMesh;

    return NULL;
}




//-----------------------------------------------------------------------------
CDXUTMeshFrame* CDXUTMeshFrame::FindFrame( LPCWSTR strFrameName )
{
    CDXUTMeshFrame* pFrame;

    if( !lstrcmpi( m_strName, strFrameName ) )
        return this;

    if( m_pChild )
        if( NULL != ( pFrame = m_pChild->FindFrame( strFrameName ) ) )
            return pFrame;

    if( m_pNext )
        if( NULL != ( pFrame = m_pNext->FindFrame( strFrameName ) ) )
            return pFrame;

    return NULL;
}




//-----------------------------------------------------------------------------
HRESULT CDXUTMeshFrame::Destroy()
{
    if( m_pMesh )  m_pMesh->Destroy();
    if( m_pChild ) m_pChild->Destroy();
    if( m_pNext )  m_pNext->Destroy();

    SAFE_DELETE( m_pMesh );
    SAFE_DELETE( m_pNext );
    SAFE_DELETE( m_pChild );

    return S_OK;
}




//-----------------------------------------------------------------------------
HRESULT CDXUTMeshFrame::RestoreDeviceObjects( LPDIRECT3DDEVICE9 pd3dDevice )
{
    if( m_pMesh )  m_pMesh->RestoreDeviceObjects( pd3dDevice );
    if( m_pChild ) m_pChild->RestoreDeviceObjects( pd3dDevice );
    if( m_pNext )  m_pNext->RestoreDeviceObjects( pd3dDevice );
    return S_OK;
}




//-----------------------------------------------------------------------------
HRESULT CDXUTMeshFrame::InvalidateDeviceObjects()
{
    if( m_pMesh )  m_pMesh->InvalidateDeviceObjects();
    if( m_pChild ) m_pChild->InvalidateDeviceObjects();
    if( m_pNext )  m_pNext->InvalidateDeviceObjects();
    return S_OK;
}




//-----------------------------------------------------------------------------
HRESULT CDXUTMeshFrame::Render( LPDIRECT3DDEVICE9 pd3dDevice, bool bDrawOpaqueSubsets,
                           bool bDrawAlphaSubsets, D3DXMATRIX* pmatWorldMatrix )
{
    // For pure devices, specify the world transform. If the world transform is not
    // specified on pure devices, this function will fail.

    D3DXMATRIX matSavedWorld, matWorld;

    if ( NULL == pmatWorldMatrix )
        pd3dDevice->GetTransform( D3DTS_WORLD, &matSavedWorld );
    else
        matSavedWorld = *pmatWorldMatrix;

    D3DXMatrixMultiply( &matWorld, &m_mat, &matSavedWorld );
    pd3dDevice->SetTransform( D3DTS_WORLD, &matWorld );

    if( m_pMesh )
        m_pMesh->Render( pd3dDevice, bDrawOpaqueSubsets, bDrawAlphaSubsets );

    if( m_pChild )
        m_pChild->Render( pd3dDevice, bDrawOpaqueSubsets, bDrawAlphaSubsets, &matWorld );

    pd3dDevice->SetTransform( D3DTS_WORLD, &matSavedWorld );

    if( m_pNext )
        m_pNext->Render( pd3dDevice, bDrawOpaqueSubsets, bDrawAlphaSubsets, &matSavedWorld );

    return S_OK;
}




//-----------------------------------------------------------------------------
HRESULT CDXUTMeshFile::LoadFrame( LPDIRECT3DDEVICE9 pd3dDevice,
                             LPD3DXFILEDATA pFileData,
                             CDXUTMeshFrame* pParentFrame )
{
    LPD3DXFILEDATA   pChildData = NULL;
    GUID Guid;
    SIZE_T      cbSize;
    CDXUTMeshFrame*  pCurrentFrame;
    HRESULT     hr;

    // Get the type of the object
    if( FAILED( hr = pFileData->GetType( &Guid ) ) )
        return hr;

    if( Guid == TID_D3DRMMesh )
    {
        hr = LoadMesh( pd3dDevice, pFileData, pParentFrame );
        if( FAILED(hr) )
            return hr;
    }
    if( Guid == TID_D3DRMFrameTransformMatrix )
    {
        D3DXMATRIX* pmatMatrix;
        hr = pFileData->Lock(&cbSize, (LPCVOID*)&pmatMatrix );
        if( FAILED(hr) )
            return hr;

        // Update the parent's matrix with the new one
        pParentFrame->SetMatrix( pmatMatrix );
    }
    if( Guid == TID_D3DRMFrame )
    {
        // Get the frame name
        CHAR  strAnsiName[512] = "";
        WCHAR strName[512];
        SIZE_T dwNameLength = 512;
        SIZE_T cChildren;
        if( FAILED( hr = pFileData->GetName( strAnsiName, &dwNameLength ) ) )
            return hr;

        MultiByteToWideChar( CP_ACP, 0, strAnsiName, -1, strName, 512 );
        strName[511] = 0;

        // Create the frame
        pCurrentFrame = new CDXUTMeshFrame( strName );
        if( pCurrentFrame == NULL )
            return E_OUTOFMEMORY;

        pCurrentFrame->m_pNext = pParentFrame->m_pChild;
        pParentFrame->m_pChild = pCurrentFrame;

        // Enumerate child objects
        pFileData->GetChildren(&cChildren);
        for (UINT iChild = 0; iChild < cChildren; iChild++)
        {
            // Query the child for its FileData
            hr = pFileData->GetChild(iChild, &pChildData );
            if( SUCCEEDED(hr) )
            {
                hr = LoadFrame( pd3dDevice, pChildData, pCurrentFrame );
                SAFE_RELEASE( pChildData );
            }

            if( FAILED(hr) )
                return hr;
        }
    }

    return S_OK;
}




//-----------------------------------------------------------------------------
HRESULT CDXUTMeshFile::LoadMesh( LPDIRECT3DDEVICE9 pd3dDevice,
                            LPD3DXFILEDATA pFileData,
                            CDXUTMeshFrame* pParentFrame )
{
    // Currently only allowing one mesh per frame
    if( pParentFrame->m_pMesh )
        return E_FAIL;

    // Get the mesh name
    CHAR  strAnsiName[512] = {0};
    WCHAR strName[512];
    SIZE_T dwNameLength = 512;
    HRESULT hr;
    if( FAILED( hr = pFileData->GetName( strAnsiName, &dwNameLength ) ) )
        return hr;

    MultiByteToWideChar( CP_ACP, 0, strAnsiName, -1, strName, 512 );
    strName[511] = 0;

    // Create the mesh
    pParentFrame->m_pMesh = new CDXUTMesh( strName );
    if( pParentFrame->m_pMesh == NULL )
        return E_OUTOFMEMORY;
    pParentFrame->m_pMesh->Create( pd3dDevice, pFileData );

    return S_OK;
}




//-----------------------------------------------------------------------------
HRESULT CDXUTMeshFile::CreateFromResource( LPDIRECT3DDEVICE9 pd3dDevice, LPCWSTR strResource, LPCWSTR strType )
{
    LPD3DXFILE           pDXFile   = NULL;
    LPD3DXFILEENUMOBJECT pEnumObj  = NULL;
    LPD3DXFILEDATA       pFileData = NULL;
    HRESULT hr;
    SIZE_T cChildren;

    // Create a x file object
    if( FAILED( hr = D3DXFileCreate( &pDXFile ) ) )
        return E_FAIL;

    // Register templates for d3drm and patch extensions.
    if( FAILED( hr = pDXFile->RegisterTemplates( (void*)D3DRM_XTEMPLATES,
                                                 D3DRM_XTEMPLATE_BYTES ) ) )
    {
        SAFE_RELEASE( pDXFile );
        return E_FAIL;
    }
    
    CHAR strTypeAnsi[MAX_PATH];
    CHAR strResourceAnsi[MAX_PATH];

    WideCharToMultiByte( CP_ACP, 0, strType, -1, strTypeAnsi, MAX_PATH, NULL, NULL );
    strTypeAnsi[MAX_PATH-1] = 0;

    WideCharToMultiByte( CP_ACP, 0, strResource, -1, strResourceAnsi, MAX_PATH, NULL, NULL );
    strResourceAnsi[MAX_PATH-1] = 0;

    D3DXF_FILELOADRESOURCE dxlr;
    dxlr.hModule = NULL;
    dxlr.lpName = strResourceAnsi;
    dxlr.lpType = strTypeAnsi;

    // Create enum object
    hr = pDXFile->CreateEnumObject( (void*)&dxlr, D3DXF_FILELOAD_FROMRESOURCE, 
                                    &pEnumObj );
    if( FAILED(hr) )
    {
        SAFE_RELEASE( pDXFile );
        return hr;
    }

    // Enumerate top level objects (which are always frames)
    pEnumObj->GetChildren(&cChildren);
    for (UINT iChild = 0; iChild < cChildren; iChild++)
    {
        hr = pEnumObj->GetChild(iChild, &pFileData);
        if (FAILED(hr))
            return hr;

        hr = LoadFrame( pd3dDevice, pFileData, this );
        SAFE_RELEASE( pFileData );
        if( FAILED(hr) )
        {
            SAFE_RELEASE( pEnumObj );
            SAFE_RELEASE( pDXFile );
            return E_FAIL;
        }
    }

    SAFE_RELEASE( pFileData );
    SAFE_RELEASE( pEnumObj );
    SAFE_RELEASE( pDXFile );

    return S_OK;
}




//-----------------------------------------------------------------------------
HRESULT CDXUTMeshFile::Create( LPDIRECT3DDEVICE9 pd3dDevice, LPCWSTR strFilename )
{
    LPD3DXFILE           pDXFile   = NULL;
    LPD3DXFILEENUMOBJECT pEnumObj  = NULL;
    LPD3DXFILEDATA       pFileData = NULL;
    HRESULT hr;
    SIZE_T cChildren;

    // Create a x file object
    if( FAILED( hr = D3DXFileCreate( &pDXFile ) ) )
        return E_FAIL;

    // Register templates for d3drm and patch extensions.
    if( FAILED( hr = pDXFile->RegisterTemplates( (void*)D3DRM_XTEMPLATES,
                                                 D3DRM_XTEMPLATE_BYTES ) ) )
    {
        SAFE_RELEASE( pDXFile );
        return E_FAIL;
    }

    // Find the path to the file, and convert it to ANSI (for the D3DXOF API)
    WCHAR strPath[MAX_PATH];
    CHAR  strPathANSI[MAX_PATH];
    DXUTFindDXSDKMediaFileCch( strPath, sizeof(strPath) / sizeof(WCHAR), strFilename );
    
    
    WideCharToMultiByte( CP_ACP, 0, strPath, -1, strPathANSI, MAX_PATH, NULL, NULL );
    strPathANSI[MAX_PATH-1] = 0;

    
    // Create enum object
    hr = pDXFile->CreateEnumObject( (void*)strPathANSI, D3DXF_FILELOAD_FROMFILE, 
                                    &pEnumObj );
    if( FAILED(hr) )
    {
        SAFE_RELEASE( pDXFile );
        return hr;
    }

    // Enumerate top level objects (which are always frames)
    pEnumObj->GetChildren(&cChildren);
    for (UINT iChild = 0; iChild < cChildren; iChild++)
    {
        hr = pEnumObj->GetChild(iChild, &pFileData);
        if (FAILED(hr))
            return hr;

        hr = LoadFrame( pd3dDevice, pFileData, this );
        SAFE_RELEASE( pFileData );
        if( FAILED(hr) )
        {
            SAFE_RELEASE( pEnumObj );
            SAFE_RELEASE( pDXFile );
            return E_FAIL;
        }
    }

    SAFE_RELEASE( pFileData );
    SAFE_RELEASE( pEnumObj );
    SAFE_RELEASE( pDXFile );

    return S_OK;
}




//-----------------------------------------------------------------------------
HRESULT CDXUTMeshFile::Render( LPDIRECT3DDEVICE9 pd3dDevice, D3DXMATRIX* pmatWorldMatrix )
{

    // For pure devices, specify the world transform. If the world transform is not
    // specified on pure devices, this function will fail.

    // Set up the world transformation
    D3DXMATRIX matSavedWorld, matWorld;

    if ( NULL == pmatWorldMatrix )
        pd3dDevice->GetTransform( D3DTS_WORLD, &matSavedWorld );
    else
        matSavedWorld = *pmatWorldMatrix;

    D3DXMatrixMultiply( &matWorld, &matSavedWorld, &m_mat );
    pd3dDevice->SetTransform( D3DTS_WORLD, &matWorld );

    // Render opaque subsets in the meshes
    if( m_pChild )
        m_pChild->Render( pd3dDevice, TRUE, FALSE, &matWorld );

    // Enable alpha blending
    pd3dDevice->SetRenderState( D3DRS_ALPHABLENDENABLE, TRUE );
    pd3dDevice->SetRenderState( D3DRS_SRCBLEND,  D3DBLEND_SRCALPHA );
    pd3dDevice->SetRenderState( D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA );

    // Render alpha subsets in the meshes
    if( m_pChild )
        m_pChild->Render( pd3dDevice, FALSE, TRUE, &matWorld );

    // Restore state
    pd3dDevice->SetRenderState( D3DRS_ALPHABLENDENABLE, FALSE );
    pd3dDevice->SetTransform( D3DTS_WORLD, &matSavedWorld );

    return S_OK;
}




