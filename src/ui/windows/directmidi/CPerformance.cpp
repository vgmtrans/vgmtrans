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
Module : CPerformance.cpp
Purpose: Code implementation for CPerformance class
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

#include "MyCoCreateInstance.h"

using namespace directmidi;

// Constructor

CPerformance::CPerformance()
{
	m_pPerformance  = NULL;
	m_pPort			= NULL;
}

// Destructor

CPerformance::~CPerformance()
{
	ReleasePerformance();
}

// Instances the performance COM object

HRESULT CPerformance::Initialize()
{
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("CPerformance::Initialize()");

	//if(FAILED(hr = CoCreateInstance(CLSID_DirectMusicPerformance,NULL,CLSCTX_INPROC, 
	//   IID_IDirectMusicPerformance8,(void**)&m_pPerformance)))
	//		throw CDMusicException(strMembrFunc,hr,__LINE__);
	hr = CoCreateInstance(CLSID_DirectMusicPerformance,NULL,CLSCTX_INPROC, 
		IID_IDirectMusicPerformance8,(void**)&m_pPerformance);
	if (hr == REGDB_E_CLASSNOTREG)
		hr = MyCoCreateInstance(_T("dmime.dll"),
		  CLSID_DirectMusicPerformance,NULL, IID_IDirectMusicPerformance8,(void**)&m_pPerformance);
	if (FAILED(hr))
		throw CDMusicException(strMembrFunc,hr,__LINE__);


    return hr;

}

// Base function to play a segment

HRESULT CPerformance::PlaySegment(CSegment &Segment,CAudioPath *pPath,DWORD dwFlags,__int64 i64StartTime)
{
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("CPerformance::PlaySegment()");
	
	SAFE_RELEASE(Segment.m_pSegmentState);
	
	CComPtr<IDirectMusicSegmentState> pSegmentState;

	if (FAILED(hr = m_pPerformance->PlaySegmentEx(Segment.m_pSegment,NULL,
		NULL,dwFlags,i64StartTime,&pSegmentState,NULL,(pPath==NULL) ? NULL:pPath->m_pPath)))
	 		throw CDMusicException(strMembrFunc,hr,__LINE__);
			
	// Gets the segmentstate8

	if (pSegmentState)
	{
		if (FAILED(hr = pSegmentState->QueryInterface(IID_IDirectMusicSegmentState8,
		   (void**)&Segment.m_pSegmentState))) throw CDMusicException(strMembrFunc,hr,__LINE__);
	} else throw CDMusicException(strMembrFunc,DM_FAILED,__LINE__);
	
	return hr;
}

// Stops a playing segment

HRESULT CPerformance::Stop(CSegment &Segment,__int64 i64StopTime,DWORD dwFlags)
{
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("CPerformance::Stop()");

	if (m_pPerformance == NULL || Segment.m_pSegment == NULL)
		throw CDMusicException(strMembrFunc,hr,__LINE__);

	if (FAILED(hr = m_pPerformance->StopEx(Segment.m_pSegment,i64StopTime,dwFlags)))
		throw CDMusicException(strMembrFunc,hr,__LINE__);

	return hr;
}

// Stops all current playing segments

HRESULT CPerformance::StopAll()
{
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("CPerformance::StopAll()");

	if (m_pPerformance == NULL) 
		throw CDMusicException(strMembrFunc,hr,__LINE__);

	if (FAILED(hr = m_pPerformance->Stop(NULL,NULL,0,0)))
		throw CDMusicException(strMembrFunc,hr,__LINE__);

	return hr;
}

// Releases and closes down the performance

HRESULT CPerformance::ReleasePerformance()
{
	if (m_pPerformance) 
	{
		m_pPerformance->CloseDown();
		SAFE_RELEASE(m_pPerformance);
		return S_OK;
	} else return S_FALSE;
}


// Sends a MIDI 1.0 message to the performance

