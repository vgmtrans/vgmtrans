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
Module : CMidiPort.cpp
Purpose: Code implementation for the CMidiPort port class
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

using namespace directmidi;


CMidiPort::CMidiPort(DWORD dwType)
{
	m_pPort	  = NULL;		
	m_pBuffer = NULL;
	m_pControl= NULL;
	m_pMusic8 = NULL;
	m_dwPortType = dwType;
}

CMidiPort::~CMidiPort() 
{
		
	SAFE_RELEASE(m_pPort);
	SAFE_RELEASE(m_pBuffer);
	SAFE_RELEASE(m_pControl);
}

// Initialize object

HRESULT CMidiPort::Initialize(CDirectMusic &DMusic)
{
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("CMidiPort::Initialize()");
	if (DMusic.m_pMusic8 == NULL) throw CDMusicException(strMembrFunc,hr,__LINE__);

	m_pMusic8 = DMusic.m_pMusic8;	
	
	// Sets to 0 the port capabilities structure
	ZeroMemory(&m_PortParams,sizeof(DMUS_PORTPARAMS8));
	m_PortParams.dwSize = sizeof(DMUS_PORTPARAMS8);
	
	return S_OK;
}


// Returns the number of MIDI ports

DWORD CMidiPort::GetNumPorts()
{
	DMUS_PORTCAPS portinf;
	DWORD dwIndex = 0, dwNum = 0;
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("CMidiPort::GetNumPorts()");
	
	// Checks member variables initialization

	if (!m_pMusic8) throw CDMusicException(strMembrFunc,hr,__LINE__);
	
	// Set to 0 the DMUS_PORTCAPS structure  
	
	ZeroMemory(&portinf,sizeof(portinf));
	portinf.dwSize = sizeof(DMUS_PORTCAPS);
     
	// Call the DirectMusic8 member function to enumerate systems ports
	
	while ((hr = m_pMusic8->EnumPort(dwIndex++,&portinf))==S_OK)
		if (portinf.dwClass == m_dwPortType) dwNum++;
	if ((hr != S_FALSE) && (hr != S_OK)) throw CDMusicException(strMembrFunc,hr,__LINE__);
	return dwNum;
}

// Gets the INFOPORT information of an activated port

HRESULT CMidiPort::GetActivatedPortInfo(LPINFOPORT lpInfoPort)
{
	DMUS_PORTCAPS portinf;
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("CMidiPort::GetActivatedPortInfo()");
	
	
	// Checks member variables initialization
	
	if (!m_pPort || !m_pMusic8 || !lpInfoPort) throw CDMusicException(strMembrFunc,hr,__LINE__);

	// Sets to 0 the DMUS_PORTCAPS structure 

	ZeroMemory(&portinf,sizeof(portinf));
	portinf.dwSize = sizeof(DMUS_PORTCAPS);

	if (FAILED(hr = m_pPort->GetCaps(&portinf))) throw CDMusicException(strMembrFunc,hr,__LINE__);

	// Initializes INFOPORT structure

	ZeroMemory(lpInfoPort,sizeof(INFOPORT));
	CopyMemory(&(lpInfoPort->guidSynthGUID),&portinf.guidPort,sizeof(GUID));
		
	#ifdef _UNICODE
		_tcscpy(lpInfoPort->szPortDescription,portinf.wszDescription);
	#else
		wcstombs(lpInfoPort->szPortDescription,portinf.wszDescription,DMUS_MAX_DESCRIPTION);
	#endif

	lpInfoPort->dwClass					= portinf.dwClass;
	lpInfoPort->dwEffectFlags			= portinf.dwEffectFlags;
	lpInfoPort->dwFlags	 				= portinf.dwFlags;
	lpInfoPort->dwMemorySize			= portinf.dwMemorySize;
	lpInfoPort->dwMaxAudioChannels	    = portinf.dwMaxAudioChannels;
	lpInfoPort->dwMaxChannelGroups		= portinf.dwMaxChannelGroups;
	lpInfoPort->dwMaxVoices				= portinf.dwMaxVoices;
	lpInfoPort->dwType					= portinf.dwType;
	
	return hr;
}



// Gets the port info given the number of the port, returns a pointer to an INFOPORT structure 

