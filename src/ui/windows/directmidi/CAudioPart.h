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
Module : CAudioPart.h
Purpose: Defines the header implementation for the Audio part classes
Created: CJP / 02-08-2002
History: CJP / 20-02-2004 
Date format: Day-month-year
	
	Update: 20/09/2002

	1. Improved class destructors

	2. Check member variables for NULL values before performing an operation
		
	3. Restructured the class system
	
	4. Better method to enumerate and select MIDI ports
	
	5. Adapted the external thread to a  virtual function

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

#ifndef CAUDIOPART_H
#define CAUDIOPART_H

#include "CDirectBase.h"
#include "atlbase.h"

namespace directmidi
{
	
	class CInstrument;
	class CCollection;
	class CDLSLoader;
	class CMidiPort;
	class CReceiver;
	class CInputPort;
	class COutputPort;
	class CDirectMusic;
	class CSampleInstrument;
	class CMasterClock;
	class CSegment;
	class CPerformance;
	class CAPathPerformance;
	class CPortPerformance;
	class CAudioPath;
	class CSegment;
	class C3DSegment;
	class C3DListener;
	class C3DBuffer;

	// Performance base class definition

	class CPerformance
	{
	friend CSegment;
		// Copying and assignment not allowed
	    
		CPerformance(const CPerformance&);
        CPerformance &operator = (const CPerformance&);

	protected:
		IDirectMusicPort8*					m_pPort; // Internal port interface
		IDirectMusicPerformance8*			m_pPerformance; // Performance interface
		HRESULT Initialize();				
		HRESULT ReleasePerformance();		
		HRESULT PlaySegment(CSegment &pSegment,CAudioPath *pPath,DWORD dwFlags = 0,__int64 i64StartTime = 0);
		HRESULT SendMidiMsg(CComPtr<IDirectMusicGraph8> &pGraph,BYTE bStatus,BYTE bByte1,BYTE bByte2,DWORD dwPChannel);
	public:
		CPerformance();
		~CPerformance();
		
		// Public member functions

		HRESULT Stop(CSegment &Segment,__int64 i64StopTime = 0,DWORD dwFlags = 0); 
		HRESULT StopAll();
		HRESULT SetMasterVolume(long nVolume);
		HRESULT SetMasterTempo(float fTempo);
		HRESULT GetMasterVolume(long *nVolume);
		HRESULT GetMasterTempo(float *nTempo);
		HRESULT IsPlaying(CSegment &Segment);
		HRESULT AdjustTime(REFERENCE_TIME rtAmount);
		HRESULT GetLatencyTime(REFERENCE_TIME *prtTime);
		HRESULT GetPrepareTime(DWORD *pdwMilliSeconds);
		HRESULT GetQueueTime(REFERENCE_TIME *prtTime);
		HRESULT GetResolvedTime(REFERENCE_TIME rtTime,REFERENCE_TIME *prtResolved,DWORD dwTimeResolveFlags);
		HRESULT GetTime(REFERENCE_TIME *prtNow,MUSIC_TIME *pmtNow);
		HRESULT MusicToReferenceTime(MUSIC_TIME mtTime,REFERENCE_TIME *prtTime);
		HRESULT ReferenceToMusicTime(REFERENCE_TIME rtTime,MUSIC_TIME *pmtTime);
		HRESULT RhythmToTime(WORD wMeasure,BYTE bBeat,BYTE bGrid,short nOffset,DMUS_TIMESIGNATURE *pTimeSig,MUSIC_TIME *pmtTime);
		HRESULT SetPrepareTime(DWORD dwMilliSeconds);
		HRESULT TimeToRhythm(MUSIC_TIME mtTime,DMUS_TIMESIGNATURE *pTimeSig,WORD *pwMeasure,BYTE *pbBeat,BYTE *pbGrid,short *pnOffset);
		HRESULT Invalidate(MUSIC_TIME mtTime,DWORD dwFlags);
		HRESULT GetBumperLength(DWORD *pdwMilliSeconds);
		HRESULT SetBumperLength(DWORD dwMilliSeconds);
		HRESULT PChannelInfo(DWORD dwPChannel,IDirectMusicPort **ppPort,DWORD *pdwGroup,DWORD *pdwMChannel);
		HRESULT DownloadInstrument(CInstrument &Instrument,DWORD dwPChannel,DWORD *pdwGroup,DWORD *pdwMChannel);
		HRESULT UnloadInstrument(CInstrument &Instrument);
	};
	
