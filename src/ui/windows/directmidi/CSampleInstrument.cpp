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
Module : CSampleInstrument.cpp
Purpose: Code implementation for the CSampleInstrument class
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

	25. New methods added to the classes

	26. Fixed small bugs

  Update: 20/02/2004

	27. Added new DirectMusic classes for Audio handling

	28. Improved CMidiPort class with internal buffer run-time resizing

	29. 3D audio positioning


*/
#include "pch.h"
#include "CMidiPart.h"

using namespace directmidi;

// Class constructor, member variables initialization

CSampleInstrument::CSampleInstrument()
{

	m_pArticulationDownload = NULL;
	m_pWaveDownload		= NULL;
	m_pPortDownload		= NULL;
	m_bReadAlways		= FALSE;
	m_bWaveFormSet		= FALSE;
	m_bFileRead			= FALSE;
	m_bLoop				= FALSE;
	m_pRawData			= NULL;
	m_dwSize			= 0;
	m_dwPatch			= 0;
	m_lAttenuation		= 0;
	m_sFineTune			= 0;
	m_usUnityNote		= 0;
	m_fulOptions		= 0;
	ZeroMemory(&m_Region,sizeof(REGION));
	ZeroMemory(&m_Articparams,sizeof(ARTICPARAMS));
}

// Destructor. Releases the sample data

CSampleInstrument::~CSampleInstrument()
{
	ReleaseSample();
}

// Releases internal interfaces and frees allocated memory and resources

HRESULT CSampleInstrument::ReleaseSample()
{

	// Unloads data if present in the port

	if ((m_pPortDownload) && (m_pWaveDownload))
		m_pPortDownload->Unload(m_pWaveDownload);	
		
	if ((m_pPortDownload) && (m_pArticulationDownload))
		m_pPortDownload->Unload(m_pArticulationDownload);
				
	SAFE_RELEASE(m_pWaveDownload);
	SAFE_RELEASE(m_pArticulationDownload);
	if ((!m_bWaveFormSet) && (!m_bReadAlways)) 
		SAFE_DELETE_ARRAY(m_pRawData); // Must be a file loaded in memory!
	
	m_bReadAlways  = FALSE;
	m_bFileRead	   = FALSE;
	m_bWaveFormSet = FALSE;
	m_bLoop		   = FALSE;	
	m_dwSize	   = 0;
	m_dwPatch	   = 0;
	m_lAttenuation = 0;
	m_sFineTune    = 0;
	m_usUnityNote  = 0;
	m_fulOptions   = 0;
	ZeroMemory(&m_Region,sizeof(REGION));
	ZeroMemory(&m_Articparams,sizeof(ARTICPARAMS));
	  
	return S_OK;
}


// Allocates memory for instrument data download

HRESULT CSampleInstrument::AllocateInstBuffer()
{
	HRESULT hr;
	TCHAR strMembrFunc[] = _T("CSampleInstrument::AllocateInstBuffer()");

	if (FAILED(hr = m_pPortDownload->AllocateBuffer(sizeof(INSTRUMENT_DOWNLOAD),
		&m_pArticulationDownload)))	throw CDMusicException(strMembrFunc,hr,__LINE__);
	
	return S_OK;
}

// Allocates memory for wave data download

HRESULT CSampleInstrument::AllocateWaveBuffer(DWORD dwAppend)
{
	HRESULT hr;
	TCHAR strMembrFunc[] = _T("CSampleInstrument::AllocateWaveBuffer()");
	
	if (FAILED(hr = m_pPortDownload->AllocateBuffer(sizeof(WAVE_DOWNLOAD)+
		dwAppend*m_wfex.nBlockAlign+m_dwSize,&m_pWaveDownload)))	
		throw CDMusicException(strMembrFunc,hr,__LINE__);
	
	return S_OK;
}


// Fills the buffer with the wave data