HRESULT CMidiPort::GetPortInfo(DWORD dwNumPort,LPINFOPORT lpInfoPort)
{
	HRESULT hr = DM_FAILED;
	DMUS_PORTCAPS portinf;
	DWORD dwIndex = 0, dwNum = 0;
	TCHAR strMembrFunc[] = _T("CMidiPort::GetPortInfo()");
	
	// Checks member variables initialization

	if (!m_pMusic8 || !lpInfoPort) throw CDMusicException(strMembrFunc,hr,__LINE__);
	
		
	// Set to 0 the DMUS_PORTCAPS structure  
	
	ZeroMemory(&portinf,sizeof(portinf));
	portinf.dwSize = sizeof(DMUS_PORTCAPS);
    
	while ((hr = m_pMusic8->EnumPort(dwIndex++,&portinf))==S_OK) 
	{	
		if (portinf.dwClass == m_dwPortType) dwNum++;
		if (dwNum == dwNumPort) break;
	}
	
	if ((hr != S_FALSE) && (hr != S_OK)) throw CDMusicException(strMembrFunc,hr,__LINE__);										 

	if (SUCCEEDED(hr))
	{
		// Copy the GUID of DMUS_PORTCAP structure to an INFOPORT structure
   
		ZeroMemory(lpInfoPort,sizeof(INFOPORT));
		CopyMemory(&(lpInfoPort->guidSynthGUID),&portinf.guidPort,sizeof(GUID));
		
		#ifdef _UNICODE
			_tcscpy(lpInfoPort->szPortDescription,portinf.wszDescription);
		#else
			wcstombs(lpInfoPort->szPortDescription,portinf.wszDescription,DMUS_MAX_DESCRIPTION);
		#endif
		
		lpInfoPort->dwClass					= portinf.dwClass;
		lpInfoPort->dwEffectFlags			= portinf.dwEffectFlags;
		lpInfoPort->dwFlags	 				= portinf.dwFlags;
		lpInfoPort->dwMemorySize			= portinf.dwMemorySize;
		lpInfoPort->dwMaxAudioChannels	    = portinf.dwMaxAudioChannels;
		lpInfoPort->dwMaxChannelGroups		= portinf.dwMaxChannelGroups;
		lpInfoPort->dwMaxVoices				= portinf.dwMaxVoices;
		lpInfoPort->dwType					= portinf.dwType;
	}
	
	return hr;
}	

// Function to select and activate an Input MIDI port given an INFOPORT structure

HRESULT CMidiPort::ActivatePort(LPINFOPORT InfoPort,DWORD dwSysExSize,BOOL bFromInterface)
{
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("CMidiPort::ActivatePort()");
	
	// Checks member variables initialization

	if (!m_pMusic8 || ((m_pPort || !InfoPort) && !bFromInterface) || m_pBuffer ) throw CDMusicException(strMembrFunc,hr,__LINE__); 
	
	// The midi port is created here 
	
	if (!bFromInterface)
	{
		if (FAILED(hr = m_pMusic8->CreatePort(InfoPort->guidSynthGUID ,&m_PortParams,&m_pPort,NULL))) 
			throw CDMusicException(strMembrFunc,hr,__LINE__); 

	}	

	// We have to activate it
	
	if (FAILED(hr = m_pPort->Activate(TRUE))) throw CDMusicException(strMembrFunc,hr,__LINE__); 

	DMUS_BUFFERDESC BufferDesc;					// Create the DirectMusic buffer to store MIDI messages
	ZeroMemory(&BufferDesc,sizeof(DMUS_BUFFERDESC));
	BufferDesc.dwSize = sizeof(DMUS_BUFFERDESC);
	BufferDesc.guidBufferFormat = GUID_NULL;
	BufferDesc.cbBuffer = DMUS_EVENT_SIZE(dwSysExSize);	// at least 32 bytes to store messages
	
	if (FAILED(hr = m_pMusic8->CreateMusicBuffer(&BufferDesc,&m_pBuffer,NULL)))
   		throw CDMusicException(strMembrFunc,hr,__LINE__);
	
	return S_OK;
}


// Establishes a new buffer for the port

HRESULT CMidiPort::SetBuffer(DWORD dwBufferSize)
{
	HRESULT      hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("CMidiPort::SetBuffer()");
    
	// Checks member variables initialization

	if (m_pBuffer)
		RemoveBuffer();

	if (!m_pMusic8) 
		throw CDMusicException(strMembrFunc,hr,__LINE__); 
		
	DMUS_BUFFERDESC BufferDesc;		// Create the DirectMusic buffer to store MIDI messages
	ZeroMemory(&BufferDesc,sizeof(DMUS_BUFFERDESC));
	BufferDesc.dwSize = sizeof(DMUS_BUFFERDESC);
	BufferDesc.guidBufferFormat = GUID_NULL;
	BufferDesc.cbBuffer = DMUS_EVENT_SIZE(dwBufferSize);
	
	if (FAILED(hr = m_pMusic8->CreateMusicBuffer(&BufferDesc,&m_pBuffer,NULL)))
   		throw CDMusicException(strMembrFunc,hr,__LINE__);
	
	return S_OK;

}

