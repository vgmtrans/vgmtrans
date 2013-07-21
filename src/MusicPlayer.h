#pragma once

#include ".\\DirectMidi\\CDirectMidi.h"
#include "common.h"
#include "MidiFile.h"

class VGMItem;
class VGMSeq;
class DLSFile;

/////////////////////////////////////////////////////////////////////////////
// CDirectInputPort class
// Derived from CInputport class

class CMusicReceiver:public CReceiver
{
public:
	void RecvMidiMsg(REFERENCE_TIME rt,DWORD dwChannel,DWORD dwBytesRead,BYTE *lpBuffer) {};
	void RecvMidiMsg(REFERENCE_TIME rt,DWORD dwChannel,DWORD dwMsg);
};




class MusicPlayer
{
public:
	MusicPlayer(void);
public:
	~MusicPlayer(void);

	bool Init(HWND hWnd);
	void CloseDown(void);
	void PrepareSoftwareSynth(void);
	//bool ChangeInstrument(BYTE instrNum);
	void InitializeEventHandler();
	void EnumPorts();

	void ChangeDLS(DLSFile* dlsfile);

	void Play(VGMItem* pItem, ULONG absTime);
    void Pause();
	void Stop();
	void ReleaseAllKeys();
	void SetWriteLatency(DWORD dwLatency);
	void SetWritePeriod(DWORD dwPeriod);
	void SetupReverb(VGMSeq* vgmseq);
	void SetVolume(long vol);
	void ResetMidi();
	static DWORD CALLBACK ProcessSeqPlayback(VOID* lpParam);
	static void CALLBACK CallbackTestFunc(UINT idEvent, void* pUserContext);
	//DWORD ProcessSeqPlayback(PVOID pParam);
	//bool ProcessSeqPlayback(vector<MidiTrack*>* pvTrack);//, vector<ULONG> vIndex)

protected:
	MidiFile* midi;
	DWORD playbackThrdID;
	HANDLE playbackThread;
	HANDLE stopPlaybackEvent;					// The event
    HANDLE unpausedPlaybackEvent;					// The event

	bool bPlaying;
	bool bPaused;

	bool m_bSwSynth;

	vector<CInstrument*> vpInstruments;

	//CCriticalSection CS;		// Critical Section for synchronization 

	// Declaration of the main objects
	CDirectMusic	 CMusic8;
	CPortPerformance CPortPerformance;
	COutputPort		 COutPort;
	//CInputPort       CInPort;
	CDLSLoader		 DLSLoader;
	CCollection		 Collection;
	CMusicReceiver   Receiver;

	
	//CPortPerformance  CPortPerformance;
	//CSegment		  CSegment1;

	/*CDirectMusic	  CDMusic;
	CDLSLoader		  CLoader;
	COutputPort		  COutPort,COutPort2;
	CInputPort		  CInPort;
	CPortPerformance  CPortPerformance;
	CAPathPerformance CAPathPerformance;
	CCollection		  CCollectionA;
	CInstrument		  CInstrument1;
	CAudioPath		  CAudioPath1;
	CSegment		  CSegment1;
	C3DSegment		  C3DSegment1;
	C3DBuffer		  C3DBuffer1;*/
};

extern MusicPlayer musicplayer;