HRESULT CPerformance::SendMidiMsg(CComPtr<IDirectMusicGraph8> &pGraph,BYTE bStatus,BYTE bByte1,BYTE bByte2,DWORD dwPChannel)
{
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("CPerformance::SendMidiMsg()");

	DMUS_MIDI_PMSG *pDMUS_MIDI_PMSG = NULL; 
 
    // Allocates memory for the message

	if (FAILED(hr = m_pPerformance->AllocPMsg(sizeof(DMUS_MIDI_PMSG),(DMUS_PMSG**)&pDMUS_MIDI_PMSG)))
		throw CDMusicException(strMembrFunc,hr,__LINE__);
    
	pDMUS_MIDI_PMSG->dwPChannel = dwPChannel;
	pDMUS_MIDI_PMSG->bStatus = bStatus;
	pDMUS_MIDI_PMSG->bByte1 = bByte1;
	pDMUS_MIDI_PMSG->bByte2 = bByte2;
	
	// Stamps the message

	hr = pGraph->StampPMsg((DMUS_PMSG*)pDMUS_MIDI_PMSG);

	if (SUCCEEDED(hr))
	{
		// Sends and frees the message

		if (FAILED(hr = m_pPerformance->SendPMsg((DMUS_PMSG*)pDMUS_MIDI_PMSG)))
		{
			m_pPerformance->FreePMsg((DMUS_PMSG*)pDMUS_MIDI_PMSG);
			throw CDMusicException(strMembrFunc,hr,__LINE__);
		}
	} else m_pPerformance->FreePMsg((DMUS_PMSG*)pDMUS_MIDI_PMSG);

	return hr;
}

// Gets the information related to a Performance channel

HRESULT CPerformance::PChannelInfo(DWORD dwPChannel,IDirectMusicPort **ppPort,DWORD *pdwGroup,DWORD *pdwMChannel)
{
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("CPerformance::PChannelInfo()");

	if (m_pPerformance == NULL)
		throw CDMusicException(strMembrFunc,hr,__LINE__);
		
	if (FAILED(hr = m_pPerformance->PChannelInfo(dwPChannel,ppPort,pdwGroup,pdwMChannel)))
		throw CDMusicException(strMembrFunc,hr,__LINE__);

	return hr;
}

// Downloads a DLS instrument to the performance port

HRESULT CPerformance::DownloadInstrument(CInstrument &Instrument,DWORD dwPChannel,DWORD *pdwGroup,DWORD *pdwMChannel)
{
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("CPerformance::DownloadInstrument()");

	if (m_pPerformance == NULL  || pdwGroup == NULL || pdwMChannel == NULL)
		throw CDMusicException(strMembrFunc,hr,__LINE__);
	
	if (FAILED(hr = m_pPerformance->DownloadInstrument(Instrument.m_pInstrument,dwPChannel,
		&Instrument.m_pDownLoadedInstrument,&Instrument.m_NoteRange,1,&m_pPort,pdwGroup,pdwMChannel)))
			throw CDMusicException(strMembrFunc,hr,__LINE__);

	Instrument.m_pPort = m_pPort; // Keeps a reference to the current port

	return hr;
}

// Unloads a DLS instrument from the performance port

HRESULT CPerformance::UnloadInstrument(CInstrument &Instrument)
{
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("CPerformance::UnloadInstrument()");

	if (m_pPerformance == NULL || m_pPort == NULL || Instrument.m_pDownLoadedInstrument == NULL)
		throw CDMusicException(strMembrFunc,hr,__LINE__);
	
	if (FAILED(hr = m_pPort->UnloadInstrument(Instrument.m_pDownLoadedInstrument)))
		throw CDMusicException(strMembrFunc,hr,__LINE__);

	return hr;
}

// Sets the master volume for the performance

HRESULT CPerformance::SetMasterVolume(long nVolume)
{
	
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("CPerformance::SetMasterVolume()");

	if (m_pPerformance == NULL)
		throw CDMusicException(strMembrFunc,hr,__LINE__);

	if (FAILED(hr = m_pPerformance->SetGlobalParam(GUID_PerfMasterVolume,
		&nVolume,sizeof(long)))) throw CDMusicException(strMembrFunc,hr,__LINE__);           

	return hr;
}

// Retrieves the master volume for the performance

HRESULT CPerformance::GetMasterVolume(long *nVolume)
{
	
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("CPerformance::GetMasterVolume()");

	if (m_pPerformance == NULL || nVolume == NULL)
		throw CDMusicException(strMembrFunc,hr,__LINE__);
  
	if (FAILED(hr = m_pPerformance->GetGlobalParam(GUID_PerfMasterVolume,
		nVolume,sizeof(long)))) throw CDMusicException(strMembrFunc,hr,__LINE__);           

	return hr;
}

// Sets the master tempo for the performance

HRESULT CPerformance::SetMasterTempo(float fTempo)
{
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("CPerformance::SetMasterTempo()");

	if (m_pPerformance == NULL)
		throw CDMusicException(strMembrFunc,hr,__LINE__);

	if (FAILED(hr = m_pPerformance->SetGlobalParam(GUID_PerfMasterTempo, 
        &fTempo, sizeof(float)))) throw CDMusicException(strMembrFunc,hr,__LINE__);                               
	
	return hr;
}

// Gets the performance master tempo

