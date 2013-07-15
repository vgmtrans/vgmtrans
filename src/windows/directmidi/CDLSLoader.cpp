/*
 ____   _  ____  _____ _____ ______  __  __  _  ____   _
|  _ \ | ||  _ \|  __//  __//_   _/ |  \/  || ||  _ \ | |
| |_| || || |> /|  _| | <__   | |   | |\/| || || |_| || | 
|____/ |_||_|\_\|____\\____/  |_|   |_|  |_||_||____/ |_|			 

////////////////////////////////////////////////////////////////////////
  
  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.
 
  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.
 
  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 
 
Copyright (c) 2002-2004 by Carlos Jiménez de Parga  
All rights reserved.
For any suggestion or failure report, please contact me by:
e-mail: cjimenez@isometrica.net

Note: I would appreciate very much if you could provide a brief description of your project, 
as well as any report on bugs or errors. This will help me improve the code for the benefit of your
application and the other users    
///////////////////////////////////////////////////////////////////////
Version: 2.3b
Module : CDLSLoader.cpp
Purpose: Code implementation for the CDLSLoader class
Created: CJP / 02-08-2002
History: CJP / 20-02-2004 
Date format: Day-month-year
	
	Update: 20/09/2002

	1. Improved class destructors

	2. Check member variables for NULL values before performing an operation
		
	3. Restructured the class system
	
	4. Better method to enumerate and select MIDI ports
	
	5. Adapted the external thread to a pure virtual function

	6. SysEx reception and sending enabled

	7. Added flexible conversion functions

	8. Find out how to activate the Microsoft software synth. 

	9. Added DLS support

  Update: 25/03/2003

	10. Added exception handling

    11. Fixed bugs with channel groups
  
    12. Redesigned class hierarchy
	
    13. DirectMidi wrapped in the directmidi namespace
  
    14. UNICODE/MBCS support for instruments and ports

	15. Added DELAY effect to software synthesizer ports
	
	16. Project released under GNU (General Public License) terms		 

  Update: 03/10/2003	

	17. Redesigned class interfaces

	18. Safer input port termination thread

	19. New CMasterClock class

	20. DLS files can be loaded from resources

	21. DLS instruments note range support

	22. New CSampleInstrument class added to the library

	23. Direct download of samples to wave-table memory
	
	24. .WAV file sample format supported	

	25. New methods added to the output port class

	26. Fixed small bugs

  Update: 20/02/2004

	27. Added new DirectMusic classes for Audio handling

	28. Improved CMidiPort class with internal buffer run-time resizing

	29. 3D audio positioning

	
*/
#include "stdafx.h"
#include "CMidiPart.h"
#include "CAudioPart.h"

#include "MyCoCreateInstance.h"

using namespace directmidi;


// The constructor of the class

CDLSLoader::CDLSLoader()
{
	m_pLoader		= NULL;
}

// Class destructor 
// Clear the references to a collection in the loader

CDLSLoader::~CDLSLoader()
{
	if (m_pLoader)
	{	
		m_pLoader->ClearCache(GUID_DirectMusicAllTypes);
		// Remove from the cache objects that are no longer in use
		m_pLoader->CollectGarbage();
	}

	SAFE_RELEASE(m_pLoader);
}	

// Initialize the object, create the instance of DirectMusicLoader object

HRESULT CDLSLoader::Initialize() 
{
	HRESULT hr;
	TCHAR strMembrFunc[] = _T("CDLSLoader::Initialize()");
	
	//if (FAILED(hr = CoCreateInstance(CLSID_DirectMusicLoader,NULL,
	//    CLSCTX_INPROC_SERVER,IID_IDirectMusicLoader8,(LPVOID*)&m_pLoader))) 
	//		 throw CDMusicException(strMembrFunc,hr,__LINE__); 
	
	hr = CoCreateInstance(CLSID_DirectMusicLoader,NULL,
		CLSCTX_INPROC_SERVER,IID_IDirectMusicLoader8,(LPVOID*)&m_pLoader);
	if (hr != S_OK)
		hr = MyCoCreateInstance(_T("dmloader.dll"),
		  CLSID_DirectMusicLoader,NULL,IID_IDirectMusicLoader8,(LPVOID*)&m_pLoader);
	if (FAILED(hr))
		throw CDMusicException(strMembrFunc,hr,__LINE__);


	return S_OK;
}


