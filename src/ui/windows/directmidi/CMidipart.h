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
Module : CMidiPart.h
Purpose: Defines the header implementation for the Midi part classes
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

#ifndef CMIDIPART_H
#define CMIDIPART_H

#include "CDirectBase.h"
using namespace directmidi;

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

	// Definition of the class instrument
	class CInstrument
	{
	friend CPerformance;
	friend CCollection;
	friend COutputPort;
		DMUS_NOTERANGE							m_NoteRange;
		// Copying and assignment not allowed
	    CInstrument(const CInstrument&);
        CInstrument &operator = (const CInstrument&);
	protected:
		IDirectMusicDownloadedInstrument8*		m_pDownLoadedInstrument; // Interface pointers
		IDirectMusicInstrument8*				m_pInstrument;	
		IDirectMusicPort8*						m_pPort;
	public:
		CInstrument();
		~CInstrument();
		HRESULT SetPatch(DWORD dwdstPatchMidi);	// Sets the instrument of the collection in a MIDI program
		void	SetNoteRange(DWORD dwLowNote,DWORD dwHighNote); // Sets the note range 
		void    GetNoteRange(LPDWORD pdwLowNote, LPDWORD pdwHighNote); // Gets the note range
		DWORD m_dwPatchInCollection;			// Information provided by the class
		DWORD m_dwPatchInMidi;					
		TCHAR m_strInstName[MAX_PATH];			// Instrument description in MBCS/UNICODE chars
	};


	// Definition of the collection class
	class CCollection
	{
	friend CDLSLoader;
	friend CSegment;
	
		// Copying and assignment not allowed
	    CCollection(const CCollection &);
        CCollection &operator = (const CCollection&);
	protected:	
		IDirectMusicCollection8*	m_pCollection;	// Interface pointer
	public:	
		CCollection();
		~CCollection();
		HRESULT EnumInstrument(DWORD dwIndex,LPINSTRUMENTINFO InstInfo);	// Enumerates instruments
		HRESULT GetInstrument(CInstrument &Instrument,LPINSTRUMENTINFO InstInfo);	// Gets the instrument
		HRESULT GetInstrument(CInstrument &Instrument,INT nIndex);
	};

	// Definition of the loader class

	class CDLSLoader
	{
		// Copying and assignment not allowed
	    CDLSLoader(const CDLSLoader&);
        CDLSLoader &operator = (const CDLSLoader&);
	protected:	
		IDirectMusicLoader8*		m_pLoader; // Interface pointer
	public:
		CDLSLoader();
		~CDLSLoader();
		HRESULT Initialize();	// Initialize the object
		HRESULT LoadDLS(LPTSTR lpFileName,CCollection &Collection);	// Loads a DLS file
		HRESULT LoadDLSFromMem(LPBYTE pDLSFile, LONGLONG llDLSSize, CCollection &Collection);
		HRESULT LoadDLSFromResource(LPTSTR strResource,LPTSTR strResourceType,CCollection &Collection);
		static HRESULT LoadWaveFile(LPTSTR lpstrFileName,CSampleInstrument &SampleInstrument,BOOL bReadAlways);
		HRESULT SetSearchDirectory(LPTSTR  pszPath,BOOL fClear); // Sets the search directory
		HRESULT UnloadCollection(CCollection &Collection);	// Unload the collection from the loader
		HRESULT LoadSegment(LPTSTR lpstrFileName,CSegment &Segment,BOOL bIsMIDIFile = FALSE); // Loads a segment
		HRESULT LoadSegmentFromResource(TCHAR *strResource,TCHAR *strResourceType,CSegment &Segment,BOOL bIsMidiFile = FALSE);
		
	};

	// CDirectMusic main class definition

	class CDirectMusic
	{
	friend CMasterClock;
	friend CMidiPort; 
	friend CPortPerformance;
	friend CAPathPerformance;
		// Copying and assignment not allowed
	    CDirectMusic(const CDirectMusic&);
        CDirectMusic &operator = (const CDirectMusic&);
	protected:	
		IDirectMusic8*			    m_pMusic8; // DirectMusic interface pointer
	public:
		CDirectMusic();					// Constructor and the destructor of the class
		~CDirectMusic();
		HRESULT Initialize(HWND hWnd = NULL,IDirectSound8* pDirectSound = NULL);	// Initialize object and DirectSound
		
	};

	// CMasterClock class definition
	
	class CMasterClock
	{
		// Copying and assignment not allowed
	    CMasterClock(const CMasterClock&);
        CMasterClock &operator = (const CMasterClock&);
	protected:
		IReferenceClock*		    m_pReferenceClock;
		IDirectMusic8*				m_pMusic8;
	public:
		CMasterClock();
		~CMasterClock();

		HRESULT Initialize(CDirectMusic &DMusic); // Initialize the object
		HRESULT ReleaseMasterClock(); // Frees the resources
		DWORD	GetNumClocks();		  // Returns the number of clocks available in the system 
		HRESULT GetClockInfo(DWORD dwNumClock,LPCLOCKINFO ClockInfo); // Get clock info
		HRESULT ActivateMasterClock(LPCLOCKINFO ClockInfo);	// Activate clock
		IReferenceClock* GetReferenceClock(); // Returns the IReferenceClock Interface
	};

	typedef BOOL (CALLBACK* MIDIPORTENUMPROC)(LPINFOPORT, LPVOID);

	class CMidiPort
	{
		DWORD					   m_dwPortType;
		// Copying and assignment not allowed
	 protected:
		IDirectMusicPort8*		   m_pPort;		// Pointer to IDirectMusicPort interface
		IDirectMusicBuffer8*	   m_pBuffer;	// Interface pointer to the buffer for incoming MIDI events 
		IDirectMusic8*			   m_pMusic8;	// Pointer to the DirectMusic interface
		IKsControl*				   m_pControl;	// Interace properties pointer
		DMUS_PORTPARAMS8		   m_PortParams; // Port features
		HRESULT ActivatePort(LPINFOPORT InfoPort,DWORD dwSysExSize,BOOL bFromInterface); // Activate the MIDI port
	public:
		CMidiPort(DWORD dwType);
		~CMidiPort();
		HRESULT	Initialize(CDirectMusic &DMusic); // Initialize the ports
		DWORD	GetNumPorts();	// Returns the number of midi ports
		HRESULT GetPortInfo(DWORD dwNumPort,LPINFOPORT lpInfoPort);	// Gets port info
		HRESULT GetActivatedPortInfo(LPINFOPORT lpInfoPort);
		HRESULT KsProperty(GUID Set,ULONG Id,ULONG Flags,LPVOID pvPropertyData,ULONG ulDataLength,PULONG pulBytesReturned);
		HRESULT SetBuffer(DWORD dwBufferSize);
		HRESULT RemoveBuffer();

		// Additional functions
		void EnumPort(MIDIPORTENUMPROC lpEnumFunc, LPVOID lParam);
	};

	// Receiver class

	class CReceiver
	{
	public:
		// Virtual functions to receive midi messages, structured and unstructured
		virtual void RecvMidiMsg(REFERENCE_TIME rt,DWORD dwChannel,DWORD dwBytesRead,BYTE *lpBuffer)=0; 
		virtual void RecvMidiMsg(REFERENCE_TIME rt,DWORD dwChannel,DWORD dwMsg)=0;
	};
	
	// CInputPort class definition

	class CInputPort:public CMidiPort
	{
		typedef	std::vector<WORD> VECTOR;	
		HANDLE					   m_hEvent,m_hTerminationEvent;	// Event handler	
		HANDLE					   m_hThread;
		BOOL					   m_ProcessActive;	// Thread exit condition
		std::list<VECTOR> m_ChannelList;	// Channel STL list	   
		std::list<VECTOR>::iterator m_ChannelIterator;	// STL list iterator
		// Copying and assignment not allowed
	    CInputPort(const CInputPort&);
        CInputPort &operator = (const CInputPort&);

	protected:	
		CReceiver*				   m_pReceiver; // Pointer to CReceiver object
		IDirectMusicThru8*         m_pThru;		// Pointer to IdirectMusicThru interface
		HRESULT ReadBuffer();					// Read the buffer
		HRESULT ResetBuffer();					// Reset the buffer pointer
		HRESULT GetMidiEvent(REFERENCE_TIME *lprt,DWORD *lpdwGroup,DWORD *lpcb,BYTE **lppb); // Extract an event
		HRESULT BreakAllThru();
	public:
		CInputPort();										// The constructor and the destructor of the class
		~CInputPort();									    // Public member functions exposed by the class 						
		HRESULT ActivatePort(LPINFOPORT InfoPort,DWORD dwSysExSize = 32);
		HRESULT ActivatePortFromInterface(IDirectMusicPort8* pPort,DWORD dwSysExSize = 32);
		HRESULT ActivateNotification();	// Activate the event notification 
		BOOL	SetThreadPriority(int nPriority); // Sets the thread priority 
		HRESULT SetReceiver(CReceiver &Receiver);
		HRESULT ReleasePort();			// Releases the references to the interfaces	
		HRESULT SetThru(DWORD dwSourceChannel,DWORD dwDestinationChannelGroup,DWORD dwDestinationChannel,COutputPort &dstMidiPort); //Activate Thru
		HRESULT BreakThru(DWORD dwSourceChannel,DWORD dwDestinationChannelGroup,DWORD dwDestinationChannel);	//Desactivate Thru
		HRESULT TerminateNotification();				// Terminate the event notification	
		static DWORD WINAPI WaitForEvent(LPVOID lpv);	// Main thread for reading events	
		static void DecodeMidiMsg(DWORD dwMsg,BYTE *Status,BYTE *DataByte1,BYTE *DataByte2); // Decode a Midi Message		
		static void DecodeMidiMsg(DWORD Msg,BYTE *Command,BYTE *Channel,BYTE *DataByte1,BYTE *DataByte2);
	};
													
	// COutputPort class definition

	class COutputPort:public CMidiPort
	{
	friend CInputPort;
	friend CPortPerformance;
		// Copying and assignment not allowed
	    COutputPort(const COutputPort&);
        COutputPort &operator = (const COutputPort&);
	protected:	
		IReferenceClock*					m_pClock;	// Pointer to the Reference Clock
		IDirectMusicPortDownload8*			m_pPortDownload;
	public:
		COutputPort();										// Constructor and the destructor of the class
		~COutputPort();   											
		HRESULT ActivatePort(LPINFOPORT InfoPort,DWORD dwSysExSize = 32);
		HRESULT ActivatePortFromInterface(IDirectMusicPort8* pPort,DWORD dwSysExSize = 32);
		void	SetPortParams(DWORD dwVoices,DWORD dwAudioChannels,DWORD dwChannelGroups,DWORD dwEffectFlags,DWORD dwSampleRate);
		HRESULT ReleasePort();	// Releases the references to the interfaces
		HRESULT AllocateMemory(CSampleInstrument &SampleInstrument);
		HRESULT DownloadInstrument(CInstrument &Instrument);	// Download an instrument to the port
		HRESULT DownloadInstrument(CSampleInstrument &SampleInstrument);
		HRESULT UnloadInstrument(CInstrument &Instrument);	// Unload an instrument from the port
		HRESULT UnloadInstrument(CSampleInstrument &SampleInstrument); // Unload a sample instrument
		HRESULT DeallocateMemory(CSampleInstrument &SampleInstrument); // Frees the memory reserved by the interfaces
		HRESULT CompactMemory(); // Compact wave-table memory
		HRESULT GetChannelPriority(DWORD dwChannelGroup,DWORD dwChannel,LPDWORD pdwPriority);
		HRESULT SetChannelPriority(DWORD dwChannelGroup,DWORD dwChannel,DWORD dwPriority);
		HRESULT GetSynthStats(LPSYNTHSTATS SynthStats);
		HRESULT GetFormat(LPWAVEFORMATEX pWaveFormatEx,LPDWORD pdwWaveFormatExSize,LPDWORD pdwBufferSize);
		HRESULT GetNumChannelGroups(LPDWORD pdwChannelGroups);
		HRESULT SetNumChannelGroups(DWORD dwChannelGroups);
		HRESULT SetEffect(BYTE bEffect);	// Activate midi effects
		HRESULT SendMidiMsg(DWORD dwMsg,DWORD dwChannelGroup);	// Sends a message to the port in DWORD format
		HRESULT SendMidiMsg(LPBYTE lpMsg,DWORD dwLength,DWORD dwChannelGroup);	// Sends an unstrucuted message to the port
		IReferenceClock* GetReferenceClock(); // Returns the IReferenceClock interface
		static DWORD EncodeMidiMsg(BYTE Status,BYTE DataByte1,BYTE DataByte2); // Conversion functions	
		static DWORD EncodeMidiMsg(BYTE Command,BYTE Channel,BYTE DataByte1,BYTE DataByte2);
	};
													
	// CSampleInstrument class definition
	
	class CSampleInstrument
	{
	friend CDLSLoader;
	friend COutputPort;	
		
		// Internal DLS 1.0 structures 
	
		struct INSTRUMENT_DOWNLOAD
		{	
			DMUS_DOWNLOADINFO dlInfo;
			ULONG			  ulOffsetTable[4];
			DMUS_INSTRUMENT   dmInstrument;
			DMUS_REGION		  dmRegion;
			DMUS_ARTICULATION dmArticulation;
			DMUS_ARTICPARAMS  dmArticParams;
		};
		
		struct WAVE_DOWNLOAD
		{
			DMUS_DOWNLOADINFO dlInfo;
			ULONG			  ulOffsetTable[2];
			DMUS_WAVE		  dmWave;
			DMUS_WAVEDATA	  dmWaveData;
		};

		// Private member variables

		REGION								m_Region;
		ARTICPARAMS							m_Articparams;
		WAVEFORMATEX						m_wfex;
		BYTE*								m_pRawData;
		DWORD								m_dwSize;
		DWORD								m_dwPatch;
		BOOL								m_bReadAlways;
		BOOL								m_bFileRead;
		BOOL								m_bWaveFormSet;
		BOOL								m_bLoop;
		LONG								m_lAttenuation;
		SHORT								m_sFineTune;
		USHORT								m_usUnityNote;
		ULONG								m_fulOptions;
		TCHAR								m_lpstrFileName[_MAX_FNAME];

		// Private internal member functions

		BOOL InitializeInstDownload(INSTRUMENT_DOWNLOAD *pInstDownload,DWORD dwDLId,DWORD dwDLIdWave);
		BOOL InitializeWaveDownload(WAVE_DOWNLOAD *pWaveDownload,DWORD dwDLId,WAVEFORMATEX *pwfex,DWORD dwWaveSize,DWORD dwOverallSize);
		HRESULT AllocateInstBuffer();
		HRESULT AllocateWaveBuffer(DWORD dwAppend);
		HRESULT GetWaveBuffer(DWORD dwDLId);
		HRESULT GetInstBuffer(DWORD dwDLId);
		
		// Copying and assignment not allowed
	    
		CSampleInstrument(const CSampleInstrument&);
        CSampleInstrument &operator = (const CSampleInstrument&);

		
	protected:
		// DLS interfaces 
		IDirectMusicDownload8*				m_pArticulationDownload;
		IDirectMusicDownload8*				m_pWaveDownload;
		
		// Port download required interface
		IDirectMusicPortDownload8*			m_pPortDownload;
	public:
		CSampleInstrument();		// Constructor/Destructor	
		~CSampleInstrument();
		void SetPatch(DWORD dwPatch);	// Sets the patch in a MIDI program
		void SetWaveForm(BYTE *pRawData,WAVEFORMATEX *pwfex,DWORD dwSize); // Sets s waveform data
		void GetWaveForm(BYTE **pRawData,WAVEFORMATEX *pwfex,DWORD *dwSize); // Returns a pointer to waveform data
		void SetLoop(BOOL bLoop); // Loops a sample 
		void SetWaveParams(LONG lAttenuation,SHORT sFineTune,USHORT usUnityNote,ULONG fulOptions);
		void GetWaveParams(LONG *plAttenuation,SHORT *psFineTune,USHORT *pusUnityNote,ULONG *pfulOptions);
		DWORD GetWaveFormSize(); 
		void SetRegion(REGION *pRegion);
		void SetArticulationParams(ARTICPARAMS *pArticParams);
		void GetRegion(REGION *pRegion);
		void GetArticulationParams(ARTICPARAMS *pArticParams);
		HRESULT ReleaseSample(); // Frees the resources adquired by the sample
				
	};
}

#endif												// End of class definitions