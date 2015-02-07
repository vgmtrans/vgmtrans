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
Module : CSegment.cpp
Purpose: Code implementation for CSegment class
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
#include "CAudioPart.h"
#include "CMidiPart.h"
#include <algorithm>
#include <atlbase.h>

using namespace directmidi;
using namespace std;

// Constructor

CSegment::CSegment()
{
	m_pSegment		= NULL;
	m_pSegmentState = NULL;
	m_pPerformance	= NULL;
}

// Destructor

CSegment::~CSegment()
{
	ReleaseSegment();
}

// Releases the segment, unloads the segment from all downloaded performances

HRESULT CSegment::ReleaseSegment()
{
	if (!m_PerformanceList.empty()) 
	{	
		UnloadAllPerformances();
		m_PerformanceList.clear();
	}

	if (m_pSegment)
	{
		SAFE_RELEASE(m_pSegment);
		return S_OK;
	} else return S_FALSE;
}

// Downloads a segment to a performance

HRESULT CSegment::Download(CPerformance &Performance)
{
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("CSegment::Download()");

	if (m_pSegment == NULL || Performance.m_pPerformance == NULL)
		throw CDMusicException(strMembrFunc,hr,__LINE__);

	if (FAILED(hr = m_pSegment->Download(m_pPerformance = Performance.m_pPerformance)))
		throw CDMusicException(strMembrFunc,hr,__LINE__);
	
	// Add this segment to the performance list

	m_PerformanceList.insert(m_PerformanceList.end(),Performance.m_pPerformance);
	
	return hr;
}

// Unloads a segment from the performance

HRESULT CSegment::Unload(CPerformance &Performance)
{
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("CSegment::Unload()");

	if (m_pSegment == NULL || Performance.m_pPerformance == NULL)
		throw CDMusicException(strMembrFunc,hr,__LINE__);

	if (FAILED(hr = m_pSegment->Unload(Performance.m_pPerformance)))
		throw CDMusicException(strMembrFunc,hr,__LINE__);
	
	// Removes the segment from the performance list

	if (m_PerformanceList.end()!=(m_PerformanceIterator = std::find(m_PerformanceList.begin(),m_PerformanceList.end(),Performance.m_pPerformance)))
			m_PerformanceList.erase(m_PerformanceIterator);
	
	return hr;
}

// Connects a DLS collection to a segment

HRESULT CSegment::ConnectToDLS(CCollection &Collection)
{

	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("CSegment::ConnectToDLS()");

	if (m_pSegment == NULL)
		throw CDMusicException(strMembrFunc,hr,__LINE__);

	if (FAILED(hr = m_pSegment->SetParam(GUID_ConnectToDLSCollection,
		0xFFFFFFFF,DMUS_SEG_ALLTRACKS,0,(void*)Collection.m_pCollection)))
			throw CDMusicException(strMembrFunc,hr,__LINE__);
  
	return hr;
}


// Unloads the segment from all downloaded performances

HRESULT CSegment::UnloadAllPerformances()
{
	for(m_PerformanceIterator = m_PerformanceList.begin();m_PerformanceIterator!=m_PerformanceList.end();m_PerformanceIterator++)
			m_pSegment->Unload(*m_PerformanceIterator);
	return S_OK;
}	

// Retrieves the number of times the looping portion of the segment is set to repeat

HRESULT CSegment::GetRepeats(DWORD *pdwRepeats)
{
	TCHAR strMembrFunc[] = _T("CSegment::GetRepeats()");
	DM_ASSERT(strMembrFunc,__LINE__,m_pSegmentState==NULL,pdwRepeats==NULL);
	DM_ASSERT_HR(strMembrFunc,__LINE__,m_pSegmentState->GetRepeats(pdwRepeats));
	return S_OK;
}


// Retrieves the seek pointer for the current playing segment

HRESULT CSegment::GetSeek(MUSIC_TIME *pmtSeek)
{
	TCHAR strMembrFunc[] = _T("CSegment::GetSeek()");
	DM_ASSERT(strMembrFunc,__LINE__,m_pSegmentState==NULL,pmtSeek==NULL);
	DM_ASSERT_HR(strMembrFunc,__LINE__,m_pSegmentState->GetSeek(pmtSeek));
	return S_OK;
}
	
// Retrieves a pointer to the segment 

HRESULT CSegment::GetSegment(IDirectMusicSegment **ppSegment)
{
	TCHAR strMembrFunc[] = _T("CSegment::GetSegment()");
	DM_ASSERT(strMembrFunc,__LINE__,m_pSegmentState==NULL,ppSegment==NULL);
	DM_ASSERT_HR(strMembrFunc,__LINE__,m_pSegmentState->GetSegment(ppSegment));
	return S_OK;
}
	
// Sets the point within the segment at which it starts playing

HRESULT CSegment::SetStartPoint(MUSIC_TIME mtStart)
{
	TCHAR strMembrFunc[] = _T("CSegment::SetStartPoint()");
	DM_ASSERT(strMembrFunc,__LINE__,m_pSegment==NULL);
	DM_ASSERT_HR(strMembrFunc,__LINE__,m_pSegment->SetStartPoint(mtStart));
	return S_OK;
}

// Retrieves the time within the segment at which it started playing