// Loads a DLS file and initializes a Collection object

HRESULT CDLSLoader::LoadDLS(LPTSTR lpFileName,CCollection &Collection)
{
	HRESULT hr = DM_FAILED;
	DMUS_OBJECTDESC dmusdesc;
	TCHAR strMembrFunc[] = _T("CDLSLoader::LoadDLS()");

	// Sets to 0 the DMUS structure
	
	ZeroMemory(&dmusdesc,sizeof(DMUS_OBJECTDESC));
	dmusdesc.dwSize = sizeof(DMUS_OBJECTDESC);
	dmusdesc.guidClass = CLSID_DirectMusicCollection; 

	if (lpFileName == NULL)	// Load the synthesizer default GM set
	{
		dmusdesc.guidObject	= GUID_DefaultGMCollection;
		dmusdesc.dwValidData = DMUS_OBJ_CLASS | DMUS_OBJ_OBJECT;
	}
	else
	{
		#ifdef _UNICODE
			if (wcslen(lpFileName) <= DMUS_MAX_FILENAME)	
				_tcscpy(dmusdesc.wszFileName,lpFileName);
			else throw CDMusicException(strMembrFunc,hr,__LINE__);	
		#else
			mbstowcs(dmusdesc.wszFileName,lpFileName,DMUS_MAX_FILENAME); // Convert to wide-char
		#endif

		dmusdesc.dwValidData = DMUS_OBJ_CLASS | DMUS_OBJ_FILENAME | DMUS_OBJ_FULLPATH;
	}

	UnloadCollection(Collection);	// In case the object is already with data

	if (m_pLoader)	// Loads the DLS file
	{
		if (FAILED(hr = m_pLoader->GetObject(&dmusdesc,IID_IDirectMusicCollection,(void**)&Collection.m_pCollection)))
			throw CDMusicException(strMembrFunc,hr,__LINE__);
	} else throw CDMusicException(strMembrFunc,hr,__LINE__);

	return S_OK;

}


// Loads a DLS file from memory initializes a Collection object
// Added for VGMTrans

HRESULT CDLSLoader::LoadDLSFromMem(LPBYTE pDLSFile, LONGLONG llDLSSize, CCollection &Collection)
{
	HRESULT hr = DM_FAILED;
	DMUS_OBJECTDESC dmusdesc;
	TCHAR strMembrFunc[] = _T("CDLSLoader::LoadDLSFromMem()");

	// Sets to 0 the DMUS structure
	
	ZeroMemory(&dmusdesc,sizeof(DMUS_OBJECTDESC));
	dmusdesc.dwSize = sizeof(DMUS_OBJECTDESC);
	dmusdesc.guidClass = CLSID_DirectMusicCollection; 
	dmusdesc.llMemLength = llDLSSize;
	dmusdesc.pbMemData = pDLSFile;
	
	_tcscpy(dmusdesc.wszName, _T("DLS File"));
	//dmusdesc.guidObject	= GUID_DefaultGMCollection;
	//dmusdesc.dwValidData = DMUS_OBJ_CLASS | DMUS_OBJ_OBJECT;

	dmusdesc.dwValidData = DMUS_OBJ_CLASS | DMUS_OBJ_MEMORY;

	UnloadCollection(Collection);	// In case the object is already with data

	if (m_pLoader)	// Loads the DLS file
	{
		if (FAILED(hr = m_pLoader->GetObject(&dmusdesc,IID_IDirectMusicCollection,(void**)&Collection.m_pCollection)))
			throw CDMusicException(strMembrFunc,hr,__LINE__);
	} else throw CDMusicException(strMembrFunc,hr,__LINE__);

	return S_OK;

}

// Loads a DLS from a resource and returns a CCollection object

