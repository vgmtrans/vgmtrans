// DragDropSource.cpp: implementation of the CDragDropSource class.
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "DragDropSource.h"

//////////////////////////////////////////////////////////////////////
// Construction/Destruction

CDragDropSource::CDragDropSource() :
    m_bInitialized(false), m_dwLastEffect(DROPEFFECT_NONE), m_cde(0), m_rgde(NULL)
{
}

CDragDropSource::~CDragDropSource()
{
    for ( int i = 0; i < m_cde; i++ )
        {
        CoTaskMemFree ( m_rgde[i].fe.ptd );
        ReleaseStgMedium ( &m_rgde[i].stgm );
        }

    CoTaskMemFree ( m_rgde );
}


//////////////////////////////////////////////////////////////////////
// IDataObject methods

HRESULT CDragDropSource::SetData ( 
    FORMATETC* pfe, STGMEDIUM* pstgm, BOOL fRelease )
{
    if ( !fRelease )
        return E_NOTIMPL;

LPDATAENTRY pde;
HRESULT hres = FindFORMATETC ( pfe, &pde, TRUE );

    if ( SUCCEEDED(hres) )
        {
        if ( pde->stgm.tymed )
            {
            ReleaseStgMedium ( &pde->stgm );
            ZeroMemory ( &pde->stgm, sizeof(STGMEDIUM) );
            }

        if ( fRelease ) 
            {
            pde->stgm = *pstgm;
            hres = S_OK;
            }
        else 
            {
            hres = AddRefStgMedium ( pstgm, &pde->stgm, TRUE );
            }

        pde->fe.tymed = pde->stgm.tymed;    /* Keep in sync */

        /* Subtlety!  Break circular reference loop */
        if ( GetCanonicalIUnknown ( pde->stgm.pUnkForRelease ) ==
             GetCanonicalIUnknown ( static_cast<IDataObject*>(this) ))
            {
            pde->stgm.pUnkForRelease->Release();
            pde->stgm.pUnkForRelease = NULL;
            }
        }

    return hres;
}

STDMETHODIMP CDragDropSource::QueryGetData ( FORMATETC* pfe )
{
LPDATAENTRY pde;

    return FindFORMATETC ( pfe, &pde, FALSE );
}

HRESULT CDragDropSource::GetData ( FORMATETC* pfe, STGMEDIUM* pstgm )
{
LPDATAENTRY pde;
HRESULT hres;

    hres = FindFORMATETC ( pfe, &pde, FALSE );

    if ( SUCCEEDED(hres) )
        hres = AddRefStgMedium ( &pde->stgm, pstgm, FALSE );

    return hres;
}

// This typedef creates an IEnumFORMATETC enumerator that the drag source
// uses in EnumFormatEtc().
typedef CComEnumOnSTL<IEnumFORMATETC,           // name of enumerator interface
                      &IID_IEnumFORMATETC,      // IID of enumerator interface
                      FORMATETC,                // type of object to return
                      _Copy<FORMATETC>,         // copy policy class
                      std::vector<FORMATETC> >  // type of collection holding the data
    CEnumFORMATETCImpl;

STDMETHODIMP CDragDropSource::EnumFormatEtc ( 
    DWORD dwDirection, IEnumFORMATETC** ppenumFormatEtc )
{   
HRESULT hr;
CComObject<CEnumFORMATETCImpl>* pImpl;

    // Create an enumerator object.
    hr = CComObject<CEnumFORMATETCImpl>::CreateInstance ( &pImpl );

    if ( FAILED(hr) )
        return hr;

    pImpl->AddRef();
    
    // Fill in m_vecSupportedFormats with the formats that the caller has
    // put in this object.
    m_vecSupportedFormats.clear();

    for ( int i = 0; i < m_cde; i++ )
        m_vecSupportedFormats.push_back ( m_rgde[i].fe );

    // Init the enumerator, passing it our vector of supported FORMATETCs.
    hr = pImpl->Init ( GetUnknown(), m_vecSupportedFormats );
    
    if ( FAILED(hr) )
        {
        pImpl->Release();
        return E_UNEXPECTED;
        }

    // Return the requested interface to the caller.
    hr = pImpl->QueryInterface ( ppenumFormatEtc );

    pImpl->Release();
    return hr;
}


//////////////////////////////////////////////////////////////////////
// IDropSource methods

