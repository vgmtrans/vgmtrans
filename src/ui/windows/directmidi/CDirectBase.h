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
Module : CDirectBase.h
Purpose: Defines the header implementation for the library classes
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

#ifndef CDIRECTBASE_H
#define CDIRECTBASE_H



// Some required DX headers
#include <dmusicc.h>
#include <dmusici.h>
#include <dmksctrl.h>
#include <dmdls.h>
#include <process.h>
#include <tchar.h>
#include <windows.h>
//#include "DXUTSound.h"
#include ".\\dsutil\\dsutil.h"


#pragma warning(disable:4786) 

// STL list
#include <vector>
#include <list>


// Defines utility macros

#ifndef SAFE_RELEASE
	#define SAFE_RELEASE(p) { if(p) {(p)->Release(); (p) = NULL;} }
#endif

#ifndef SAFE_DELETE
	#define SAFE_DELETE(p)  { if(p) {delete (p); (p) = NULL;} }
#endif

#ifndef SAFE_DELETE_ARRAY
	#define SAFE_DELETE_ARRAY(p) { if(p) {delete[] (p); (p)=NULL;} }
#endif

namespace directmidi
{
	const long DM_FAILED		=	-1;
	
	// Byte constants to activate synthesizer effect (only if supported)
	const BYTE SET_NOEFFECT		=   0x0;
	const BYTE SET_REVERB		=	0x1;
	const BYTE SET_CHORUS		=	0x2;
	const BYTE SET_DELAY		=	0x4;

	// Reserved constants for internal operations
	const BYTE MIDI_STRUCTURED  =	3;
	const BYTE SHIFT_8			=	8;
	const BYTE SHIFT_16			=	16;
	const BYTE BITMASK			=	15;

	// >>>>>> Main status bytes of the MIDI message system <<<<<<

	// Note off event
	const BYTE NOTE_OFF			=	0x80;

	// Note on event
	const BYTE NOTE_ON			=	0x90;

	// Program change
	const BYTE PATCH_CHANGE		=	0xC0;

	// Polyphonic Key Pressure (Aftertouch)
	const BYTE POLY_PRESSURE	=	0xA0;

	// Control Change 
	const BYTE CONTROL_CHANGE	=	0xB0;

	//Channel Pressure (After-touch).
	const BYTE CHANNEL_PREASURE =	0xD0;

	// Pitch Wheel Change
	const BYTE PITCH_BEND		=	0xE0;

	// >>>>> System Common messages <<<<<<

	// Start of exclusive
	const BYTE START_SYS_EX		=	0xF0;

	// End of exclusive
	const BYTE END_SYS_EX		=	0xF7;

	// Song position pointer
	const BYTE SONG_POINTER		=	0xF2;

	// Song select
	const BYTE SONG_SELECT		=	0xF3;

	// Tune request
	const BYTE TUNE_REQUEST		=	0xF6;

	// >>>>>> System Real-Time Messages <<<<<<

	// Timing clock
	const BYTE TIMING_CLOCK		=	0xF8;

	// Reset
	const BYTE RESET			=	0xFF;

	// Active sensing
	const BYTE ACTIVE_SENSING   =	0xFE;

	// Start the sequence
	const BYTE START			=	0xFA;

	// Stop the sequence
	const BYTE STOP				=	0xFC;

	const BOOL DM_LOAD_FROM_FILE = TRUE;
	
	const BOOL DM_USE_MEMORY     = FALSE;
	
	// Infoport structure
	typedef struct INFOPORT {
		TCHAR szPortDescription[DMUS_MAX_DESCRIPTION]; //Port description string in MBCS/UNICODE chars
		DWORD dwFlags;		// Port characteristics
		DWORD dwClass;		// Class of port (Input/Output) 
		DWORD dwType;		// Type of port (See remarks) 	
		DWORD dwMemorySize;	// Amount of memory available to store DLS instruments
		DWORD dwMaxAudioChannels;  // Maximum number of audio channels that can be rendered by the port
		DWORD dwMaxVoices;		  // Maximum number of voices that can be allocated when this port is opened
		DWORD dwMaxChannelGroups ; // Maximum number of channel groups supported by this port 
		DWORD dwSampleRate;		// Desired Sample rate in Hz; 	
		DWORD dwEffectFlags;	// Flags indicating what audio effects are available on the port
		DWORD dwfShare;		// Exclusive/shared mode
		DWORD dwFeatures;	// Miscellaneous capabilities of the port
		GUID  guidSynthGUID;	// Identifier of the port
	} *LPINFOPORT;


	// Instrument Info structure
	typedef struct INSTRUMENTINFO {
		TCHAR szInstName[MAX_PATH];	// Name of the instruments in MBCS/UNICODE chars
		DWORD dwPatchInCollection;		// Position of the instrument in the collection
	} *LPINSTRUMENTINFO;

	// Clock info structure
	typedef struct CLOCKINFO {
		DMUS_CLOCKTYPE  ctType;		
		GUID            guidClock;	// Clock GUID
		TCHAR           szClockDescription[DMUS_MAX_DESCRIPTION];
		DWORD           dwFlags;
	} *LPCLOCKINFO;

	// Synthesizer stats
	typedef struct SYNTHSTATS {
		DWORD dwSize; 
		DWORD dwValidStats;
		DWORD dwVoices;
		DWORD dwTotalCPU;
		DWORD dwCPUPerVoice;
		DWORD dwLostNotes;
		DWORD dwFreeMemory;
		long  lPeakVolume;
		DWORD dwSynthMemUse;
	} *LPSYNTHSTATS;

	
	// Region structure
	typedef struct REGION {
		RGNRANGE RangeKey;
		RGNRANGE RangeVelocity;
	} *LPREGION;

	// Articulation parameters structure
	typedef struct ARTICPARAMS {
		DMUS_LFOPARAMS LFO;
		DMUS_VEGPARAMS VolEG;
		DMUS_PEGPARAMS PitchEG;
		DMUS_MSCPARAMS Misc;
	} *LPARTICPARAMS;

	
	TCENT TimeCents(double nTime);
	PCENT PitchCents(double nFrequency);
	GCENT GainCents(double nVoltage,double nRefVoltage);
	PCENT PercentUnits(double nPercent);
	void  DM_ASSERT(TCHAR strMembrFunc[],int nLine,BOOL Param1, BOOL Param2 = false, BOOL Param3 = false);
	void  DM_ASSERT_HR(TCHAR strMembrFunc[],int nLine,HRESULT hr);

	// CDMusicException definition

	class CDMusicException
	{
		TCHAR m_strErrorBuf[512], m_strMessage[256]; // Error output strings 
	protected:	
		void CopyMsg(LPCTSTR strDefMsg,LPCTSTR strDescMsg);
		void ObtainErrorString();
	public:
		HRESULT m_hrCode;	// The HRESULT return value
		TCHAR	m_strMethod[128];	// The method where the DirectMusic function failed 
		int		m_nLine;		// The line where the DirectMusic call failed
		
		CDMusicException(LPCTSTR lpMemberFunc,HRESULT hr,int nLine) throw();
		LPCTSTR ErrorToString() throw(); // Returns the error definition
		LPCTSTR GetErrorDescription() throw(); // Returns the complete error description
	};
	
	// Class prototypes
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
}


#endif