HRESULT CSampleInstrument::GetWaveBuffer(DWORD dwDLId)
{
	HRESULT hr;
	TCHAR strMembrFunc[] = _T("CSampleInstrument::GetWaveBuffer()");
	
	void *pvData = NULL;
    DWORD dwSize = 0;
    
	// Obtains a pointer to the buffer

	if (FAILED(hr = m_pWaveDownload->GetBuffer(&pvData,&dwSize)))
		throw CDMusicException(strMembrFunc,hr,__LINE__);
    
	// Fills required DLS1.0 data
	
	InitializeWaveDownload((WAVE_DOWNLOAD*)pvData,dwDLId,&m_wfex,m_dwSize,dwSize);

	
	if (m_bReadAlways)	// The user always wants to read the file from disk
	{
	
		CWaveFile* pWaveFile = new CWaveFile();
	
		if (pWaveFile == NULL)
			throw CDMusicException(strMembrFunc,E_OUTOFMEMORY,__LINE__);
	

		if (FAILED(hr = pWaveFile->Open(m_lpstrFileName,&m_wfex,WAVEFILE_READ)))
		{
			SAFE_DELETE(pWaveFile);
			throw CDMusicException(strMembrFunc,hr,__LINE__);
		}

		// Reads the .WAV file	
	
		DWORD dwRead;

 		if (FAILED(hr = pWaveFile->Read(((WAVE_DOWNLOAD*)pvData)->dmWaveData.byData,pWaveFile->GetSize(),&dwRead)))
		{
			SAFE_DELETE(pWaveFile);
			throw CDMusicException(strMembrFunc,hr,__LINE__);
		}
	
		SAFE_DELETE(pWaveFile);

	} else  
	{	
			
		if (m_pRawData == NULL)
			throw CDMusicException(strMembrFunc,DM_FAILED,__LINE__);

		// Copy the sample data to the internal memory 

		CopyMemory(((WAVE_DOWNLOAD*)pvData)->dmWaveData.byData,m_pRawData,m_dwSize);
	}

		
	return S_OK;

}
	

// Fills the DLS instrument data buffer

HRESULT CSampleInstrument::GetInstBuffer(DWORD dwDLId)
{
	HRESULT hr;
	TCHAR strMembrFunc[] = _T("CSampleInstrument::GetInstBuffer()");
	
	void *pvData = NULL;
    DWORD dwSize = 0;
    	
	if (FAILED(hr = m_pArticulationDownload->GetBuffer(&pvData,&dwSize)))
		throw CDMusicException(strMembrFunc,hr,__LINE__);
	
	InitializeInstDownload((INSTRUMENT_DOWNLOAD*)pvData,dwDLId + 1,dwDLId);

	return S_OK;
}
	

// Fills the DLS1.0 instrument data structures

