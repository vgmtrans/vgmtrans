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
Module : CInputPort.cpp
Purpose: Code implementation for the CInputport class
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
#include <algorithm>

using namespace directmidi;
using namespace std;

const int THREAD_TIMEOUT = 30000;

// Class constructor, member variables initialization

CInputPort::CInputPort():CMidiPort(DMUS_PC_INPUTCLASS)
{
	m_pThru			= NULL;
	m_pReceiver     = NULL;
	m_hEvent		= NULL;
	m_hThread		= NULL;
	m_hTerminationEvent = NULL;
	m_ProcessActive = FALSE;
}	

// Class destructor
// Release all the interfaces
// Call TerminateNotification in case the thread is active 

CInputPort::~CInputPort()
{
	if (m_hEvent) TerminateNotification();
	if (!m_ChannelList.empty())
		BreakAllThru();
	ReleasePort();
}

// Function to select and activate an Input MIDI port given a LPINFOPORT structure

HRESULT CInputPort::ActivatePort(LPINFOPORT InfoPort,DWORD dwSysExSize)
{
	HRESULT hr = DM_FAILED;
	
	TCHAR strMembrFunc[] = _T("CInputPort::ActivatePort()");

	if (m_pThru || m_hEvent || m_ProcessActive) throw CDMusicException(strMembrFunc,hr,__LINE__);
	
	CMidiPort::ActivatePort(InfoPort,dwSysExSize,FALSE);
	
	// Gets the thru interface
	if (FAILED(hr = m_pPort->QueryInterface(IID_IDirectMusicThru8,(void**)&m_pThru))) 
			throw CDMusicException(strMembrFunc,hr,__LINE__);
	
	return S_OK;
}

// Initializes a CInputPort from a valid IDirectMusicPort8 interface

HRESULT CInputPort::ActivatePortFromInterface(IDirectMusicPort8* pPort,DWORD dwSysExSize)
{

	HRESULT hr = DM_FAILED;
	
	TCHAR strMembrFunc[] = _T("CInputPort::ActivatePort()");

	if (pPort==NULL) throw CDMusicException(strMembrFunc,hr,__LINE__);

	m_pPort = pPort;

	CMidiPort::ActivatePort(NULL,dwSysExSize,TRUE);

	// Gets the thru interface
	if (FAILED(hr = m_pPort->QueryInterface(IID_IDirectMusicThru8,(void**)&m_pThru))) 
			throw CDMusicException(strMembrFunc,hr,__LINE__);
	
	return S_OK;
}

// This function removes the selected port 
// and releases all their asociated interfaces for a new port selection

HRESULT CInputPort::ReleasePort()
{
	HRESULT hr = DM_FAILED;
	
	TCHAR strMembrFunc[] = _T("CInputPort::ReleasePort()");
	
	if (m_pPort)
		hr = m_pPort->Activate(FALSE);
	
	SAFE_RELEASE(m_pPort);
	SAFE_RELEASE(m_pBuffer);
	SAFE_RELEASE(m_pThru);
	
	return hr;
}


// Main thread for processing incoming MIDI events

DWORD CInputPort::WaitForEvent(LPVOID lpv)
{
    HRESULT hr;
    REFERENCE_TIME rt;	// System time when the event is received
    DWORD dwGroup;		// Channel group
    DWORD dwcb,dwMsg;	// Byte counter, Midi message	
    BYTE *pb;			// Pointer to the data
		
	CInputPort *ptrPort=(CInputPort*)(lpv);
	
	while(ptrPort->m_ProcessActive)	// Check for active
	{
		WaitForMultipleObjects(1,&ptrPort->m_hEvent, FALSE, INFINITE); // Waits for an event
		while (ptrPort->m_ProcessActive)
		{
			hr = ptrPort->ReadBuffer();	// Read incoming midi messages
			if (hr == S_FALSE)
			{
				break;  // No more messages to read into the buffer
			} 			
			ptrPort->ResetBuffer();// Set the buffer pointer to the start
					
			while (ptrPort->m_ProcessActive)	
			{
				hr = ptrPort->GetMidiEvent(&rt,&dwGroup,&dwcb,&pb); // Extract midi data
				if (hr == S_OK)
				{
					if (dwcb>MIDI_STRUCTURED)	// In case it is SysEx data
						ptrPort->m_pReceiver->RecvMidiMsg(rt,dwGroup,dwcb,pb);
					else
					{
						CopyMemory(&dwMsg,pb,dwcb); // Structured data (Standard format)	
						ptrPort->m_pReceiver->RecvMidiMsg(rt,dwGroup,dwMsg);
					}
					// pb points to the data structure for the message, and
				}	// you can do anything that you want with it
				else if (hr == S_FALSE)
				{
					break;  // No more messages in the buffer
				}
			}  // Done with the buffer
				
		}  // Done reading pending events
	}
		
	SetEvent(ptrPort->m_hTerminationEvent);  
	return 0;
}


