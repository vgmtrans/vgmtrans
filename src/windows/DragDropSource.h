// DragDropSource.h: interface for the CDragDropSource class.
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_DRAGDROPSOURCE_H__93E203FE_B672_421E_A008_FE8B3BD71FFA__INCLUDED_)
#define AFX_DRAGDROPSOURCE_H__93E203FE_B672_421E_A008_FE8B3BD71FFA__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

// Drag/drop source implementation, IDataObject impl is taken from a
// technical article in MSDN:
// http://msdn.microsoft.com/library/en-us/dnwui/html/ddhelp_pt2.asp

class CDragDropSource : public CComObjectRootEx<CComSingleThreadModel>,
                        public CComCoClass<CDragDropSource>,
                        public IDataObject,
                        public IDropSource
{
public:
    // Construction
    CDragDropSource();
    ~CDragDropSource();

    // Maps
    BEGIN_COM_MAP(CDragDropSource)
        COM_INTERFACE_ENTRY(IDataObject)
        COM_INTERFACE_ENTRY(IDropSource)
    END_COM_MAP()

    // Operations
    bool Init ( /*LPCTSTR szItemName,*/ std::vector<CDraggedFileInfo>& vec );

    HRESULT DoDragDrop ( DWORD dwOKEffects, DWORD* pdwEffect );
    const std::vector<CDraggedFileInfo>& GetDragResults() const { return m_vecDraggedFiles; }

    // IDataObject
    STDMETHODIMP SetData ( FORMATETC* pformatetc, STGMEDIUM* pmedium, BOOL fRelease );
    STDMETHODIMP GetData ( FORMATETC* pformatetcIn, STGMEDIUM* pmedium );
    STDMETHODIMP EnumFormatEtc ( DWORD dwDirection, IEnumFORMATETC** ppenumFormatEtc );
    STDMETHODIMP QueryGetData ( FORMATETC* pformatetc );

    STDMETHODIMP GetDataHere(FORMATETC* pformatetc, STGMEDIUM *pmedium)
        { return E_NOTIMPL; }
    STDMETHODIMP GetCanonicalFormatEtc(FORMATETC* pformatectIn,
                                       FORMATETC* pformatetcOut)
        { return E_NOTIMPL; }
    STDMETHODIMP DAdvise(FORMATETC* pformatetc, DWORD advf,
                         IAdviseSink* pAdvSink, DWORD* pdwConnection)
        { return E_NOTIMPL; }
    STDMETHODIMP DUnadvise(DWORD dwConnection)
        { return E_NOTIMPL; }
    STDMETHODIMP EnumDAdvise(IEnumSTATDATA** ppenumAdvise)
        { return E_NOTIMPL; }

    // IDropSource
    STDMETHODIMP QueryContinueDrag ( BOOL fEscapePressed, DWORD grfKeyState );
    STDMETHODIMP GiveFeedback ( DWORD dwEffect );

    // Implementation
protected:
    bool  m_bInitialized;
    DWORD m_dwLastEffect;

    std::vector<CDraggedFileInfo> m_vecDraggedFiles;
    //CString m_sItemName;//m_sCabFilePath;
    //CString m_sFileBeingExtracted;
    std::vector<CDraggedFileInfo>::iterator m_it;   // itr on file currently being extracted
    //bool m_bAbortingDueToMissingCab;

    // List of FORMATETCs for which we have data, used in EnumFormatEtc
    std::vector<FORMATETC> m_vecSupportedFormats;

    typedef struct 
    {
        FORMATETC fe;
        STGMEDIUM stgm;
    } DATAENTRY, *LPDATAENTRY;

    LPDATAENTRY m_rgde;                 // Array of active DATAENTRY entries
    int m_cde;                          // Size of m_rgde

    // Helper functions used by IDataObject methods
    HRESULT   FindFORMATETC ( FORMATETC* pfe, LPDATAENTRY* ppde, BOOL fAdd );
    HRESULT   AddRefStgMedium ( STGMEDIUM* pstgmIn, STGMEDIUM* pstgmOut, BOOL fCopyIn );
    HGLOBAL   GlobalClone ( HGLOBAL hglobIn );
    IUnknown* GetCanonicalIUnknown ( IUnknown* punk );

    // CAB extraction/callback methods
    //bool ExtractFilesFromCab();

    /*static int DIAMONDAPI fdi_Notify ( FDINOTIFICATIONTYPE message, PFDINOTIFICATION pInfo );
    int Notify_PartialFile ( LPCTSTR szFilename, LPCTSTR szStartingCabName, LPCTSTR szStartingDiskName );
    int Notify_CopyFile ( LPCTSTR szFilename, long cbyUncompressedSize, USHORT uDate, USHORT uTime,
                          USHORT uAttribs, USHORT uFolderIdx );
    int Notify_FileDone ( LPCTSTR szFilename, int nFileHandle, USHORT uDate, USHORT uTime,
                          USHORT uAttribs, USHORT uFolderIdx, bool bRunAfterExtracting );
    int Notify_NextCabinet ( LPCTSTR szNextCabName, LPCTSTR szNextDiskName, LPCTSTR szNextCabDir, FDIERROR err );*/
};

#endif // !defined(AFX_DRAGDROPSOURCE_H__93E203FE_B672_421E_A008_FE8B3BD71FFA__INCLUDED_)