// Removes the internal buffer

HRESULT CMidiPort::RemoveBuffer()
{
	HRESULT      hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("CMidiPort::RemoveBuffer()");
    
	if (m_pBuffer)
	{
		SAFE_RELEASE(m_pBuffer);
		return S_OK;
	} else return S_FALSE;
}	

// Sets properties for a given port

HRESULT CMidiPort::KsProperty(GUID Set,ULONG Id,ULONG Flags,LPVOID pvPropertyData,ULONG ulDataLength,PULONG pulBytesReturned)
{
    HRESULT      hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("CMidiPort::KsProperty()");
    
	KSPROPERTY   ksp;

	// Get the IKsControl interface
	if (m_pPort)
	{
		SAFE_RELEASE(m_pControl);

		hr = m_pPort->QueryInterface(IID_IKsControl,(void**)&m_pControl);

		if (SUCCEEDED(hr))
		{
			ZeroMemory(&ksp,sizeof(ksp));

			ksp.Set = Set;
			ksp.Id  = Id;
			ksp.Flags = Flags;
			
			hr = m_pControl->KsProperty(&ksp,sizeof(ksp),pvPropertyData,ulDataLength,pulBytesReturned);
			if (FAILED(hr) && (hr != DMUS_E_UNKNOWN_PROPERTY)) throw CDMusicException(strMembrFunc,hr,__LINE__);

			SAFE_RELEASE(m_pControl);
		} else throw CDMusicException(strMembrFunc,hr,__LINE__);
	} else throw CDMusicException(strMembrFunc,hr,__LINE__);

	return hr;
}


// Additional functions for VGMTrans ___________________________________________

// Gets the port info one by one, and passes it to a callback function

// User can make such a routine by using GetNumPorts and GetPortInfo.
// However, it will be very slow, since IDirectMusic8::EnumPort sometimes take a few seconds to finish,
// and those two functions call IDirectMusic8::EnumPort several times.

void CMidiPort::EnumPort(MIDIPORTENUMPROC lpEnumFunc, LPVOID lParam)
{
	DMUS_PORTCAPS portinf;
	DWORD dwIndex = 0, dwNum = 0;
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("CMidiPort::GetNumPorts()");

	// Checks member variables initialization

	if (!m_pMusic8) throw CDMusicException(strMembrFunc, hr, __LINE__);

	// Set to 0 the DMUS_PORTCAPS structure  

	ZeroMemory(&portinf, sizeof(portinf));
	portinf.dwSize = sizeof(DMUS_PORTCAPS);

	// Call the DirectMusic8 member function to enumerate systems ports

	while ((hr = m_pMusic8->EnumPort(dwIndex++, &portinf)) == S_OK)
	{
		if (portinf.dwClass == m_dwPortType)
		{
			INFOPORT stInfoPort;
			LPINFOPORT lpInfoPort = &stInfoPort;

			// Copy the GUID of DMUS_PORTCAP structure to an INFOPORT structure

			ZeroMemory(lpInfoPort, sizeof(INFOPORT));
			CopyMemory(&(lpInfoPort->guidSynthGUID), &portinf.guidPort, sizeof(GUID));

#ifdef _UNICODE
			_tcscpy(lpInfoPort->szPortDescription, portinf.wszDescription);
#else
			wcstombs(lpInfoPort->szPortDescription, portinf.wszDescription, DMUS_MAX_DESCRIPTION);
#endif

			lpInfoPort->dwClass = portinf.dwClass;
			lpInfoPort->dwEffectFlags = portinf.dwEffectFlags;
			lpInfoPort->dwFlags = portinf.dwFlags;
			lpInfoPort->dwMemorySize = portinf.dwMemorySize;
			lpInfoPort->dwMaxAudioChannels = portinf.dwMaxAudioChannels;
			lpInfoPort->dwMaxChannelGroups = portinf.dwMaxChannelGroups;
			lpInfoPort->dwMaxVoices = portinf.dwMaxVoices;
			lpInfoPort->dwType = portinf.dwType;

			if (lpEnumFunc(lpInfoPort, lParam) == FALSE) {
				break;
			}
		}
	}

	if ((hr != S_FALSE) && (hr != S_OK)) throw CDMusicException(strMembrFunc, hr, __LINE__);

	return;
}