HRESULT CPerformance::GetMasterTempo(float *fTempo)
{
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("CPerformance::GetMasterTempo()");

	if (m_pPerformance == NULL || fTempo == NULL)
		throw CDMusicException(strMembrFunc,hr,__LINE__);
	
	if (FAILED(hr = m_pPerformance->GetGlobalParam(GUID_PerfMasterTempo, 
        fTempo, sizeof(float)))) throw CDMusicException(strMembrFunc,hr,__LINE__);                               
	
	return hr;
}

// Informs if a segment is playing

HRESULT CPerformance::IsPlaying(CSegment &Segment)
{
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("CPerformance::IsPlaying()");

	if (m_pPerformance == NULL || Segment.m_pSegment == NULL)
		throw CDMusicException(strMembrFunc,hr,__LINE__);

	if (FAILED(hr = m_pPerformance->IsPlaying(Segment.m_pSegment,NULL))) 
		throw CDMusicException(strMembrFunc,hr,__LINE__);                               
	
	return hr;
}

// Adjusts the internal performance time forward or backward

HRESULT CPerformance::AdjustTime(REFERENCE_TIME rtAmount)
{
	HRESULT hr;
	TCHAR strMembrFunc[] = _T("CPerformance::AdjutTime()");

	DM_ASSERT(strMembrFunc,__LINE__,m_pPerformance==NULL);
	DM_ASSERT_HR(strMembrFunc,__LINE__,hr = m_pPerformance->AdjustTime(rtAmount));
	
	return hr;
}

// Retrieves the performance latency time

HRESULT CPerformance::GetLatencyTime(REFERENCE_TIME *prtTime)
{
	HRESULT hr;
	TCHAR strMembrFunc[] = _T("CPerformance::GetLatencyTime()");

	DM_ASSERT(strMembrFunc,__LINE__,m_pPerformance==NULL,prtTime==NULL);
	DM_ASSERT_HR(strMembrFunc,__LINE__,hr = m_pPerformance->GetLatencyTime(prtTime));
	
	return hr;
}

// Sets the interval between the time when messages are sent by tracks and 
// the time when the sound is heard

HRESULT CPerformance::SetPrepareTime(DWORD dwMilliSeconds)
{
	HRESULT hr;
	TCHAR strMembrFunc[] = _T("CPerformance::SetPrepareTime()");

	DM_ASSERT(strMembrFunc,__LINE__,m_pPerformance==NULL);
	DM_ASSERT_HR(strMembrFunc,__LINE__,hr = m_pPerformance->SetPrepareTime(dwMilliSeconds));
	
	return hr;
}


// Retrieves the interval between the time when messages are sent by tracks 
// and the time when the sound is heard

HRESULT CPerformance::GetPrepareTime(DWORD *pdwMilliSeconds)
{
	HRESULT hr;
	TCHAR strMembrFunc[] = _T("CPerformance::GetPrepareTime()");

	DM_ASSERT(strMembrFunc,__LINE__,m_pPerformance==NULL,pdwMilliSeconds==NULL);
	DM_ASSERT_HR(strMembrFunc,__LINE__,hr = m_pPerformance->GetPrepareTime(pdwMilliSeconds));
	
	return hr;
}

// Retrieves the earliest time in the queue at which messages can be flushed

HRESULT CPerformance::GetQueueTime(REFERENCE_TIME *prtTime)
{
	HRESULT hr;
	TCHAR strMembrFunc[] = _T("CPerformance::GetQueueTime()");

	DM_ASSERT(strMembrFunc,__LINE__,m_pPerformance==NULL,prtTime==NULL);
	DM_ASSERT_HR(strMembrFunc,__LINE__,hr = m_pPerformance->GetQueueTime(prtTime));
	
	return hr;
}

// Resolves a given time to a given boundary

HRESULT CPerformance::GetResolvedTime(REFERENCE_TIME rtTime,REFERENCE_TIME *prtResolved,DWORD dwTimeResolveFlags)
{
	HRESULT hr;
	TCHAR strMembrFunc[] = _T("CPerformance::GetResolvedTime()");

	DM_ASSERT(strMembrFunc,__LINE__,m_pPerformance==NULL,prtResolved==NULL);
	DM_ASSERT_HR(strMembrFunc,__LINE__,hr = m_pPerformance->GetResolvedTime(rtTime,prtResolved,dwTimeResolveFlags));
	
	return hr;
}

// Retrieves the current time of the performance

HRESULT CPerformance::GetTime(REFERENCE_TIME *prtNow,MUSIC_TIME *pmtNow)
{
	HRESULT hr;
	TCHAR strMembrFunc[] = _T("CPerformance::GetTime()");

	DM_ASSERT(strMembrFunc,__LINE__,m_pPerformance==NULL);
	DM_ASSERT_HR(strMembrFunc,__LINE__,hr = m_pPerformance->GetTime(prtNow,pmtNow));
	
	return hr;
}