STDMETHODIMP CDragDropSource::QueryContinueDrag (
    BOOL fEscapePressed, DWORD grfKeyState )
{
    // If ESC was pressed, cancel the drag. If the left button was released,
    // do the drop.
    if ( fEscapePressed )
        return DRAGDROP_S_CANCEL;
    else if ( !(grfKeyState & MK_LBUTTON) )
        {
        if ( DROPEFFECT_NONE == m_dwLastEffect )
            return DRAGDROP_S_CANCEL;

        // If the drop was accepted, do the extracting here, so that when we
        // return, the files are in the temp dir & ready for Explorer to copy.
        // If ExtractFilesFromCab() failed and m_bAbortingDueToMissingCab is true,
        // then return success anyway, because we don't want to fail the whole
        // shebang in the case of a missing CAB file (some files may still be
        // extractable).
//        if ( ExtractFilesFromCab() || m_bAbortingDueToMissingCab )
//            return DRAGDROP_S_DROP;
//        else
//            return E_UNEXPECTED;
			return DRAGDROP_S_DROP;
        }
    else
        return S_OK;
}

STDMETHODIMP CDragDropSource::GiveFeedback ( DWORD dwEffect )
{
    m_dwLastEffect = dwEffect;
    return DRAGDROP_S_USEDEFAULTCURSORS;
}


//////////////////////////////////////////////////////////////////////
// Operations

