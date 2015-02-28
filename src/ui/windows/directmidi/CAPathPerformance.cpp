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
Module : CAPathPerformance.cpp
Purpose: Code implementation for CAPathPerformance class
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
#include "CAudioPart.h"
#include <atlbase.h>

using namespace directmidi;

// Constructor

CAPathPerformance::CAPathPerformance()
{
}

// Destructor

CAPathPerformance::~CAPathPerformance()
{
	ReleasePerformance();
}

// Initializes and sets up the audiopath performance

HRESULT CAPathPerformance::Initialize(CDirectMusic &DMusic,IDirectSound **ppDirectSound,HWND hWnd,DWORD dwDefaultPathType,
									  DWORD dwPChannelCount,DWORD dwFlags,DMUS_AUDIOPARAMS *pAudioParams)
{
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("CAPathPerformance::Initialize()");

	if (DMusic.m_pMusic8 == NULL || m_pPerformance)
		throw CDMusicException(strMembrFunc,hr,__LINE__);

	CPerformance::Initialize();
	
	if (FAILED(hr = m_pPerformance->InitAudio((IDirectMusic**)&DMusic.m_pMusic8,ppDirectSound,hWnd,
		dwDefaultPathType, dwPChannelCount, dwFlags,pAudioParams)))
			throw CDMusicException(strMembrFunc,hr,__LINE__);

	return hr;
}

// Creates and audiopath from the performance

HRESULT CAPathPerformance::CreateAudioPath(CAudioPath &Path,DWORD dwType,DWORD dwPChannelCount,BOOL bActivate)
{
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("CAPathPerformance::CreateAudioPath()");

	if (m_pPerformance == NULL || Path.m_pPath)
		throw CDMusicException(strMembrFunc,hr,__LINE__);

	if (FAILED(hr = m_pPerformance->CreateStandardAudioPath(dwType,dwPChannelCount,
		bActivate,&Path.m_pPath))) throw CDMusicException(strMembrFunc,hr,__LINE__);

	return hr;
}

// Plays a segment in the audiopath performance

HRESULT CAPathPerformance::PlaySegment(CSegment &Segment,CAudioPath *Path,DWORD dwFlags,__int64 i64StartTime)
{
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("CAPathPerformance::PlaySegment(CSegment)");

	if (m_pPerformance == NULL ||  Segment.m_pSegment == NULL)
		throw CDMusicException(strMembrFunc,hr,__LINE__);
	
	return CPerformance::PlaySegment(Segment,Path,dwFlags,i64StartTime);
}

// Plays a 3D segment in the audiopath performance

HRESULT CAPathPerformance::PlaySegment(C3DSegment &Segment,DWORD dwFlags,__int64 i64StartTime)
{
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("CAPathPerformance::PlaySegment(C3DSegment)");
	
	if (m_pPerformance == NULL || Segment.m_pSegment == NULL)
		throw CDMusicException(strMembrFunc,hr,__LINE__);

	return CPerformance::PlaySegment(Segment,&Segment.m_AudioPath,dwFlags,i64StartTime);
}

// Releases the performance

HRESULT CAPathPerformance::ReleasePerformance()
{
	CPerformance::ReleasePerformance();
	return S_OK;
}

// Retrieves the default audiopath for the current actived performance

HRESULT CAPathPerformance::GetDefaultAudioPath(CAudioPath &Path)
{
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("CAPathPerformance::GetDefaultAudioPath()");

	if (m_pPerformance == NULL || Path.m_pPath)
		throw CDMusicException(strMembrFunc,hr,__LINE__);

	if (FAILED(hr = m_pPerformance->GetDefaultAudioPath(&Path.m_pPath))) 
		throw CDMusicException(strMembrFunc,hr,__LINE__);

	return hr;
}

// Sets the default audiopath

HRESULT CAPathPerformance::SetDefaultAudioPath(CAudioPath &Path)
{
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("CAPathPerformance::SetDefaultAudioPath()");

	if (m_pPerformance == NULL || Path.m_pPath == NULL)
		throw CDMusicException(strMembrFunc,hr,__LINE__);

	if (FAILED(hr = m_pPerformance->SetDefaultAudioPath(Path.m_pPath))) 
		throw CDMusicException(strMembrFunc,hr,__LINE__);

	return hr;
}

// Removes default audiopath

HRESULT CAPathPerformance::RemoveDefaultAudioPath()
{
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("CAPathPerformance::RemoveDefaultAudioPath()");

	if (m_pPerformance == NULL)
		throw CDMusicException(strMembrFunc,hr,__LINE__);

	if (FAILED(hr = m_pPerformance->SetDefaultAudioPath(NULL))) 
		throw CDMusicException(strMembrFunc,hr,__LINE__);

	return hr;
}

// Sends a MIDI 1.0 message to the audiopath performance

HRESULT CAPathPerformance::SendMidiMsg(BYTE bStatus,BYTE bByte1,BYTE bByte2,DWORD dwPChannel,CAudioPath &Path)
{
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("CAPathPerformance::SendMidiMsg()");

	if (m_pPerformance == NULL || Path.m_pPath == NULL)
		throw CDMusicException(strMembrFunc,hr,__LINE__);

	CComPtr<IDirectMusicGraph8> pIDirectMusicGraph;

	// Gets the graph

	if (FAILED(hr = Path.m_pPath->GetObjectInPath(0,DMUS_PATH_AUDIOPATH_GRAPH,0,GUID_NULL,
		0,IID_IDirectMusicGraph8,(void**)&pIDirectMusicGraph)))
			throw CDMusicException(strMembrFunc,hr,__LINE__);

	DMUS_MIDI_PMSG *pDMUS_MIDI_PMSG = NULL; 
 
    DWORD dwVirtual;

	// Converts a performance channel to
	// the equivalent channel allocated in the performance for the audiopath

	if (FAILED(hr = Path.m_pPath->ConvertPChannel(dwPChannel,&dwVirtual)))
		throw CDMusicException(strMembrFunc,hr,__LINE__);
	
	CPerformance::SendMidiMsg(pIDirectMusicGraph,bStatus,bByte1,bByte2,dwVirtual);

	return hr;
}