// Converts MUSIC_TIME to REFERENCE_TIME

HRESULT CPerformance::MusicToReferenceTime(MUSIC_TIME mtTime,REFERENCE_TIME *prtTime)
{
	HRESULT hr;
	TCHAR strMembrFunc[] = _T("CPerformance::MusicToReferenceTime()");

	DM_ASSERT(strMembrFunc,__LINE__,m_pPerformance==NULL,prtTime==NULL);
	DM_ASSERT_HR(strMembrFunc,__LINE__,hr = m_pPerformance->MusicToReferenceTime(mtTime,prtTime));
	
	return hr;
}

// Convers REFERENCE_TIME to MUSIC_TIME

HRESULT CPerformance::ReferenceToMusicTime(REFERENCE_TIME rtTime,MUSIC_TIME *pmtTime)
{
	HRESULT hr;
	TCHAR strMembrFunc[] = _T("CPerformance::ReferenceToMusicTime()");

	DM_ASSERT(strMembrFunc,__LINE__,m_pPerformance==NULL,pmtTime==NULL);
	DM_ASSERT_HR(strMembrFunc,__LINE__,hr = m_pPerformance->ReferenceToMusicTime(rtTime,pmtTime));
	
	return hr;
}

// Converts rhythm time to music time

HRESULT CPerformance::RhythmToTime(WORD wMeasure,BYTE bBeat,BYTE bGrid,short nOffset,DMUS_TIMESIGNATURE *pTimeSig,MUSIC_TIME *pmtTime)
{
	HRESULT hr;
	TCHAR strMembrFunc[] = _T("CPerformance::RhythmToTime()");

	DM_ASSERT(strMembrFunc,__LINE__,m_pPerformance==NULL,pTimeSig==NULL,pmtTime==NULL);
	DM_ASSERT_HR(strMembrFunc,__LINE__,hr = m_pPerformance->RhythmToTime(wMeasure,bBeat,bGrid,nOffset,pTimeSig,pmtTime));
	
	return hr;
}

// Converts music time to rhythm time

HRESULT CPerformance::TimeToRhythm(MUSIC_TIME mtTime,DMUS_TIMESIGNATURE *pTimeSig,WORD *pwMeasure,BYTE *pbBeat,BYTE *pbGrid,short *pnOffset)
{
	HRESULT hr;
	TCHAR strMembrFunc[] = _T("CPerformance::TimeToRhythm()");

	DM_ASSERT(strMembrFunc,__LINE__,m_pPerformance==NULL,pTimeSig==NULL,pwMeasure==NULL);
	DM_ASSERT(strMembrFunc,__LINE__,pbBeat==NULL,pbGrid==NULL,pnOffset==NULL);
	DM_ASSERT_HR(strMembrFunc,__LINE__,hr = m_pPerformance->TimeToRhythm(mtTime,pTimeSig,pwMeasure,pbBeat,pbGrid,pnOffset));
	
	return hr;
}

// Flushes all queued messages 

HRESULT CPerformance::Invalidate(MUSIC_TIME mtTime,DWORD dwFlags)
{
	HRESULT hr;
	TCHAR strMembrFunc[] = _T("CPerformance::Invalidate()");

	DM_ASSERT(strMembrFunc,__LINE__,m_pPerformance==NULL);
	DM_ASSERT_HR(strMembrFunc,__LINE__,hr = m_pPerformance->Invalidate(mtTime,dwFlags));

	return hr;
}

// Retrieves the interval between the time at which messages are placed in the port buffer 
// and the time at which they begin to be processed by the port

HRESULT CPerformance::GetBumperLength(DWORD *pdwMilliSeconds)
{
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("CPerformance::GetBumperLength()");
	
	DM_ASSERT(strMembrFunc,__LINE__,m_pPerformance==NULL,pdwMilliSeconds==NULL);
	DM_ASSERT_HR(strMembrFunc,__LINE__,hr = m_pPerformance->GetBumperLength(pdwMilliSeconds));
	
	return hr;
}

// Sets the interval between the time at which messages are placed in the port buffer and 
// the time at which they begin to be processed by the port

HRESULT CPerformance::SetBumperLength(DWORD dwMilliSeconds)
{
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("CPerformance::GetBumperLength()");
	
	DM_ASSERT(strMembrFunc,__LINE__,m_pPerformance==NULL);
	DM_ASSERT_HR(strMembrFunc,__LINE__,hr = m_pPerformance->SetBumperLength(dwMilliSeconds));
	
	return hr;
}