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
Module : CPortPerformance.cpp
Purpose: Code implementation for CPortPerformance class
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
#include "CAudioPart.h"
#include "CMidiPart.h"
#include <atlbase.h>
#include <algorithm>

using namespace directmidi;
using namespace std;

// Constructor

CPortPerformance::CPortPerformance()
{
	m_bAddedPort     = FALSE;
}

// Destructor

CPortPerformance::~CPortPerformance()
{
	ReleasePerformance();	
}

// Initializes port performance

HRESULT CPortPerformance::Initialize(CDirectMusic &DMusic8,IDirectSound *pDirectSound,HWND hWnd)
{
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("CPortPerformance::Initialize()");
	
	if (DMusic8.m_pMusic8 == NULL || m_pPerformance)
		throw CDMusicException(strMembrFunc,hr,__LINE__);

	CPerformance::Initialize();
	
	if (FAILED(hr = m_pPerformance->Init((IDirectMusic**)&DMusic8.m_pMusic8,pDirectSound,hWnd)))
		throw CDMusicException(strMembrFunc,hr,__LINE__);

	return hr;
}

// Adds the port to the performance

HRESULT CPortPerformance::AddPort(COutputPort &OutPort,DWORD dwBlockNum,DWORD dwGroup)
{
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("CPortPerformance::AddPort()");

	if (m_pPerformance == NULL || OutPort.m_pPort == NULL || m_bAddedPort)
		throw CDMusicException(strMembrFunc,hr,__LINE__);

	if (FAILED(hr = m_pPerformance->AddPort(OutPort.m_pPort)))
		throw CDMusicException(strMembrFunc,hr,__LINE__);

	// Assigns a block of 16 performance channels to the port and maps them to a port and group


	if (FAILED(hr = m_pPerformance->AssignPChannelBlock(dwBlockNum,OutPort.m_pPort,dwGroup)))
		throw CDMusicException(strMembrFunc,hr,__LINE__);


	m_bAddedPort = TRUE;

	m_pPort = OutPort.m_pPort;

	return hr;
}

// Removes the port from the performance

HRESULT CPortPerformance::RemovePort()
{
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("CPortPerformance::RemovePort()");

	if (m_pPerformance == NULL)
		throw CDMusicException(strMembrFunc,hr,__LINE__);

	if (FAILED(hr = m_pPerformance->RemovePort(m_pPort)))
			throw CDMusicException(strMembrFunc,hr,__LINE__);
	
	m_bAddedPort = FALSE;

	return hr;
}

// Plays a segment in the port performance

HRESULT CPortPerformance::PlaySegment(CSegment &Segment,DWORD dwFlags,__int64 i64StartTime)
{
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("CPortPerformance::PlaySegment()");

	if (m_pPerformance == NULL || Segment.m_pSegment == NULL)
		throw CDMusicException(strMembrFunc,hr,__LINE__);

	return CPerformance::PlaySegment(Segment,NULL,dwFlags,i64StartTime);
}

// Releases and closes down the performance

HRESULT CPortPerformance::ReleasePerformance()
{
	HRESULT hr = S_FALSE;

	if ((m_pPort) && (m_pPerformance) && (m_bAddedPort))
		hr = m_pPerformance->RemovePort(m_pPort);
	
	CPerformance::ReleasePerformance();
	
	return hr;
}

// Assigns a block of 16 performance channels to the performance and maps them to a 
// port and a channel group		!!!WRONG WRONG WRONG!!!  It assigns a single channel.

HRESULT CPortPerformance::AssignPChannel(COutputPort &OutPort,DWORD dwPChannel,DWORD dwGroup,DWORD dwMChannel)
{
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("CPortPerformance::AssignPChannel()");

	if (m_pPerformance == NULL || OutPort.m_pPort == NULL)
		throw CDMusicException(strMembrFunc,hr,__LINE__);
	
	if (FAILED(hr = m_pPerformance->AssignPChannel(dwPChannel,OutPort.m_pPort,dwGroup,dwMChannel)))
		throw CDMusicException(strMembrFunc,hr,__LINE__);

	return hr;

}

// Assigns a block of 16 performance channels to the performance and maps them to a 
// port and a channel group

HRESULT CPortPerformance::AssignPChannelBlock(COutputPort &OutPort,DWORD dwBlockNum,DWORD dwGroup)
{
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("CPortPerformance::AssignPChannelBlock()");

	if (m_pPerformance == NULL || OutPort.m_pPort == NULL)
		throw CDMusicException(strMembrFunc,hr,__LINE__);
	
	if (FAILED(hr = m_pPerformance->AssignPChannelBlock(dwBlockNum,OutPort.m_pPort,dwGroup)))
		throw CDMusicException(strMembrFunc,hr,__LINE__);

	return hr;

}

// Sends a MIDI 1.0 message to the port performance

HRESULT CPortPerformance::SendMidiMsg(BYTE bStatus,BYTE bByte1,BYTE bByte2,DWORD dwPChannel)
{
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("CPortPerformance::SendMidiMsg()");

	if (m_pPerformance == NULL)
		throw CDMusicException(strMembrFunc,hr,__LINE__);

	// Gets the graph

	CComPtr<IDirectMusicGraph8> pIDirectMusicGraph;
    
	if (FAILED(hr = m_pPerformance->QueryInterface(IID_IDirectMusicGraph8,(void**)&pIDirectMusicGraph)))
		throw CDMusicException(strMembrFunc,hr,__LINE__);
    
	CPerformance::SendMidiMsg(pIDirectMusicGraph,bStatus,bByte1,bByte2,dwPChannel);

	return hr;
}