// Activate the notification handler
// Begins the worker thread execution
 
HRESULT CInputPort::ActivateNotification()
{
	HRESULT hr = DM_FAILED;
	DWORD	dwThId;
	TCHAR strMembrFunc[] = _T("CInputPort::ActivateNotification()");

	m_hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	
	if (m_hEvent && m_pReceiver)
	{
	
		if (m_pPort)	// Activate the event notification
		{
			if (FAILED(hr = m_pPort->SetReadNotificationHandle(m_hEvent))) 
				throw CDMusicException(strMembrFunc,hr,__LINE__);
			
						// Activate the safe thread termination event
			if ((m_hTerminationEvent = CreateEvent(NULL,FALSE,FALSE,NULL))==NULL)
				throw CDMusicException(strMembrFunc,DM_FAILED,__LINE__);

			m_ProcessActive = TRUE;
			// Create the main thread	
			if ((m_hThread = CreateThread(NULL,0,WaitForEvent,this,0,&dwThId)) == NULL) 
				throw CDMusicException(strMembrFunc,hr,__LINE__);
		} else throw CDMusicException(strMembrFunc,hr,__LINE__);
	} else throw CDMusicException(strMembrFunc,hr,__LINE__);
	
	return S_OK;

}


// Sets the internal MIDI receiver thread priority

BOOL CInputPort::SetThreadPriority(int nPriority)
{
	TCHAR strMembrFunc[] = _T("CInputPort::SetThreadPriority()");

	if (m_hThread == NULL) throw CDMusicException(strMembrFunc,DM_FAILED,__LINE__);

	if (!::SetThreadPriority(m_hThread,nPriority))
		throw CDMusicException(strMembrFunc,DM_FAILED,__LINE__);
	
	return TRUE;
}

// Sets the receiver object for the current input port thread

HRESULT CInputPort::SetReceiver(CReceiver &Receiver)
{
	TCHAR strMembrFunc[] = _T("CInputPort::SetReceiver()");

	if (m_pPort == NULL) throw CDMusicException(strMembrFunc,DM_FAILED,__LINE__);

	m_pReceiver = &Receiver;

	return S_OK;
}
	
// Activates the MIDI thru from an Input MIDI port to a given Output MIDI port

HRESULT CInputPort::SetThru(DWORD dwSourceChannel,DWORD dwDestinationChannelGroup,
							DWORD dwDestinationChannel,COutputPort &dstMidiPort)
{
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("CInputPort::SetThru()");
	
	if (m_pThru)
	{
		if (FAILED(hr = m_pThru->ThruChannel(1,dwSourceChannel,dwDestinationChannelGroup,
			dwDestinationChannel,dstMidiPort.m_pPort)))
				throw CDMusicException(strMembrFunc,hr,__LINE__);
		
		// Finds the set of channels on the list and inserts them

		VECTOR chvect(3);
		chvect[0] = static_cast<WORD>(dwSourceChannel);
		chvect[1] = static_cast<WORD>(dwDestinationChannelGroup);
		chvect[2] = static_cast<WORD>(dwDestinationChannel);

		if (m_ChannelList.end() == find(m_ChannelList.begin(),m_ChannelList.end(),chvect))
			m_ChannelList.insert(m_ChannelList.end(),chvect);
	
	} else throw CDMusicException(strMembrFunc,hr,__LINE__);		
	
	return S_OK;
}    


// Desactivates the MIDI thru 

