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
Module : COutputPort.cpp
Purpose: Code implementation for the COutput port class
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
#include "pch.h"
#include "CMidiPart.h"

using namespace directmidi;

// Class constructor, member variables initialization

COutputPort::COutputPort():CMidiPort(DMUS_PC_OUTPUTCLASS)
{
	m_pClock		= NULL;
	m_pPortDownload = NULL;
}	


// Class destructor
// Release all the interfaces

COutputPort::~COutputPort()
{
	ReleasePort();
}


// Function to select and activate an Input MIDI port given an INFOPORT structure

HRESULT COutputPort::ActivatePort(LPINFOPORT InfoPort,DWORD dwSysExSize)
{
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("COutputPort::ActivatePort()");
	
	if (m_pClock) throw CDMusicException(strMembrFunc,hr,__LINE__); 

	CMidiPort::ActivatePort(InfoPort,dwSysExSize,FALSE);
	
	if (FAILED(hr = m_pPort->GetLatencyClock(&m_pClock))) throw CDMusicException(strMembrFunc,hr,__LINE__); 
	
	m_pPort->QueryInterface(IID_IDirectMusicPortDownload8,(void**)&m_pPortDownload);
	
	return S_OK;
}

// Initializes a COutputPort from a valid IDirectMusicPort8 interface

HRESULT COutputPort::ActivatePortFromInterface(IDirectMusicPort8* pPort,DWORD dwSysExSize)
{
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("COutputPort::ActivatePortFromInterface()");
	
	if (pPort==NULL) throw CDMusicException(strMembrFunc,hr,__LINE__); 

	m_pPort = pPort;

	CMidiPort::ActivatePort(NULL,dwSysExSize,TRUE);

	if (FAILED(hr = m_pPort->GetLatencyClock(&m_pClock))) throw CDMusicException(strMembrFunc,hr,__LINE__); 
	
	m_pPort->QueryInterface(IID_IDirectMusicPortDownload8,(void**)&m_pPortDownload);

	return S_OK;
}


// Sets the parameters to activate port features

void COutputPort::SetPortParams(DWORD dwVoices,DWORD dwAudioChannels,DWORD dwChannelGroups,DWORD dwEffectFlags,DWORD dwSampleRate)
{
	m_PortParams.dwValidParams  = 0;

	if (dwVoices)		 m_PortParams.dwValidParams  = DMUS_PORTPARAMS_VOICES;
	if (dwAudioChannels) m_PortParams.dwValidParams |= DMUS_PORTPARAMS_AUDIOCHANNELS;
	if (dwChannelGroups) m_PortParams.dwValidParams |= DMUS_PORTPARAMS_CHANNELGROUPS;
	if (dwEffectFlags)   m_PortParams.dwValidParams |= DMUS_PORTPARAMS_EFFECTS; 
	if (dwSampleRate)    m_PortParams.dwValidParams |= DMUS_PORTPARAMS_SAMPLERATE;
	
	m_PortParams.dwVoices		 = dwVoices;
	m_PortParams.dwAudioChannels = dwAudioChannels;
	m_PortParams.dwChannelGroups = dwChannelGroups;
	m_PortParams.dwEffectFlags   = dwEffectFlags;
	m_PortParams.dwSampleRate    = dwSampleRate;
}

	
// This function removes the selected port 
// and releases all their asociated interfaces for a new port selection

HRESULT COutputPort::ReleasePort()
{
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("COutputPort::ReleasePort()");
	
	if (m_pPort)
		hr = m_pPort->Activate(FALSE); 
	
	SAFE_RELEASE(m_pPort);
	SAFE_RELEASE(m_pBuffer);
	SAFE_RELEASE(m_pClock);
	SAFE_RELEASE(m_pPortDownload);

	return hr;
}


// Download an instrument to the port wave-table memory

HRESULT COutputPort::DownloadInstrument(CInstrument &Instrument)
{
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("COutputPort::DownloadInstrument(CInstrument)");
	
	if ((m_pPort) && (Instrument.m_pInstrument))
	{	
		
		// Download the instrument to the port and keep a reference to
		// this instrument in the port with the m_pDownloadedInstrument

		if (FAILED(hr = m_pPort->DownloadInstrument(Instrument.m_pInstrument,
			&Instrument.m_pDownLoadedInstrument,&Instrument.m_NoteRange,1))) 
				throw CDMusicException(strMembrFunc,hr,__LINE__);
		Instrument.m_pPort = m_pPort;	// Updates the Outputport reference 
	} else return hr; 
	
	return S_OK;
}


