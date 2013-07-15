#pragma once
#include "common.h"

class MediaThread
{
public:
	MediaThread(void);
public:
	~MediaThread(void);

	bool Init();

	void Run();
	void Terminate();
    void Pause();
	static DWORD CALLBACK MainMediaThreadLoop(VOID* lpParam);

	//DWORD ProcessSeqPlayback(PVOID pParam);
	//bool ProcessSeqPlayback(vector<MidiTrack*>* pvTrack);//, vector<ULONG> vIndex)

protected:
	DWORD mainMediaThrdID;
	HANDLE mainMediaThread;

//	MusicPlayer musicPlayer;
};

extern MediaThread mediaThread;