BOOL CSampleInstrument::InitializeInstDownload(CSampleInstrument::INSTRUMENT_DOWNLOAD *pInstDownload,DWORD dwDLId,DWORD dwDLIdWave)
{
	ZeroMemory(pInstDownload,sizeof(INSTRUMENT_DOWNLOAD));
	
	CopyMemory(&pInstDownload->dmRegion,&m_Region,sizeof(REGION));
	CopyMemory(&pInstDownload->dmArticParams,&m_Articparams,sizeof(ARTICPARAMS));

	pInstDownload->dmInstrument.ulPatch  = m_dwPatch;
	pInstDownload->dlInfo.dwDLType = DMUS_DOWNLOADINFO_INSTRUMENT;
	pInstDownload->dlInfo.cbSize   = sizeof(INSTRUMENT_DOWNLOAD);
	pInstDownload->dlInfo.dwDLId = dwDLId;

	pInstDownload->dlInfo.dwNumOffsetTableEntries = 4;
	pInstDownload->ulOffsetTable[0] = offsetof(INSTRUMENT_DOWNLOAD,dmInstrument);
	pInstDownload->ulOffsetTable[1] = offsetof(INSTRUMENT_DOWNLOAD,dmRegion);
	pInstDownload->ulOffsetTable[2] = offsetof(INSTRUMENT_DOWNLOAD,dmArticulation);
	pInstDownload->ulOffsetTable[3] = offsetof(INSTRUMENT_DOWNLOAD,dmArticParams);

	pInstDownload->dmInstrument.ulFirstRegionIdx = 1;
	pInstDownload->dmInstrument.ulGlobalArtIdx = 2;
	
	pInstDownload->dmRegion.fusOptions = F_RGN_OPTION_SELFNONEXCLUSIVE;
	pInstDownload->dmRegion.WaveLink.ulChannel = 1;
	pInstDownload->dmRegion.WaveLink.ulTableIndex = dwDLIdWave;
	pInstDownload->dmRegion.WaveLink.usPhaseGroup = 0;
    pInstDownload->dmRegion.WSMP.cbSize = sizeof(WSMPL);
    pInstDownload->dmRegion.WSMP.fulOptions = m_fulOptions;
    pInstDownload->dmRegion.WSMP.usUnityNote = m_usUnityNote; 
    pInstDownload->dmRegion.WSMP.sFineTune = m_sFineTune; 
	pInstDownload->dmRegion.WSMP.lAttenuation = m_lAttenuation; 
	
	if (m_bLoop) 
	{
		pInstDownload->dmRegion.WSMP.cSampleLoops = 1;
		pInstDownload->dmRegion.WLOOP[0].ulStart = 0;
		pInstDownload->dmRegion.WLOOP[0].ulLength = m_dwSize/m_wfex.nBlockAlign;
	}

	pInstDownload->dmRegion.WLOOP[0].cbSize = sizeof(WLOOP);
    pInstDownload->dmRegion.WLOOP[0].ulType = WLOOP_TYPE_FORWARD;
    pInstDownload->dmArticulation.ulArt1Idx = 3;
		
	return TRUE;
}

// Fills the DLS1.0 structures with the wave data

BOOL CSampleInstrument::InitializeWaveDownload(CSampleInstrument::WAVE_DOWNLOAD *pWaveDownload,DWORD dwDLId,WAVEFORMATEX *pwfex,DWORD dwWaveSize,DWORD dwOverallSize)
{
	ZeroMemory(pWaveDownload,sizeof(WAVE_DOWNLOAD));
    
	pWaveDownload->dlInfo.dwDLType = DMUS_DOWNLOADINFO_WAVE;
    pWaveDownload->dlInfo.cbSize = dwOverallSize;
    pWaveDownload->dlInfo.dwDLId = dwDLId;
    pWaveDownload->dlInfo.dwNumOffsetTableEntries = 2;

    pWaveDownload->ulOffsetTable[0] = offsetof(WAVE_DOWNLOAD,dmWave);
    pWaveDownload->ulOffsetTable[1] = offsetof(WAVE_DOWNLOAD,dmWaveData);

    pWaveDownload->dmWave.ulWaveDataIdx = 1;

    CopyMemory(&pWaveDownload->dmWave.WaveformatEx, pwfex, sizeof(WAVEFORMATEX));

    pWaveDownload->dmWaveData.cbSize = dwWaveSize;
	return TRUE;

}

// Sets the destination MIDI patch

void CSampleInstrument::SetPatch(DWORD dwPatch)
{
	m_dwPatch = dwPatch;
}

// External user waveform supplied

void CSampleInstrument::SetWaveForm(BYTE *pRawData,WAVEFORMATEX *pwfex,DWORD dwSize)
{
	
	HRESULT hr = DM_FAILED;

	TCHAR strMembrFunc[] = _T("CSampleInstrument::SetWaveForm()");

	// Check parameters

	if ((pRawData == NULL) || (pwfex == NULL))
		throw CDMusicException(strMembrFunc,hr,__LINE__);

	if ((!m_bWaveFormSet) && (!m_bReadAlways)) 
		SAFE_DELETE_ARRAY(m_pRawData); // In case there is a file loaded in memory

	m_pRawData = pRawData;
	CopyMemory(&m_wfex,pwfex,sizeof(WAVEFORMATEX));
	m_bWaveFormSet = TRUE;
	m_bReadAlways  = FALSE;
	m_dwSize   = dwSize;
}