HRESULT CDLSLoader::LoadDLSFromResource(LPTSTR strResource,LPTSTR strResourceType,CCollection &Collection)
{
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("CDLSLoader::LoadDLSFromResource()");

    HRSRC hres  = NULL;
    LPVOID pMem = NULL;
    DMUS_OBJECTDESC dmusdesc;
	DWORD dwSize= 0;
    

    // Finds the resource
    if ((hres = FindResource(NULL,strResource,strResourceType)) == NULL)
		throw CDMusicException(strMembrFunc,E_FAIL,__LINE__);

    // Loads the resource
    if ((pMem = (LPVOID)LoadResource(NULL,hres)) == NULL)
		throw CDMusicException(strMembrFunc,E_FAIL,__LINE__);
    
	// Stores the size of the resource
    dwSize = SizeofResource(NULL,hres); 
    
    // Sets up our object description 
    ZeroMemory(&dmusdesc,sizeof(DMUS_OBJECTDESC));
    dmusdesc.dwSize = sizeof(DMUS_OBJECTDESC);
    dmusdesc.dwValidData = DMUS_OBJ_MEMORY | DMUS_OBJ_CLASS;
    dmusdesc.guidClass = CLSID_DirectMusicCollection;
    dmusdesc.llMemLength =(LONGLONG)dwSize;
    dmusdesc.pbMemData = (BYTE*)pMem;
    
    UnloadCollection(Collection);	// In case the object is already with data

	if (m_pLoader)	// Loads the DLS resource
	{
		if (FAILED(hr = m_pLoader->GetObject(&dmusdesc,IID_IDirectMusicCollection,(void**)&Collection.m_pCollection)))
			throw CDMusicException(strMembrFunc,hr,__LINE__);
	} else throw CDMusicException(strMembrFunc,hr,__LINE__);
    
	return S_OK;
}


// Loads a segment and returns a CSegment object

HRESULT CDLSLoader::LoadSegment(LPTSTR lpstrFileName,CSegment &Segment,BOOL bIsMIDIFile)
{
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("CDLSLoader::LoadSegment()");
	
	WCHAR wstrFileName[MAX_PATH];
	
	// Releases the old segment
	
	Segment.ReleaseSegment();
	
	
	#ifdef _UNICODE
		if (wcslen(lpstrFileName) <= MAX_PATH)	
			_tcscpy(wstrFileName,lpstrFileName);
		else throw CDMusicException(strMembrFunc,hr,__LINE__);	
	#else
		mbstowcs(wstrFileName,lpstrFileName,MAX_PATH); // Convert to wide-char
	#endif
	  	
	// Loads the segment and specifies if it's a MIDI file or not
	
	if (m_pLoader)
	{
		if (FAILED(hr = m_pLoader->LoadObjectFromFile(CLSID_DirectMusicSegment,
        IID_IDirectMusicSegment8,wstrFileName,(void**)&Segment.m_pSegment)))
			throw CDMusicException(strMembrFunc,hr,__LINE__);
	
		if(bIsMIDIFile )
		{
			if(FAILED(hr = Segment.m_pSegment->SetParam(GUID_StandardMIDIFile,0xFFFFFFFF,0,0,NULL)))
				throw CDMusicException(strMembrFunc,hr,__LINE__);
		}
	} else throw CDMusicException(strMembrFunc,hr,__LINE__);
	
	return hr;
}


HRESULT CDLSLoader::LoadSegmentFromResource(TCHAR *strResource,TCHAR *strResourceType,CSegment &Segment,BOOL bIsMidiFile)
{
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("CDLSLoader::LoadSegmentFromResource()");
	
	HRSRC hres = NULL;
    void* pMem = NULL;
    DWORD dwSize = 0;
    
	DMUS_OBJECTDESC objdesc;
	
	Segment.ReleaseSegment();

	// Find the resource
    if ((hres = FindResource(NULL,strResource,strResourceType)) == NULL)
		throw CDMusicException(strMembrFunc,E_FAIL,__LINE__);
	
    // Load the resource
    if ((pMem = (void*)LoadResource(NULL,hres)) == NULL)
		throw CDMusicException(strMembrFunc,E_FAIL,__LINE__);
    	
    // Store the size of the resource
    dwSize = SizeofResource(NULL,hres); 
    
    // Set up our object description 
    ZeroMemory(&objdesc,sizeof(DMUS_OBJECTDESC));
    objdesc.dwSize = sizeof(DMUS_OBJECTDESC);
    objdesc.dwValidData = DMUS_OBJ_MEMORY | DMUS_OBJ_CLASS;
    objdesc.guidClass = CLSID_DirectMusicSegment;
    objdesc.llMemLength = (LONGLONG)dwSize;
    objdesc.pbMemData = (BYTE*)pMem;
    
	// Loads the segment and specifies if it's a MIDI file or not
    
	if (m_pLoader)
	{
		if (FAILED(hr = m_pLoader->GetObject(&objdesc,IID_IDirectMusicSegment8,(void**)&Segment.m_pSegment)))
			throw CDMusicException(strMembrFunc,E_FAIL,__LINE__);
	
		if (bIsMidiFile)
		{
			if(FAILED(hr = Segment.m_pSegment->SetParam(GUID_StandardMIDIFile,0xFFFFFFFF,0,0,NULL)))
				throw CDMusicException(strMembrFunc,E_FAIL,__LINE__);
		}
	} else throw CDMusicException(strMembrFunc,hr,__LINE__);
	
	
	return S_OK;
}