// Allocates memory to download DLS data to the port memory

HRESULT COutputPort::AllocateMemory(CSampleInstrument &SampleInstrument)
{
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("COutputPort::AllocateMemory()");
	
	DWORD dwDLId = 0;

	// Checks member vaiables initialization

	if ((m_pPortDownload) && (!SampleInstrument.m_pWaveDownload) 
	&& (!SampleInstrument.m_pArticulationDownload)
	&& ((SampleInstrument.m_bFileRead) || (SampleInstrument.m_bWaveFormSet)))
	{

		// Gets the identifier for download buffers

		if (FAILED(hr = m_pPortDownload->GetDLId(&dwDLId,2)))
			throw CDMusicException(strMembrFunc,hr,__LINE__);

		// Allocates instrument data into the buffer

		SampleInstrument.m_pPortDownload = m_pPortDownload;
		SampleInstrument.AllocateInstBuffer();

		DWORD dwAppend = 0;

		// Appends data in the buffer

		if (FAILED(hr = m_pPortDownload->GetAppend(&dwAppend)))
			throw CDMusicException(strMembrFunc,hr,__LINE__);

 		// Allocates wave data and sample data into the buffer
		
		SampleInstrument.AllocateWaveBuffer(dwAppend);

		// Fills the buffers with their respective data

		SampleInstrument.GetWaveBuffer(dwDLId);
	
		SampleInstrument.GetInstBuffer(dwDLId);
	} else throw CDMusicException(strMembrFunc,hr,__LINE__);
	
	return S_OK;
}
	 
// Downloads a sample instrument to the port 

HRESULT COutputPort::DownloadInstrument(CSampleInstrument &SampleInstrument)
{
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("COutputPort::DownloadInstrument(CSampleInstrument)");

	if ((m_pPortDownload) && (SampleInstrument.m_pWaveDownload) && (SampleInstrument.m_pArticulationDownload))
	{
		if (FAILED(hr = m_pPortDownload->Download(SampleInstrument.m_pWaveDownload)))
			throw CDMusicException(strMembrFunc,hr,__LINE__);

		if (FAILED(hr = m_pPortDownload->Download(SampleInstrument.m_pArticulationDownload)))
			throw CDMusicException(strMembrFunc,hr,__LINE__);
	} else throw CDMusicException(strMembrFunc,hr,__LINE__);
	
	return S_OK;
}

// Unload an instrument from the port

HRESULT COutputPort::UnloadInstrument(CInstrument &Instrument)
{
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("COutputPort::UnloadInstrument(CInstrument)");
	
	if ((m_pPort) && (Instrument.m_pDownLoadedInstrument))
	{
		if (FAILED(hr = m_pPort->UnloadInstrument(Instrument.m_pDownLoadedInstrument))) 
			throw CDMusicException(strMembrFunc,hr,__LINE__); 
		SAFE_RELEASE(Instrument.m_pDownLoadedInstrument);
	} else return hr;
	return S_OK;
}	

// Unloads a sample instrument from the port

HRESULT COutputPort::UnloadInstrument(CSampleInstrument &SampleInstrument)
{
	HRESULT hr1 =S_OK ,hr2 = S_OK ,hr3 = S_OK;
	
	TCHAR strMembrFunc[] = _T("COutputPort::UnloadInstrument(CSampleInstrument)");

	if ((m_pPortDownload) && (SampleInstrument.m_pWaveDownload))
		hr1 = m_pPortDownload->Unload(SampleInstrument.m_pWaveDownload);
	else hr3 = DM_FAILED;
	
	if ((m_pPortDownload) && (SampleInstrument.m_pArticulationDownload))
		hr2 = m_pPortDownload->Unload(SampleInstrument.m_pArticulationDownload);
	else hr3 = DM_FAILED;

	
	if (FAILED(hr1)) throw CDMusicException(strMembrFunc,hr1,__LINE__);
	if (FAILED(hr2)) throw CDMusicException(strMembrFunc,hr2,__LINE__);
	if (FAILED(hr3)) throw CDMusicException(strMembrFunc,hr3,__LINE__);

	return S_OK;
}

// Frees the memory for the allocated download interfaces