HRESULT CInputPort::BreakThru(DWORD dwSourceChannel,DWORD dwDestinationChannelGroup,
							  DWORD dwDestinationChannel)
{
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("CInputPort::BreakThru()");
	
	if (m_pThru)
	{
		
		if (FAILED(hr = m_pThru->ThruChannel(1,dwSourceChannel,dwDestinationChannelGroup,
			dwDestinationChannel,NULL)))
		throw CDMusicException(strMembrFunc,hr,__LINE__);
		
		
		// Removes the set of channels from the list
		
		VECTOR chvect(3);
		chvect[0] = static_cast<WORD>(dwSourceChannel);
		chvect[1] = static_cast<WORD>(dwDestinationChannelGroup);
		chvect[2] = static_cast<WORD>(dwDestinationChannel);

		if (m_ChannelList.end()!=find(m_ChannelList.begin(),m_ChannelList.end(),chvect))
			m_ChannelList.remove(chvect);

	} else throw CDMusicException(strMembrFunc,hr,__LINE__);
	
	return S_OK;
}    

// Breaks all MIDI-thru established connections

HRESULT CInputPort::BreakAllThru()
{
  HRESULT hr = DM_FAILED;
  
	if (m_pThru)
	{
	
		for(m_ChannelIterator = m_ChannelList.begin();m_ChannelIterator!=m_ChannelList.end();m_ChannelIterator++)
			m_pThru->ThruChannel(1,(*m_ChannelIterator)[0],(*m_ChannelIterator)[1],(*m_ChannelIterator)[2],NULL);
	
		hr = S_OK;
	} 
	
	// Erase all the list

	m_ChannelList.erase(m_ChannelList.begin(),m_ChannelList.end());  
 	return hr;	
}

// Ends the notification and terminates the thread

HRESULT CInputPort::TerminateNotification()
{
	HRESULT hr = DM_FAILED;

	if (m_pPort)
	{
		hr = m_pPort->SetReadNotificationHandle(NULL);
		// Ends the read notification
		m_ProcessActive = FALSE;		// Tells the thread to exit	
		SetEvent(m_hEvent);	 
		CloseHandle(m_hEvent);
		m_hEvent = NULL;
		// Waits for the receiver thread termination
		WaitForSingleObject(m_hTerminationEvent,THREAD_TIMEOUT);
		CloseHandle(m_hTerminationEvent);
		m_hTerminationEvent = NULL;
	}
	return hr;
}


// Function to fill a buffer with incoming MIDI data from the port

HRESULT CInputPort::ReadBuffer()
{
	if (m_pPort)
		return m_pPort->Read(m_pBuffer);
	else return DM_FAILED;
}


// Sets the read pointer to the start of the data in the buffer

HRESULT CInputPort::ResetBuffer()
{	
	if (m_pBuffer)
		return m_pBuffer->ResetReadPtr();
	else return DM_FAILED;
}

// Returns information about the next message in the buffer and advances the read pointer

HRESULT CInputPort::GetMidiEvent(REFERENCE_TIME *lprt,DWORD *lpdwGroup,DWORD *lpcb,BYTE **lppb)
{
	if (m_pBuffer)
		return m_pBuffer->GetNextEvent(lprt,lpdwGroup,lpcb,lppb);
	else return DM_FAILED;
}


// Extract from a DWORD a MIDI 1.0 message

void CInputPort::DecodeMidiMsg(DWORD dwMsg,BYTE *Status,BYTE *DataByte1,BYTE *DataByte2)
{
	*Status = static_cast<BYTE>(dwMsg);
    *DataByte1 = static_cast<BYTE>(dwMsg >> SHIFT_8);
    *DataByte2 = static_cast<BYTE>(dwMsg >> SHIFT_16);
}

// Extract from a DWORD a MIDI 1.0 message separating Command and Channel

void CInputPort::DecodeMidiMsg(DWORD dwMsg,BYTE *Command,BYTE *Channel,BYTE *DataByte1,BYTE *DataByte2)
{
    *Command = static_cast<BYTE>(dwMsg & ~BITMASK);
    *Channel = static_cast<BYTE>(dwMsg & BITMASK);
    *DataByte1 = static_cast<BYTE>(dwMsg >> SHIFT_8);
    *DataByte2 = static_cast<BYTE>(dwMsg >> SHIFT_16);
}