HRESULT CSegment::GetStartPoint(MUSIC_TIME *pmtStart)
{
	TCHAR strMembrFunc[] = _T("CSegment::GetStartPoint()");
	DM_ASSERT(strMembrFunc,__LINE__,m_pSegmentState==NULL,pmtStart==NULL);
	DM_ASSERT_HR(strMembrFunc,__LINE__,m_pSegmentState->GetStartPoint(pmtStart));
	return S_OK;
}

// Retrieves the performance time at which the segment started playing

HRESULT CSegment::GetStartTime(MUSIC_TIME *pmtStart)
{
	TCHAR strMembrFunc[] = _T("CSegment::GetStartTime()");
	DM_ASSERT(strMembrFunc,__LINE__,m_pSegmentState==NULL,pmtStart==NULL);
	DM_ASSERT_HR(strMembrFunc,__LINE__,m_pSegmentState->GetStartTime(pmtStart));
	return S_OK;
}

// Sets the default resolution of the segment

HRESULT CSegment::SetDefaultResolution(DWORD dwResolution)
{
	TCHAR strMembrFunc[] = _T("CSegment::SetDefaultResolution()");
	DM_ASSERT(strMembrFunc,__LINE__,m_pSegment==NULL);
	DM_ASSERT_HR(strMembrFunc,__LINE__,m_pSegment->SetDefaultResolution(dwResolution));
	return S_OK;
}

// Retrieves the default resolution of the segment

HRESULT CSegment::GetDefaultResolution(DWORD *pdwResolution)
{
	TCHAR strMembrFunc[] = _T("CSegment::GetDefaultResolution()");
	DM_ASSERT(strMembrFunc,__LINE__,m_pSegment==NULL,pdwResolution==NULL);
	DM_ASSERT_HR(strMembrFunc,__LINE__,m_pSegment->GetDefaultResolution(pdwResolution));
	return S_OK;
}

// Sets the length, in music time, of the segment

HRESULT CSegment::SetLength(MUSIC_TIME mtLength)
{
	TCHAR strMembrFunc[] = _T("CSegment::SetLength()");
	DM_ASSERT(strMembrFunc,__LINE__,m_pSegment==NULL);
	DM_ASSERT_HR(strMembrFunc,__LINE__,m_pSegment->SetLength(mtLength));
	return S_OK;
}

// Retrieves the length of the segment

HRESULT CSegment::GetLength(MUSIC_TIME *pmtLength)
{
	TCHAR strMembrFunc[] = _T("CSegment::GetLength()");
	DM_ASSERT(strMembrFunc,__LINE__,m_pSegment==NULL,pmtLength==NULL);
	DM_ASSERT_HR(strMembrFunc,__LINE__,m_pSegment->GetLength(pmtLength));
	return S_OK;
}

// Sets the start and end points of the part of the segment that repeats

HRESULT CSegment::GetLoopPoints(MUSIC_TIME *pmtStart,MUSIC_TIME *pmtEnd)
{	
	TCHAR strMembrFunc[] = _T("CSegment::GetLoopPoints()");
	DM_ASSERT(strMembrFunc,__LINE__,m_pSegment==NULL,pmtStart==NULL,pmtEnd==NULL);
	DM_ASSERT_HR(strMembrFunc,__LINE__,m_pSegment->GetLoopPoints(pmtStart,pmtEnd));
	return S_OK;
}

// Sets the start and end points of the part of the segment that repeats

HRESULT CSegment::SetLoopPoints(MUSIC_TIME mtStart,MUSIC_TIME mtEnd)
{
	TCHAR strMembrFunc[] = _T("CSegment::SetLoopPoints()");
	DM_ASSERT(strMembrFunc,__LINE__,m_pSegment==NULL);
	DM_ASSERT_HR(strMembrFunc,__LINE__,m_pSegment->SetLoopPoints(mtStart,mtEnd));
	return S_OK;
}

// Sets the number of times the looping portion of the segment is to repeat

HRESULT CSegment::SetRepeats(DWORD dwRepeats)
{
	TCHAR strMembrFunc[] = _T("CSegment::SetRepeats()");
	DM_ASSERT(strMembrFunc,__LINE__,m_pSegment==NULL);
	DM_ASSERT_HR(strMembrFunc,__LINE__,m_pSegment->SetRepeats(dwRepeats));
	return S_OK;
}

// Sets the performance channels that this segment uses

HRESULT CSegment::SetPChannelsUsed(DWORD dwNumPChannels,DWORD *paPChannels)
{
	TCHAR strMembrFunc[] = _T("CSegment::SetPChannelUsed()");
	DM_ASSERT(strMembrFunc,__LINE__,m_pSegment==NULL,paPChannels==NULL);
	DM_ASSERT_HR(strMembrFunc,__LINE__,m_pSegment->SetPChannelsUsed(dwNumPChannels,paPChannels));
	return S_OK;
}

// = operator. It returns a cloned segment

CSegment& CSegment::operator = (const CSegment &Segment)
{
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("(CSegment)CSegment::operator=(CSegment)");

	if (Segment.m_pSegment == NULL)
		throw CDMusicException(strMembrFunc,hr,__LINE__);

	CComPtr<IDirectMusicSegment> pOldSegment;

	if (FAILED(hr = Segment.m_pSegment->Clone(0,0,&pOldSegment)))
		throw CDMusicException(strMembrFunc,hr,__LINE__);

	if (FAILED(hr = pOldSegment->QueryInterface(IID_IDirectMusicSegment8,(void**)&m_pSegment)))
		throw CDMusicException(strMembrFunc,hr,__LINE__);

	return *this;
}