// Loads a wave file

HRESULT CDLSLoader::LoadWaveFile(LPTSTR lpstrFileName,CSampleInstrument &SampleInstrument,BOOL bReadAlways)
{
	HRESULT hr;
	
	TCHAR strMembrFunc[] = _T("CDLSLoader::LoadWaveFile()");
	
	// Checks if it exists

	SampleInstrument.m_bReadAlways = bReadAlways;
	
	if (SampleInstrument.m_bWaveFormSet)
			SampleInstrument.m_pRawData = NULL;
	
	
	CWaveFile* pWaveFile = new CWaveFile();
	
	if (pWaveFile == NULL)
		throw CDMusicException(strMembrFunc,E_OUTOFMEMORY,__LINE__);

	if (FAILED(hr = pWaveFile->Open(lpstrFileName,&SampleInstrument.m_wfex,WAVEFILE_READ)))
	{
		SAFE_DELETE(pWaveFile);
		throw CDMusicException(strMembrFunc,hr,__LINE__);
	}

	// Gets the format and size
	
	SampleInstrument.m_dwSize = pWaveFile->GetSize();
	CopyMemory(&SampleInstrument.m_wfex,pWaveFile->GetFormat(),sizeof(WAVEFORMATEX));

	if (!bReadAlways)
	{
		SAFE_DELETE_ARRAY(SampleInstrument.m_pRawData);
		SampleInstrument.m_pRawData = new BYTE[SampleInstrument.m_dwSize];

		DWORD dwRead;
		
		if (FAILED(hr = pWaveFile->Read(SampleInstrument.m_pRawData,pWaveFile->GetSize(),&dwRead)))
		{	
			
			SAFE_DELETE(pWaveFile);
			SAFE_DELETE_ARRAY(SampleInstrument.m_pRawData);
			throw CDMusicException(strMembrFunc,hr,__LINE__);
		}
		
	} else
		_tcscpy(SampleInstrument.m_lpstrFileName,lpstrFileName);

	
	SampleInstrument.m_bWaveFormSet = FALSE;
	SampleInstrument.m_bFileRead = TRUE;

	SAFE_DELETE(pWaveFile);

	return S_OK;
}


// Sets the start up search directory

HRESULT CDLSLoader::SetSearchDirectory(LPTSTR pszPath,BOOL fClear)
{
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("CDLSLoader::SetSearchDirectory()");
	WCHAR wszPath[_MAX_PATH];

	if (m_pLoader)
	{	
		#ifdef _UNICODE
			if (wcslen(pszPath) <= _MAX_PATH)	
				_tcscpy(wszPath,pszPath);
			else throw CDMusicException(strMembrFunc,hr,__LINE__);
		#else
			mbstowcs(wszPath,pszPath,_MAX_PATH); // Convert to wide-char
		#endif
		
		if (FAILED(hr = m_pLoader->SetSearchDirectory(GUID_DirectMusicAllTypes,wszPath,fClear)))
			throw CDMusicException(strMembrFunc,hr,__LINE__);
		
	} else throw CDMusicException(strMembrFunc,hr,__LINE__);

	return S_OK;
}

// Unload a collection from the loader

HRESULT CDLSLoader::UnloadCollection(CCollection &Collection)
{
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("CDLSLoader::UnloadCollection()");
	
	if ((m_pLoader) && (Collection.m_pCollection))
	{
		if (FAILED(hr = m_pLoader->ReleaseObjectByUnknown(Collection.m_pCollection)))
			throw CDMusicException(strMembrFunc,hr,__LINE__);
		SAFE_RELEASE(Collection.m_pCollection);
	} else return hr;	
	return S_OK;
}