HRESULT COutputPort::DeallocateMemory(CSampleInstrument &SampleInstrument)
{
	HRESULT hr = DM_FAILED;
	
	TCHAR strMembrFunc[] = _T("COutputPort::DeallocateMemory()");

	if ((SampleInstrument.m_pWaveDownload) && (SampleInstrument.m_pArticulationDownload))
	{
		SAFE_RELEASE(SampleInstrument.m_pWaveDownload);
		SAFE_RELEASE(SampleInstrument.m_pArticulationDownload);
	} else throw CDMusicException(strMembrFunc,hr,__LINE__);
	
	return S_OK;
}

// Instructs the port to compact DLS or wave-table memory 

HRESULT COutputPort::CompactMemory()
{
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("COutputPort::CompactMemory()");
	
	if (!m_pPort) throw CDMusicException(strMembrFunc,hr,__LINE__); 

	if (FAILED(hr = m_pPort->Compact()))
			 throw CDMusicException(strMembrFunc,hr,__LINE__); 
	
	return hr;
}
	
// Returns the priority of a given channel

HRESULT COutputPort::GetChannelPriority(DWORD dwChannelGroup,DWORD dwChannel,LPDWORD pdwPriority)
{
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("COutputPort::GetChannelPriority()");
	
	if ((!m_pPort) || (!pdwPriority)) throw CDMusicException(strMembrFunc,hr,__LINE__); 

	return m_pPort->GetChannelPriority(dwChannelGroup,dwChannel,pdwPriority);

}
	
// Sets the priority for a given channel

HRESULT COutputPort::SetChannelPriority(DWORD dwChannelGroup,DWORD dwChannel,DWORD dwPriority)
{
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("COutputPort::SetChannelPriority()");
	
	if (!m_pPort) throw CDMusicException(strMembrFunc,hr,__LINE__); 

	if (FAILED(hr = m_pPort->SetChannelPriority(dwChannelGroup,dwChannel,dwPriority)))
		throw CDMusicException(strMembrFunc,hr,__LINE__); 

	return S_OK;
}

// Obtains the return the current running status of a synthesizer

HRESULT COutputPort::GetSynthStats(LPSYNTHSTATS SynthStats)
{
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("COutputPort::GetSynthStats()");

	if ((m_pPort) && (SynthStats))
	{
		ZeroMemory(SynthStats,sizeof(_DMUS_SYNTHSTATS8));
		SynthStats->dwSize = sizeof(_DMUS_SYNTHSTATS8);
		if (FAILED(hr = m_pPort->GetRunningStats((_DMUS_SYNTHSTATS*)SynthStats)))
			throw CDMusicException(strMembrFunc,hr,__LINE__);
	} else throw CDMusicException(strMembrFunc,hr,__LINE__);

	return S_OK;
}

// Retrieves information about the WAV format specified in the port

HRESULT COutputPort::GetFormat(LPWAVEFORMATEX pWaveFormatEx,LPDWORD pdwWaveFormatExSize,LPDWORD pdwBufferSize)
{
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("COutputPort::GetFormat()");

	if ((m_pPort) && (pWaveFormatEx) && (pdwWaveFormatExSize) && (pdwBufferSize))
	{	
		if (FAILED(hr = m_pPort->GetFormat(pWaveFormatEx,pdwWaveFormatExSize,pdwBufferSize)))
			throw CDMusicException(strMembrFunc,hr,__LINE__);
	} else throw CDMusicException(strMembrFunc,hr,__LINE__);

	return S_OK;
}


// Returns the number of channel groups specified for the port

HRESULT COutputPort::GetNumChannelGroups(LPDWORD pdwChannelGroups)
{
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("COutputPort::GetNumChannelGroups()");

	if ((m_pPort) && (pdwChannelGroups))
	{
		if (FAILED(hr = m_pPort->GetNumChannelGroups(pdwChannelGroups)))
			throw CDMusicException(strMembrFunc,hr,__LINE__);
	} else throw CDMusicException(strMembrFunc,hr,__LINE__);

	return S_OK;
}

// Sets the number of channel groups for the port

HRESULT COutputPort::SetNumChannelGroups(DWORD dwChannelGroups)
{
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("COutputPort::SetNumChannelGroups()");

	if (m_pPort)
	{
		if (FAILED(hr = m_pPort->SetNumChannelGroups(dwChannelGroups)))
			throw CDMusicException(strMembrFunc,hr,__LINE__);
	} else throw CDMusicException(strMembrFunc,hr,__LINE__);

	return S_OK;
}

// Sends an unstructured MIDI message