	// AudioPath performance definition

	class CAPathPerformance:public CPerformance
	{
		// Copying and assignment not allowed

	    CAPathPerformance(const CAPathPerformance&);
        CAPathPerformance &operator = (const CAPathPerformance&);

	public:
		CAPathPerformance();
		~CAPathPerformance();
		
		// Public member functions

		HRESULT Initialize(CDirectMusic &DMusic,IDirectSound **ppDirectSound,HWND hWnd,DWORD dwDefaultPathType = DMUS_APATH_SHARED_STEREOPLUSREVERB,
		DWORD dwPChannelCount = 64,DWORD dwFlags = 0,DMUS_AUDIOPARAMS *pAudioParams = NULL);
		HRESULT CreateAudioPath(CAudioPath &Path,DWORD dwType,DWORD dwPChannelCount,BOOL bActivate);
		HRESULT PlaySegment(CSegment &Segment,CAudioPath *Path,DWORD dwFlags = 0,__int64 i64StartTime = 0);
		HRESULT PlaySegment(C3DSegment &Segment,DWORD dwFlags = 0,__int64 i64StartTime = 0);
		HRESULT GetDefaultAudioPath(CAudioPath &Path);
		HRESULT SetDefaultAudioPath(CAudioPath &Path);
		HRESULT RemoveDefaultAudioPath();
		HRESULT ReleasePerformance();
		HRESULT SendMidiMsg(BYTE bStatus,BYTE bByte1,BYTE bByte2,DWORD dwPChannel,CAudioPath &Path);
	};

	// Port performance definition

	class CPortPerformance:public CPerformance
	{
		// Copying and assignment not allowed
	    
		CPortPerformance(const CPortPerformance&);
        CPortPerformance &operator = (const CPortPerformance&);
	protected:
		BOOL		m_bAddedPort;
	public:
		CPortPerformance();
		~CPortPerformance();
		HRESULT Initialize(CDirectMusic &DMusic,IDirectSound *pDirectSound,HWND hWnd);
		HRESULT AddPort(COutputPort &OutPort,DWORD dwBlockNum,DWORD dwGroup);
		HRESULT RemovePort();
		HRESULT PlaySegment(CSegment &Segment,DWORD dwFlags = 0,__int64 i64StartTime = 0);
		HRESULT ReleasePerformance();
		HRESULT AssignPChannel(COutputPort &OutPort,DWORD dwPChannel,DWORD dwGroup,DWORD dwMChannel);
		HRESULT AssignPChannelBlock(COutputPort &OutPort,DWORD dwBlockNum,DWORD dwGroup);
		HRESULT SendMidiMsg(BYTE bStatus,BYTE bByte1,BYTE bByte2,DWORD dwPChannel);
	};

	// AudioPath class definition

	class CAudioPath
	{
	friend CPerformance;
	friend CAPathPerformance;
	friend C3DSegment;

	protected:
		IDirectMusicAudioPath*	m_pPath;
	public:
		CAudioPath();
		~CAudioPath();
		HRESULT Activate(BOOL bActivate);
		HRESULT SetVolume(long lVolume,DWORD dwDuration);
		HRESULT Get3DBuffer(C3DBuffer &_3DBuffer);	
		HRESULT Get3DListener(C3DListener &_3DListener);
		HRESULT ReleaseAudioPath();
		HRESULT GetObjectInPath(DWORD dwPChannel, DWORD dwStage, DWORD dwBuffer, REFGUID guidObject,
		DWORD dwIndex,REFGUID iidInterface,void **ppObject);
	};
	
	// 3D Buffer class definition
	
	class C3DBuffer
	{
	friend CAudioPath;
		
		// Copying and assignment not allowed
	    