// Returns the internal waveform data in the current object 

void CSampleInstrument::GetWaveForm(BYTE **pRawData,WAVEFORMATEX *pwfex,DWORD *dwSize)
{
	
	HRESULT hr = DM_FAILED;

	TCHAR strMembrFunc[] = _T("CSampleInstrument::GetWaveForm()");

	if ((pwfex == NULL) || (dwSize == NULL))
		throw CDMusicException(strMembrFunc,hr,__LINE__);

	*pRawData = m_pRawData;
 	CopyMemory(pwfex,&m_wfex,sizeof(WAVEFORMATEX));
	*dwSize  = m_dwSize;
}

// Sets instrument region parameters

void CSampleInstrument::SetRegion(REGION *pRegion)
{
	HRESULT hr = DM_FAILED;
	
	TCHAR strMembrFunc[] = _T("CSampleInstrument::SetRegion()");

	if (pRegion == NULL)
		throw CDMusicException(strMembrFunc,hr,__LINE__);

	CopyMemory(&m_Region,pRegion,sizeof(REGION));
}

// Sets instrument articulation parameters

void CSampleInstrument::SetArticulationParams(ARTICPARAMS *pArticParams)
{
	HRESULT hr = DM_FAILED;
	
	TCHAR strMembrFunc[] = _T("CSampleInstrument::SetArticulationParams()");

	if (pArticParams == NULL)
		throw CDMusicException(strMembrFunc,hr,__LINE__);

	CopyMemory(&m_Articparams,pArticParams,sizeof(ARTICPARAMS));
}

// Returns instrument region data

void CSampleInstrument::GetRegion(REGION *pRegion)
{
	HRESULT hr = DM_FAILED;
	
	TCHAR strMembrFunc[] = _T("CSampleInstrument::GetRegion()");

	if (pRegion == NULL)
		throw CDMusicException(strMembrFunc,hr,__LINE__);

	CopyMemory(pRegion,&m_Region,sizeof(REGION));
}

// Returns instrument articulation data

void CSampleInstrument::GetArticulationParams(ARTICPARAMS *pArticParams)
{
	HRESULT hr = DM_FAILED;
	
	TCHAR strMembrFunc[] = _T("CSampleInstrument::SetArticulationParams()");

	if (pArticParams == NULL)
		throw CDMusicException(strMembrFunc,hr,__LINE__);

	CopyMemory(pArticParams,&m_Articparams,sizeof(ARTICPARAMS));
}

// Return the internal waveform size

DWORD CSampleInstrument::GetWaveFormSize()
{
	return m_dwSize;
}


// Activates or deactivates the sample loop

void CSampleInstrument::SetLoop(BOOL bLoop)
{
	m_bLoop = bLoop;
}

// Sets additional wave parameters

void CSampleInstrument::SetWaveParams(LONG lAttenuation,SHORT sFineTune,USHORT usUnityNote,ULONG fulOptions)
{
	m_lAttenuation = lAttenuation;
	m_sFineTune	   = sFineTune;
	m_usUnityNote  = usUnityNote;
	m_fulOptions   = fulOptions;
}

void CSampleInstrument::GetWaveParams(LONG *plAttenuation,SHORT *psFineTune,USHORT *pusUnityNote,ULONG *pfulOptions)
{
	*plAttenuation = m_lAttenuation;
	*psFineTune	   = m_sFineTune;
	*pusUnityNote  = m_usUnityNote;
	*pfulOptions   = m_fulOptions;
}