bool CDragDropSource::Init (
    /*LPCTSTR szItemName,*/ std::vector<CDraggedFileInfo>& vec )
{
FORMATETC fetc = { CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
STGMEDIUM stg = { TYMED_HGLOBAL };
HGLOBAL   hgl;
size_t    cbyData = sizeof(DROPFILES);
TCHAR     szTempDir[MAX_PATH];
CString   sTempDir, sTempFilePath;

    // Init member data
    m_vecDraggedFiles = vec;
    //m_sItemName = szItemName;

    // Get the path to the temp dir, we'll extract files here
    GetTempPath ( countof(szTempDir), szTempDir );
    PathAddBackslash ( szTempDir );
    sTempDir = szTempDir;

    // Calc how much space is needed to hold all the filenames
std::vector<CDraggedFileInfo>::iterator it;

    for ( it = m_vecDraggedFiles.begin(); it != m_vecDraggedFiles.end(); it++ )
        {
        sTempFilePath = sTempDir + it->sFilename;

        cbyData += sizeof(TCHAR) * (1 + sTempFilePath.GetLength());
        }

    // One more TCHAR for the final null char
    cbyData += sizeof(TCHAR);

    // Alloc a buffer to hold the DROPFILES data.
    hgl = GlobalAlloc ( GMEM_MOVEABLE | GMEM_ZEROINIT, cbyData );

    if ( NULL == hgl )
        return false;

DROPFILES* pDrop = (DROPFILES*) GlobalLock ( hgl );

    if ( NULL == pDrop )
        {
        GlobalFree ( hgl );
        return false;
        }

    pDrop->pFiles = sizeof(DROPFILES);

#ifdef _UNICODE
    pDrop->fWide = 1;
#endif

    // Copy the filenames into the buffer.
LPTSTR pszFilename = (LPTSTR) (pDrop + 1);
    
 /*   for ( it = m_vecDraggedFiles.begin(); it != m_vecDraggedFiles.end(); it++ )
        {
        sTempFilePath = sTempDir + it->sFilename;
        int fd = _tcreat ( sTempFilePath, _S_IREAD|_S_IWRITE );

        if ( -1 != fd )
            _close ( fd );

        _tcscpy ( pszFilename, sTempFilePath );
        pszFilename += sTempFilePath.GetLength() + 1;

        it->sTempFilePath = sTempFilePath;

        // Keep track of this temp file so we can clean up when the app exits.
        g_vecsTempFiles.push_back ( sTempFilePath );
        }
*/
    GlobalUnlock ( hgl );
    stg.hGlobal = hgl;

    if ( FAILED( SetData ( &fetc, &stg, TRUE ) ))
        {
        GlobalFree ( hgl );
        return false;
        }

    // We're done.
    m_bInitialized = true;
    return true;
}

HRESULT CDragDropSource::DoDragDrop ( DWORD dwOKEffects, DWORD* pdwEffect )
{
    if ( !m_bInitialized )
        return E_FAIL;

CComQIPtr<IDropSource> pDropSrc = this;
CComQIPtr<IDataObject> pDataObj = this;

    ATLASSERT(pDropSrc && pDataObj);

    return ::DoDragDrop ( pDataObj, pDropSrc, dwOKEffects, pdwEffect );
}


//////////////////////////////////////////////////////////////////////
// Other methods

HRESULT CDragDropSource::FindFORMATETC ( 
    FORMATETC* pfe, LPDATAENTRY* ppde, BOOL fAdd )
{
    *ppde = NULL;

    /* Comparing two DVTARGETDEVICE structures is hard, so we don't even try */
    if ( pfe->ptd != NULL )
        return DV_E_DVTARGETDEVICE;

    /* See if it's in our list */
	int ide;
    for (ide = 0; ide < m_cde; ide++ )
        {
        if ( m_rgde[ide].fe.cfFormat == pfe->cfFormat &&
             m_rgde[ide].fe.dwAspect == pfe->dwAspect &&
             m_rgde[ide].fe.lindex == pfe->lindex )
            {
            if ( fAdd || ( m_rgde[ide].fe.tymed & pfe->tymed ) )
                {
                *ppde = &m_rgde[ide];
                return S_OK;
                }
            else 
                {
                return DV_E_TYMED;
                }
            }
        }

    if ( !fAdd )
        return DV_E_FORMATETC;

LPDATAENTRY pdeT = (LPDATAENTRY) CoTaskMemRealloc ( m_rgde,
                                                    sizeof(DATAENTRY) * (m_cde+1) );

    if ( pdeT )
        {
        m_rgde = pdeT;
        m_cde++;
        m_rgde[ide].fe = *pfe;
        
        ZeroMemory ( &pdeT[ide].stgm, sizeof(STGMEDIUM) );
        *ppde = &m_rgde[ide];

        return S_OK;
        } 
    else 
        {
        return E_OUTOFMEMORY;
        }
}

HRESULT CDragDropSource::AddRefStgMedium ( 
    STGMEDIUM* pstgmIn, STGMEDIUM* pstgmOut, BOOL fCopyIn )
{
HRESULT hres = S_OK;
STGMEDIUM stgmOut = *pstgmIn;

    if ( pstgmIn->pUnkForRelease == NULL &&
         !(pstgmIn->tymed & (TYMED_ISTREAM | TYMED_ISTORAGE)) ) 
        {
        if ( fCopyIn )
            {
            /* Object needs to be cloned */
            if ( pstgmIn->tymed == TYMED_HGLOBAL )
                {
                stgmOut.hGlobal = GlobalClone ( pstgmIn->hGlobal );

                if ( !stgmOut.hGlobal )
                    hres = E_OUTOFMEMORY;
                }
            else 
                hres = DV_E_TYMED;      /* Don't know how to clone GDI objects */
            } 
        else  
            stgmOut.pUnkForRelease = static_cast<IDataObject*>(this);
        }

    if ( SUCCEEDED(hres) )
        {
        switch ( stgmOut.tymed ) 
            {
            case TYMED_ISTREAM:
                stgmOut.pstm->AddRef();
            break;

            case TYMED_ISTORAGE:
                stgmOut.pstg->AddRef();
            break;
            }

        if ( stgmOut.pUnkForRelease )
            stgmOut.pUnkForRelease->AddRef();

        *pstgmOut = stgmOut;
        }

    return hres;
}

HGLOBAL CDragDropSource::GlobalClone ( HGLOBAL hglobIn )
{
HGLOBAL hglobOut = NULL;
LPVOID pvIn = GlobalLock(hglobIn);

    if ( pvIn)
        {
        SIZE_T cb = GlobalSize ( hglobIn );

        HGLOBAL hglobOut = GlobalAlloc ( GMEM_FIXED, cb );

        if ( hglobOut )
            CopyMemory(hglobOut, pvIn, cb);

        GlobalUnlock(hglobIn);
        }

    return hglobOut;
}

IUnknown* CDragDropSource::GetCanonicalIUnknown ( IUnknown* punk )
{
IUnknown *punkCanonical;

    if ( punk && SUCCEEDED( punk->QueryInterface ( IID_IUnknown,
                                                  (void**) &punkCanonical )) )
        {
        punkCanonical->Release();
        }
    else 
        {
        punkCanonical = punk;
        }
        
    return punkCanonical;
}


//////////////////////////////////////////////////////////////////////
// CAB extraction/callback methods
/*
bool CDragDropSource::ExtractFilesFromCab()
{
TCHAR szCabDir[MAX_PATH], szCabTitle[MAX_PATH];
ERF erf = {0};
FDICABINETINFO info = {0};
int hf = 0;
HFDI hfdi = NULL;

    // Check that the passed-in file exists.
    if ( !PathFileExists ( m_sCabFilePath ) )
        {
        ATLTRACE("Error: CAB file not found in CDragDropSource::ExtractFilesFromCab()\n");
        return false;
        }

    // The CAB API takes the CAB file path as separate dir/name parts,
    // so split up the path.
    lstrcpyn ( szCabDir, m_sCabFilePath, countof(szCabDir) );
    PathRemoveFileSpec ( szCabDir );
    PathAddBackslash ( szCabDir );
    lstrcpyn ( szCabTitle, PathFindFileName ( m_sCabFilePath ), countof(szCabTitle) );

    // Init the CAB decompression engine.
    hfdi = FDICreate ( cab_Alloc, cab_Free, cab_Open, cab_Read, cab_Write,
                       cab_Close, cab_Seek, 0, &erf );

    if ( NULL == hfdi )
        {
        ATLTRACE("Error: FDICreate() failed in CDragDropSource::ExtractFilesFromCab()\n");
        return false;
        }

    // Open the CAB file for reading.
    hf = cab_Open ( T2A(LPTSTR(LPCTSTR(m_sCabFilePath))), _O_BINARY|_O_RDONLY, 0 );

    if ( -1 == hf )
        {
        ATLTRACE("Error: cab_Open() failed in CDragDropSource::ExtractFilesFromCab()\n");
        FDIDestroy ( hfdi );
        return false;
        }

    // Check that it's a valid CAB file.
    if ( !FDIIsCabinet ( hfdi, hf, &info ) )
        {
        ATLTRACE("Error: FDIIsCabinet() returned false in CDragDropSource::ExtractFilesFromCab()\n");
        cab_Close ( hf );
        FDIDestroy ( hfdi );
        return false;
        }

    cab_Close ( hf );

    // Enum the contents of the CAB.
    if ( !FDICopy ( hfdi, T2A(szCabTitle), T2A(szCabDir), 0, fdi_Notify, NULL, this ) )
        {
        ATLTRACE("Error: FDICopy() failed in CDragDropSource::ExtractFilesFromCab()\n");
        FDIDestroy ( hfdi );
        return false;
        }

    FDIDestroy ( hfdi );
    return true;
}

int DIAMONDAPI CDragDropSource::fdi_Notify (
    FDINOTIFICATIONTYPE message, PFDINOTIFICATION pInfo )
{
CDragDropSource* p = (CDragDropSource*) pInfo->pv;

    ATLASSERT(NULL != p);

    switch ( message )
        {
        case fdintPARTIAL_FILE:
            return p->Notify_PartialFile ( A2CT(pInfo->psz1), A2CT(pInfo->psz2),
                                           A2CT(pInfo->psz3) );
        break;

        case fdintCOPY_FILE:
            return p->Notify_CopyFile ( A2CT(pInfo->psz1), pInfo->cb, pInfo->date, pInfo->time,
                                        pInfo->attribs, pInfo->iFolder );
        break;

        case fdintCLOSE_FILE_INFO:
            return p->Notify_FileDone ( A2CT(pInfo->psz1), pInfo->hf, pInfo->date, pInfo->time,
                                        pInfo->attribs, pInfo->iFolder, (0 != pInfo->cb) );
        break;

        case fdintNEXT_CABINET:
            return p->Notify_NextCabinet ( A2CT(pInfo->psz1), A2CT(pInfo->psz2),
                                           A2CT(pInfo->psz3), pInfo->fdie );
        break;
        }

    return 0;
}

int CDragDropSource::Notify_PartialFile (
    LPCTSTR szFilename, LPCTSTR szStartingCabName, LPCTSTR szStartingDiskName )
{
#ifdef _DEBUG
    // See if szFilename is in m_pvecDraggedFiles. It shouldn't be, because
    // partial files aren't extractable and shouldn't be included in a drag/drop
    // operation. If we find it in the vector, it's a bug.
bool bFound = false;
std::vector<CDraggedFileInfo>::const_iterator it;

    for ( it = m_vecDraggedFiles.begin(); it != m_vecDraggedFiles.end(); it++ )
        {
        if ( 0 == it->sFilename.CompareNoCase ( szFilename ) )
            {
            bFound = true;
            break;
            }
        }

    if ( bFound )
        {
        ATLTRACE("Warning: A file (%s) continued from the previous cab (%s) was put in the drag/drop object.\n",
                 szFilename, szStartingCabName);
        ATLASSERT(0);
        }
#endif  // def _DEBUG

    return 0;   // proceed with FDICopy
}

int CDragDropSource::Notify_CopyFile (
    LPCTSTR szFilename, long cbyUncompressedSize, USHORT uDate, USHORT uTime,
    USHORT uAttribs, USHORT uFolderIdx )
{
    ATLTRACE("fdi_Notify: fdintCOPY_FILE, filename: <%s>\n", szFilename);

    m_sFileBeingExtracted = szFilename;

    // See if szFilename is in m_vecExtractedFiles. If so, open a file in the
    // temp dir and extract there. Otherwise, return 0 to skip the file.
bool bFound = false;

    for ( m_it = m_vecDraggedFiles.begin(); m_it != m_vecDraggedFiles.end(); m_it++ )
        {
        if ( 0 == m_it->sFilename.CompareNoCase ( szFilename ) )
            {
            bFound = true;
            break;
            }
        }

    if ( !bFound )
        return 0;

    ATLASSERT(m_it != m_vecDraggedFiles.end());

    // Open the temp file
CString sExtractedFilePath = m_it->sTempFilePath;
int hf, nOpenFlags = _O_BINARY|_O_TRUNC|_O_RDWR|_O_SEQUENTIAL;

    hf = cab_Open ( T2A(LPTSTR(LPCTSTR(sExtractedFilePath))), nOpenFlags, 0 );

    // Return -1 to abort FDICopy, change to 0 to just skip this file.
    return (-1 != hf) ? hf : -1;
}

int CDragDropSource::Notify_FileDone (
    LPCTSTR szFilename, int nFileHandle, USHORT uDate, USHORT uTime,
    USHORT uAttribs, USHORT uFolderIdx, bool bRunAfterExtracting )
{
CString sExtractedFilePath;

    ATLTRACE("fdi_Notify: fdintCLOSE_FILE_INFO, filename: <%s>\n", szFilename);
    ATLASSERT(m_it != m_vecDraggedFiles.end());

    cab_Close ( nFileHandle );
    sExtractedFilePath = m_it->sTempFilePath;

    // Open the extracted file again so we can set its dates/times.
HANDLE hFile = CreateFile ( sExtractedFilePath, GENERIC_WRITE, FILE_SHARE_READ,
                            NULL, OPEN_EXISTING, 0, NULL );

    if ( INVALID_HANDLE_VALUE != hFile )
        {
        FILETIME ft = {0}, ftLocal = {0};

        if ( DosDateTimeToFileTime ( uDate, uTime, &ftLocal ) &&
             LocalFileTimeToFileTime ( &ftLocal, &ft ) )
            {
            SetFileTime ( hFile, &ftLocal, &ftLocal, &ftLocal );
            }

        CloseHandle ( hFile );
        }

    // Mask off all attributes except the 4 relevant to files extracted from CABs.
    uAttribs &= _A_RDONLY | _A_HIDDEN | _A_SYSTEM | _A_ARCH;
    SetFileAttributes ( sExtractedFilePath, uAttribs );

    return TRUE;    // success
}

int CDragDropSource::Notify_NextCabinet (
    LPCTSTR szNextCabName, LPCTSTR szNextDiskName, LPCTSTR szNextCabDir, FDIERROR err )
{
    ATLTRACE("fdi_Notify: fdintNEXT_CABINET\n");
    ATLTRACE(" >> next cab name: %s\n", szNextCabName);
    ATLTRACE(" >> next disk name: %s\n", szNextDiskName);
    ATLTRACE(" >> next cab dir: %s\n", szNextCabDir);
    ATLTRACE(" >> error code: %d\n", err);

    ATLASSERT(0 == m_it->sFilename.CompareNoCase ( m_sFileBeingExtracted ));

    // The file currently being extracted is continued in another CAB, so save
    // the CAB name.
    m_it->bPartialFile = true;
    m_it->sCabName = szNextCabName;

    // If an error happened (most likely the next CAB file couldn't be found),
    // mark the file as not extractable and stop the extraction.
    if ( FDIERROR_NONE != err )
        {
        m_bAbortingDueToMissingCab = true;
        m_it->bCabMissing = true;
        return -1;  // abort FDICopy(), file is continued but that CAB isn't present
        }

    return 0;   // proceed with FDICopy
}
*/