		C3DBuffer(const C3DBuffer&);
        C3DBuffer &operator = (const C3DBuffer&);

	protected:
		IDirectSound3DBuffer8*	m_pBuffer; // Buffer interface pointer
	public:
		C3DBuffer();
		~C3DBuffer();
		
		// Public member functions

		HRESULT GetMode(LPDWORD pdwMode);
		HRESULT SetMode(DWORD dwMode,DWORD dwApply = DS3D_IMMEDIATE);
		HRESULT GetAllBufferParameters(LPDS3DBUFFER pDs3dBuffer);
		HRESULT SetAllBufferParameters(LPCDS3DBUFFER pcDs3dBuffer,DWORD dwApply = DS3D_IMMEDIATE);
		HRESULT GetMaxDistance(D3DVALUE *pflMaxDistance);
		HRESULT GetMinDistance(D3DVALUE *pflMinDistance);
		HRESULT SetMaxDistance(D3DVALUE flMaxDistance,DWORD dwApply = DS3D_IMMEDIATE);
		HRESULT SetMinDistance(D3DVALUE flMinDistance,DWORD dwApply = DS3D_IMMEDIATE);
		HRESULT GetBufferPosition(D3DVECTOR *pvPosition);
		HRESULT SetBufferPosition(D3DVALUE x,D3DVALUE y,D3DVALUE z,DWORD dwApply = DS3D_IMMEDIATE);
		HRESULT GetConeAngles(LPDWORD pdwInsideConeAngle,LPDWORD pdwOutsideConeAngle);
		HRESULT GetConeOrientation(D3DVECTOR *pvOrientation);
		HRESULT GetConeOutsideVolume(LPLONG plConeOutsideVolume);
		HRESULT SetConeAngles(DWORD dwInsideConeAngle,DWORD dwOutsideConeAngle,DWORD dwApply = DS3D_IMMEDIATE);
		HRESULT SetConeOrientation(D3DVALUE x,D3DVALUE y,D3DVALUE z,DWORD dwApply = DS3D_IMMEDIATE);
		HRESULT SetConeOutsideVolume(LONG lConeOutsideVolume,DWORD dwApply = DS3D_IMMEDIATE);
		HRESULT GetVelocity(D3DVECTOR *pvVelocity);
		HRESULT SetVelocity(D3DVALUE x,D3DVALUE y,D3DVALUE z,DWORD dwApply = DS3D_IMMEDIATE);
		HRESULT ReleaseBuffer();
	};

	// 3D Listener class definition

	class C3DListener
	{
	friend CAudioPath;
		
		// Copying and assignment not allowed
	    
		C3DListener(const C3DListener&);
        C3DListener &operator = (const C3DListener&);

	protected:
		IDirectSound3DListener8*	m_pListener; // Listener interface pointer
	public:
		C3DListener();
		~C3DListener();
		
		// Public member functions

		HRESULT CommitDeferredSettings();
		HRESULT GetAllListenerParameters(LPDS3DLISTENER pListener);
		HRESULT SetAllListenerParameters(LPCDS3DLISTENER pcListener,DWORD dwApply = DS3D_IMMEDIATE);
		HRESULT GetDistanceFactor(D3DVALUE *pflDistanceFactor);
		HRESULT GetDopplerFactor(D3DVALUE *pflDopplerFactor);
		HRESULT GetRolloffFactor(D3DVALUE *pflRolloffFactor);
		HRESULT SetDistanceFactor(D3DVALUE flDistanceFactor,DWORD dwApply = DS3D_IMMEDIATE);
		HRESULT SetDopplerFactor(D3DVALUE flDopplerFactor,DWORD dwApply = DS3D_IMMEDIATE);
		HRESULT SetRolloffFactor(D3DVALUE flRolloffFactor,DWORD dwApply = DS3D_IMMEDIATE);
		HRESULT GetOrientation(D3DVECTOR *pvOrientFront,D3DVECTOR *pvOrientTop);
		HRESULT GetListenerPosition(D3DVECTOR *pvPosition);
		HRESULT GetVelocity(D3DVECTOR *pvVelocity);
		HRESULT SetOrientation(D3DVALUE xFront,D3DVALUE yFront,D3DVALUE zFront,D3DVALUE xTop,
		D3DVALUE yTop,D3DVALUE zTop,DWORD  dwApply);
		HRESULT SetListenerPosition(D3DVALUE x,D3DVALUE y,D3DVALUE z,DWORD dwApply = DS3D_IMMEDIATE);
		HRESULT SetVelocity(D3DVALUE x,D3DVALUE y,D3DVALUE z,DWORD dwApply = DS3D_IMMEDIATE);
		HRESULT ReleaseListener();
	};	