HRESULT COutputPort::SendMidiMsg(LPBYTE lpMsg,DWORD dwLength,DWORD dwChannelGroup)
{
	HRESULT hr = DM_FAILED;
	REFERENCE_TIME rt;
	TCHAR strMembrFunc[] = _T("COutputPort::SendMidiMsg() long version");

	if ((m_pPort) && (m_pBuffer) && (m_pClock))
	{
		if (FAILED(hr = m_pClock->GetTime(&rt))) 
			 throw CDMusicException(strMembrFunc,hr,__LINE__); // Gets the exact time to play it
		if (FAILED(hr = m_pBuffer->PackUnstructured(rt,dwChannelGroup,dwLength,lpMsg))) 
			 throw CDMusicException(strMembrFunc,hr,__LINE__); 
		if (FAILED(hr = m_pPort->PlayBuffer(m_pBuffer))) 
			 throw CDMusicException(strMembrFunc,hr,__LINE__); // Sends the data
		if (FAILED(hr = m_pBuffer->Flush())) 
			 throw CDMusicException(strMembrFunc,hr,__LINE__); // Discards all data in the buffer
	} else throw CDMusicException(strMembrFunc,hr,__LINE__); 
	
	return S_OK;
}	

// Function to send a MIDI normal message to the selected output port

HRESULT COutputPort::SendMidiMsg(DWORD dwMsg,DWORD dwChannelGroup)
{
	HRESULT hr = DM_FAILED;
	REFERENCE_TIME rt;
	TCHAR strMembrFunc[] = _T("COutputPort::SendMidiMsg() short version");

	if ((m_pPort) && (m_pBuffer) && (m_pClock))
	{
		if (FAILED(hr = m_pClock->GetTime(&rt))) 
			throw CDMusicException(strMembrFunc,hr,__LINE__); // Gets the exact time to play it
		if (FAILED(hr = m_pBuffer->PackStructured(rt,dwChannelGroup,dwMsg))) 
			throw CDMusicException(strMembrFunc,hr,__LINE__); 
		if (FAILED(hr = m_pPort->PlayBuffer(m_pBuffer))) 
			throw CDMusicException(strMembrFunc,hr,__LINE__); // Sends the data
		if (FAILED(hr = m_pBuffer->Flush())) 
			throw CDMusicException(strMembrFunc,hr,__LINE__); // Discards all data in the buffer
	} else throw CDMusicException(strMembrFunc,hr,__LINE__);

	return S_OK;
	
}


// Function to activate an effect in the port

HRESULT COutputPort::SetEffect(BYTE bEffect)
{
    HRESULT      hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("COutputPort::SetEffect()");
    
	KSPROPERTY   ksp;
    DWORD        dwEffects = bEffect;
    ULONG        cb;

	// Get the IKsControl interface
	if (m_pPort)
	{
		SAFE_RELEASE(m_pControl);

		hr = m_pPort->QueryInterface(IID_IKsControl,(void**)&m_pControl);

		if (SUCCEEDED(hr))
		{
			ZeroMemory(&ksp, sizeof(ksp));
    
			ksp.Set = GUID_DMUS_PROP_Effects;
			ksp.Id = 0;
			ksp.Flags = KSPROPERTY_TYPE_SET;
	
			hr = m_pControl->KsProperty(&ksp,sizeof(ksp),(LPVOID)&dwEffects, sizeof(dwEffects),&cb);
		
			if (FAILED(hr) && (hr != DMUS_E_UNKNOWN_PROPERTY)) throw CDMusicException(strMembrFunc,hr,__LINE__);

			SAFE_RELEASE(m_pControl);
	
		} else throw CDMusicException(strMembrFunc,hr,__LINE__);
	} else throw CDMusicException(strMembrFunc,hr,__LINE__);
	
	return hr;
}

// Returns a pointer to the IReferenceClock interface

IReferenceClock* COutputPort::GetReferenceClock()
{
	return m_pClock;
}

// Code a MIDI message given the three bytes of a MIDI message

DWORD COutputPort::EncodeMidiMsg(BYTE Status,BYTE DataByte1,BYTE DataByte2)
{
	DWORD dwMsg;
	dwMsg = Status;
    dwMsg |= DataByte1 << SHIFT_8;
    dwMsg |= DataByte2 << SHIFT_16;
	return dwMsg;
}

// Code a MIDI message given the Command and the Channel

DWORD COutputPort::EncodeMidiMsg(BYTE Command,BYTE Channel,BYTE DataByte1,BYTE DataByte2)
{
    DWORD dwMsg;
	dwMsg = Command | Channel;
    dwMsg |= DataByte1 << SHIFT_8;
    dwMsg |= DataByte2 << SHIFT_16;
	return dwMsg;
}