	// Segment class definition

	class CSegment
	{
	friend CDLSLoader;
	friend CPerformance;
	friend CAPathPerformance;
	friend CPortPerformance;
	friend C3DSegment;
		
		// Assignment not allowed
	    
		CSegment(const CSegment&);
   
	protected:
		IDirectMusicSegment8*		m_pSegment;		// Segment interface pointer
		IDirectMusicSegmentState8*  m_pSegmentState; // Segment state interface pointer
		IDirectMusicPerformance8*	m_pPerformance;	 // Pointer to the active performance
		std::list<IDirectMusicPerformance8*> m_PerformanceList; // Performance STL list	   
		std::list<IDirectMusicPerformance8*>::iterator m_PerformanceIterator;	// STL list iterator
	public:
		CSegment();
		~CSegment();
		
		// Public member functions
		
		HRESULT Download(CPerformance &Performance);
		HRESULT Unload(CPerformance &Performance);
		HRESULT UnloadAllPerformances();
		HRESULT ConnectToDLS(CCollection &Collection);
		HRESULT GetRepeats(DWORD *pdwRepeats);
		HRESULT GetSeek(MUSIC_TIME *pmtSeek);
		HRESULT GetSegment(IDirectMusicSegment **ppSegment);
		HRESULT GetStartPoint(MUSIC_TIME *pmtStart);
		HRESULT GetStartTime(MUSIC_TIME *pmtStart);
		HRESULT GetDefaultResolution(DWORD *pdwResolution);
		HRESULT GetLength(MUSIC_TIME *pmtLength);
		HRESULT GetLoopPoints(MUSIC_TIME *pmtStart,MUSIC_TIME *pmtEnd);
		HRESULT SetDefaultResolution(DWORD dwResolution);
		HRESULT SetLength(MUSIC_TIME mtLength);
		HRESULT SetLoopPoints(MUSIC_TIME mtStart,MUSIC_TIME mtEnd);
		HRESULT SetRepeats(DWORD dwRepeats);
		HRESULT SetStartPoint(MUSIC_TIME mtStart);
		HRESULT SetPChannelsUsed(DWORD dwNumPChannels,DWORD *paPChannels);
		HRESULT ReleaseSegment();
		CSegment& operator = (const CSegment &Segment);
	};
	
	// 3D Segment class definition
	
	class C3DSegment:public CSegment,public C3DBuffer,public C3DListener
	{
	friend CAPathPerformance;
		
		// Assignment not allowed
	    
		C3DSegment(const C3DSegment&);
		BOOL m_bExternalAPath;
	protected:
		CAudioPath m_AudioPath;		     // Internal audiopath object
	public:
		C3DSegment();
		~C3DSegment();
		
		// Public member functions
		
		HRESULT Initialize(CAPathPerformance &Performance,DWORD dwType = DMUS_APATH_DYNAMIC_3D,DWORD dwPChannelCount = 64);
		HRESULT Initialize(CAudioPath &Path);
		HRESULT ReleaseSegment();
		HRESULT SetVolume(long lVolume,DWORD dwDuration);
		HRESULT GetObjectInPath(DWORD dwPChannel, DWORD dwStage, DWORD dwBuffer, REFGUID guidObject,
		DWORD dwIndex,REFGUID iidInterface,void **ppObject);
		CAudioPath& GetAudioPath();
		C3DSegment& operator = (const C3DSegment &Segment);
		C3DSegment& operator = (const CSegment &Segment);
	};

}
#endif							// End of class definitions
