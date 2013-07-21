#include "stdafx.h"
#include "MusicPlayer.h"
#include "osdepend.h"
#include "VGMSeq.h"
#include "DLSFile.h"
#include "LogItem.h"
#include "Root.h"

#include "MainFrm.h"

using namespace std;
using namespace directmidi;

#define NUM_CHANNEL_GROUPS 5

MusicPlayer musicplayer;

MusicPlayer::MusicPlayer(void)
: bPlaying(false),
  bPaused(false)
{
}

MusicPlayer::~MusicPlayer(void)
{
	//DeleteVect(vpInstruments);

	for (UINT i=0; i<vpInstruments.size(); i++)
	{
		COutPort.UnloadInstrument(*vpInstruments[i]); // Unloads the instrument from the port
		delete vpInstruments[i];
	}
	vpInstruments.clear();
}


bool MusicPlayer::Init(HWND hWnd)
{
	try{
		CMusic8.Initialize(hWnd);
	} catch (CDMusicException CDMusicEx)
	{
	//	Alert(L"Can't initialize DirectMusic objects.\n" \
		L"Ensure you have an audio card device and DirectX 8.0 or above " \
		L"installed in your system.");
		OutputDebugString(CDMusicEx.GetErrorDescription());
		return false;
	}

	try
	{
		CPortPerformance.Initialize(CMusic8,NULL,NULL);
		//CInPort.Initialize(CMusic8);   // Initialize the input port	
		//COutPort.Initialize(CMusic8);  // Initialize the output port	
		DLSLoader.Initialize();		   // Initialize the Loader object
		EnumPorts();				   // Enumerate ports and select the default one

		//CPortPerformance.Initialize(CMusic8,NULL,NULL);
		CPortPerformance.AddPort(COutPort,0,1);
		for (int i=1; i<NUM_CHANNEL_GROUPS; i++)
			CPortPerformance.AssignPChannelBlock(COutPort, i, i+1);

		//myHr = CPortPerformance.AddPort(COutPort,1,2);
		//SetVolume(50);
		CPortPerformance.SetMasterVolume(-600);//SetVolSetMasterVolume(100);
		SetWriteLatency(100);
		SetWritePeriod(10);

		//CInPort.SetReceiver(Receiver);
		//CInPort.ActivateNotification();	// Activates event notification
		//CInPort.SetThru(0,1,0,COutPort);// Activates the Midi thru with the default output port
	} catch(CDMusicException CDMusicEx)		// Gets the exception
	{
		switch(CDMusicEx.m_hrCode)
		{
		case DM_FAILED:
			break;
		default:
			OutputDebugString(CDMusicEx.GetErrorDescription());
			Alert(L"Error during the DirectMidi objects initialization");
			CloseDown(); // Couldn't initialize DirectMusic
		}
		OutputDebugString(CDMusicEx.GetErrorDescription());
	}

	InitializeEventHandler();

	//COutPort.SendMidiMsg(COutputPort::EncodeMidiMsg(0x90,0x40, 0x7F), 1);


	/*DWORD dwMsg = COutputPort::EncodeMidiMsg(0x90,0,0x3C,0x7F);
	try
	{
		COutPort.SendMidiMsg(dwMsg,0);		// Send it
	}
	catch(CDMusicException CDMusicEx)
	{
		OutputDebugString(CDMusicEx.GetErrorDescription());
	}*/

	return true;

	/*/////////////////////////////// INITIALIZATION //////////////////////////

	// Initializes DirectMusic

	CDMusic.Initialize();
	
	// Initializes an audiopath performance

	CAPathPerformance.Initialize(CDMusic,NULL,NULL,DMUS_APATH_DYNAMIC_3D,128);
	
	// Initializes a port performance object

	CPortPerformance.Initialize(CDMusic,NULL,NULL);
	
	// Initializes loader object

	CLoader.Initialize();
	
	// Initializes output port

	COutPort.Initialize(CDMusic);
	
	// Selects the first software synthesizer port

	INFOPORT PortInfo;
	DWORD dwPortCount = 0;
		

	do
		COutPort.GetPortInfo(++dwPortCount,&PortInfo);
	while (!(PortInfo.dwFlags & DMUS_PC_SOFTWARESYNTH));

	
	//cout << "Selected output port: " << PortInfo.szPortDescription << endl; 

	COutPort.SetPortParams(0,0,0,SET_REVERB | SET_CHORUS,44100);
	COutPort.ActivatePort(&PortInfo);


	// Adds the selected port to the performance

	CPortPerformance.AddPort(COutPort,0,1);




		
		// Loads a MIDI file into the segment

		//CLoader.LoadSegment(_T("laststar.mid"),CSegment1,TRUE);
		
		// Repeats the segment until infinite

	//	CSegment1.SetRepeats(DMUS_SEG_REPEAT_INFINITE); 
		
		// Downloads the segment to the performance

		CSegment1.Download(CPortPerformance);
		
		// Plays the segment

	//	CPortPerformance.PlaySegment(CSegment1);

	//	CPortPerformance.Stop(CSegment1);

		CPortPerformance.SendMidiMsg(NOTE_ON,64,127,1);*/
}

void MusicPlayer::EnumPorts()
{
	INFOPORT Info;
	DWORD dwNumOutPorts = COutPort.GetNumPorts();
	//DWORD dwNumInPorts = CInPort.GetNumPorts();
	//DWORD dwCountInPorts = 0;
	BOOL bSelected = FALSE;

	// List all output ports
	DWORD nPortCount;

	for(nPortCount = 1; nPortCount<=dwNumOutPorts; nPortCount++)
	{
		COutPort.GetPortInfo(nPortCount,&Info);
		//m_OutPortList.AddString(Info.szPortDescription);
		//m_OutPortList.SetItemData(nPortCount - 1,Info.dwFlags);
		if ((Info.dwFlags & DMUS_PC_DLS) && (Info.dwFlags & DMUS_PC_DLS2))
		{
			if (!bSelected) // Select the port
			{	
				//Info.dwMaxChannelGroups = 5;
				COutPort.SetPortParams(Info.dwMaxVoices, Info.dwMaxAudioChannels, NUM_CHANNEL_GROUPS, Info.dwEffectFlags, 44100);
				COutPort.ActivatePort(&Info);
				bSelected = TRUE;
			//	m_nOutPortSel = nPortCount - 1;
			//	m_OutPortList.SetCurSel(m_nOutPortSel);
				if ((Info.dwFlags & DMUS_PC_DLS) || (Info.dwFlags & DMUS_PC_DLS2))
				{	
					m_bSwSynth = TRUE; // It's working with a Sw. Synth.
					PrepareSoftwareSynth();
				}
				else
					m_bSwSynth = FALSE;
			}
		}
				
	}

	bSelected = FALSE;

	// List all input ports

	//for(nPortCount = 1;nPortCount<=dwNumInPorts;nPortCount++)
	//{
	//	//CInPort.GetPortInfo(4,&Info);
	//	CInPort.GetPortInfo(nPortCount,&Info);
	//	if (Info.dwType != DMUS_PORT_KERNEL_MODE)
	//	{
	//		//m_InPortList.AddString(Info.szPortDescription);
	//		//if (!bSelected) // Select the port
	//		if (wcscmp(Info.szPortDescription,_T("EDIROL PCR-A 1 [Emulated]")) == 0)
	//		{	
	//			CInPort.ActivatePort(&Info);
	//			bSelected = TRUE;
	//			//m_nInPortSel = dwCountInPorts;
	//			//m_InPortList.SetCurSel(m_nInPortSel);
	//		}
	//		dwCountInPorts++;
	//	}
	//}
}	


void MusicPlayer::CloseDown()
{
	try
	{
		COutPort.SendMidiMsg(COutputPort::EncodeMidiMsg(0xB0,0,123,0),0);
		DLSLoader.UnloadCollection(Collection); // Unloads the collection
		for (UINT i=0; i<vpInstruments.size(); i++)
		{
			COutPort.UnloadInstrument(*vpInstruments[i]); // Unloads the instrument from the port
			delete vpInstruments[i];
		}
		vpInstruments.clear();
		CloseHandle(stopPlaybackEvent); // Close the event handler
		CloseHandle(unpausedPlaybackEvent); // Close the event handler
		//CInPort.BreakThru(0,0,0); // Breaks the thru connection
		//CInPort.TerminateNotification(); // Terminates the input port notification
	} catch(CDMusicException CDMusicEx)
	{
		OutputDebugString(CDMusicEx.GetErrorDescription());
	}
}



void MusicPlayer::PrepareSoftwareSynth()
{
	DWORD dwIndex = 0;
	INSTRUMENTINFO InstInfo;
	CString strInst;

	try
	{
		DLSLoader.LoadDLS(NULL,Collection);	// Loads the standard GM set
		//m_InstList.ResetContent();
	
		//while (Collection.EnumInstrument(dwIndex,&InstInfo) != S_FALSE)
		for (UINT dwIndex=0; dwIndex<128; dwIndex++)
		{
			Collection.EnumInstrument(dwIndex,&InstInfo);
			vpInstruments.push_back(new CInstrument());
			CInstrument& newInstrument = *vpInstruments.back();
			//Collection.EnumInstrument(instrNum,&InstInfo);
			Collection.GetInstrument(newInstrument,&InstInfo);
			try {
				newInstrument.SetPatch(dwIndex);
			}
			catch (CDMusicException CDMusicEx)
			{
				OutputDebugString(CDMusicEx.GetErrorDescription());
				if (CDMusicEx.m_hrCode == DMUS_E_INVALIDPATCH)
					break;
			}
			
			newInstrument.SetNoteRange(0,127);
			COutPort.DownloadInstrument(newInstrument);

		//	strInst.Format(_T("%d "),dwIndex);	// List the instruments		
		//	strInst+=InstInfo.szInstName;	// and adds them to the list 
		//	m_InstList.AddString((LPCTSTR)strInst);
			//dwIndex++;
			//if (dwIndex == 8)
			//	break;
		}
	
		//m_InstList.SetCurSel(0);
		//OnSelchangeInstruments();		    // Selects the first one

		//ChangeInstrument(0);
		
	} catch (CDMusicException CDMusicEx)
	{
		if (CDMusicEx.m_hrCode == E_OUTOFMEMORY)
			Alert(L"Insufficient memory to complete the task");
		OutputDebugString(CDMusicEx.GetErrorDescription());
	}
}	

void MusicPlayer::ChangeDLS(DLSFile* dlsfile)
{
	DWORD dwIndex = 0;
	INSTRUMENTINFO InstInfo;
	CString strInst;

	//unload the previous DLS file
	DLSLoader.UnloadCollection(Collection); // Unloads the collection
	for (UINT i=0; i<vpInstruments.size(); i++)
	{
		HRESULT hr = COutPort.UnloadInstrument(*vpInstruments[i]); // Unloads the instrument from the port
		delete vpInstruments[i];
	}
	vpInstruments.clear();

	//Load in the new guy
	vector<BYTE> dlsBuf;
	dlsBuf.reserve(dlsfile->GetSize());
	dlsfile->WriteDLSToBuffer(dlsBuf);
	
	HRESULT result = DLSLoader.LoadDLSFromMem(&dlsBuf[0], dlsfile->GetSize(), Collection);	// Loads the standard GM set
	//DLSLoader.LoadDLS(_T("TEST.dls"),Collection);	// Loads the standard GM set

	//while (Collection.EnumInstrument(dwIndex,&InstInfo) != S_FALSE)
	UINT nInstrs = dlsfile->aInstrs.size();
	for (UINT dwIndex=0; dwIndex < nInstrs /*&& dwIndex < 0x50*/; dwIndex++)
	{
		Collection.EnumInstrument(dwIndex,&InstInfo);
		vpInstruments.push_back(new CInstrument());
		CInstrument& newInstrument = *vpInstruments.back();
		//Collection.EnumInstrument(instrNum,&InstInfo);
		Collection.GetInstrument(newInstrument,&InstInfo);
		try {
			newInstrument.SetPatch(/*dwIndex*/InstInfo.dwPatchInCollection);
		}
		catch (CDMusicException CDMusicEx)
		{
			OutputDebugString(CDMusicEx.GetErrorDescription());
			if (CDMusicEx.m_hrCode == DMUS_E_INVALIDPATCH)
				break;
		}
		
		newInstrument.SetNoteRange(0,127);
		COutPort.DownloadInstrument(newInstrument);

		//dwIndex++;
		//if (dwIndex == 11)
		//	break;
	}
}


/*
bool MusicPlayer::ChangeInstrument(BYTE instrNum)
{
	//BYTE bInstrument = 0;//static_cast<BYTE>(m_InstList.GetCurSel());
	INSTRUMENTINFO InstInfo;
	try
	{
		if (m_bSwSynth) // We are working with a software port
		{
			Collection.EnumInstrument(instrNum,&InstInfo);
			Collection.GetInstrument(Instrument,&InstInfo);
			Instrument.SetPatch(0);
			Instrument.SetNoteRange(0,127);
			COutPort.DownloadInstrument(Instrument);
			instrNum = 0;
		} 
	
		// Sends the MIDI patch change message to the port
		COutPort.SendMidiMsg(COutputPort::EncodeMidiMsg(PATCH_CHANGE,0,instrNum,0),0);
	}
	catch (CDMusicException CDMusicEx)
	{
		switch(CDMusicEx.m_hrCode)
		{
		case DMUS_E_INVALIDPATCH:
			Alert("Invalid patch in collection");
			break;
		case S_FALSE:
			Alert("No instrument with that index");
			break;
		case E_OUTOFMEMORY:
			Alert("Insufficient memory to complete the task");
		}
		OutputDebugString(CDMusicEx.GetErrorDescription());
		return false;
	}
	return true;
}*/


//////////////////////////////////////////////////////////////////
// Create the playback synchronization event and initialize timer
//////////////////////////////////////////////////////////////////

void MusicPlayer::InitializeEventHandler()
{
	stopPlaybackEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	unpausedPlaybackEvent = CreateEvent(NULL, TRUE, TRUE, NULL);		//default to enabled, use manual reset
	SetTimer(NULL, 1,300,NULL);
}





////////////////////////////////////////////////////
// Overriden member function for receiving messages
////////////////////////////////////////////////////

void CMusicReceiver::RecvMidiMsg(REFERENCE_TIME lprt,DWORD dwChannel,DWORD dwMsg)
{
	/*BYTE Command,Channel,Note,Velocity;
	
	CSingleLock csl(&CS);
	csl.Lock(); // Protect the shared resource
	
	CInputPort::DecodeMidiMsg(dwMsg,&Command,&Channel,&Note,&Velocity);
	if ((Command == NOTE_ON) && (Velocity > 0)) //Channel #1 Note-On
    {              
		if (pMSDlg->State == RECORDING) // In case it is recording
		{		
			if (rec.GetIndex()<rec.GetTotalMsgMemory())	// If there is space
			{	
				rec.StoreMidiMsg(dwMsg);	// Store the message in the array
				
				pMSDlg->m_MemProgress.PostMessage(PBM_SETPOS,0,rec.GetIndex());	
			} else						// The maximum space is finished
				pMSDlg->StopRecord();	//stops the recording
		} 							
		pMSDlg->m_Piano.DrawPianoKeyFromNote(Note);		//Displays the note in the keyboard
		pMSDlg->m_MessageList.AddItemListCtrl(Note,Velocity,LIST_ITEM_YELLOW);		
	
	} else if ((Command == NOTE_OFF) || ((Command == NOTE_ON) && (Velocity == 0)))    //Channel #0 Note-Off
    {
			
		if (pMSDlg->State == RECORDING)	// In case it is recording
		{	
			if (rec.GetIndex() < rec.GetTotalMsgMemory())
			{	
				rec.StoreMidiMsg(dwMsg);	// Store the message in the array
				
				pMSDlg->m_MemProgress.PostMessage(PBM_SETPOS,0,rec.GetIndex());
			} else	// no more space
				pMSDlg->StopRecord();	//Stop recording
		} 
				
		pMSDlg->m_Piano.ReleasePianoKeyFromNote(Note);	// Displays the note-off in the keyboard
	 }

	csl.Unlock();*/
}

void MusicPlayer::Play(VGMItem* pItem, ULONG absTime)
{
	if (pItem->GetType() == ITEMTYPE_VGMFILE)						//if we were passed a VGMFile
	{
		if (((VGMFile*)pItem)->GetFileType() == FILETYPE_SEQ)		//and that VGMFile just so happens to be a VGMSeq
		{
			if (!bPaused)
			{
				Stop();
				VGMSeq* seq = (VGMSeq*)pItem;
				this->midi = seq->ConvertToMidi();
				if (this->midi == NULL)
					return;
				bPlaying = true;
				SetEvent(unpausedPlaybackEvent);
				SetupReverb(seq);
				playbackThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)&MusicPlayer::ProcessSeqPlayback, midi, 0, &playbackThrdID);
				SetThreadPriority(playbackThread, THREAD_PRIORITY_TIME_CRITICAL);//THREAD_PRIORITY_HIGHEST);
			}
			else
				Pause();
		}
	}

	/*if (pItem->GetType() == VGMItem::ITEMTYPE_SEQEVENT)						//if we were passed a VGMFile
	{
		if (((SeqEvent*)pItem)->GetEventType() == EVENTTYPE_NOTEON)		//and that VGMFile just so happens to be a VGMSeq
		{
			playbackThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)&MusicPlayer::ProcessNotePlayback, &((VGMSeq*)pItem)->midi, 0, &playbackThrdID);
			SetThreadPriority(playbackThread, THREAD_PRIORITY_TIME_CRITICAL);//THREAD_PRIORITY_HIGHEST);
			//ProcessSeqPlayback(&pSeq->midi.aTracks);
		}
	}*/
}

void MusicPlayer::Pause()
{
	if (!bPaused)
	{
		ResetEvent(unpausedPlaybackEvent);
		ReleaseAllKeys();
		//bPlaying = false;
		bPaused = true;
	}
	else
	{
		//bPlaying = true;
		bPaused = false;
		SetEvent(unpausedPlaybackEvent);
	}
}

void MusicPlayer::Stop()
{
	if (bPlaying)
	{
		SetEvent(unpausedPlaybackEvent);
		SetEvent(stopPlaybackEvent);
		WaitForSingleObject(playbackThread, INFINITE);
		bPlaying = false;
		bPaused = false;
	}
}

void MusicPlayer::ReleaseAllKeys()
{
	for (UINT i=0; i<16; i++)
	{
		try
		{
			COutPort.SendMidiMsg(COutputPort::EncodeMidiMsg(0xB0,i,123,0),0);
		} catch (CDMusicException CDMusicEx)
		{
			OutputDebugString(CDMusicEx.GetErrorDescription());
			pRoot->AddLogItem(new LogItem(CDMusicEx.GetErrorDescription(), LOG_LEVEL_ERR, L"MusicPlayer"));
		}
	}
}

void MusicPlayer::SetWriteLatency(DWORD dwLatency)
{
	HRESULT hr;
	ULONG returnValue;
	try {
		hr = COutPort.KsProperty(GUID_DMUS_PROP_WriteLatency, 0, KSPROPERTY_TYPE_SET,
			(LPVOID)&dwLatency, sizeof(dwLatency), &returnValue);
	}
	catch (CDMusicException CDMusicEx)
	{
		OutputDebugString(CDMusicEx.GetErrorDescription());
		pRoot->AddLogItem(new LogItem(CDMusicEx.GetErrorDescription(), LOG_LEVEL_ERR, L"MusicPlayer"));
	}
}

void MusicPlayer::SetWritePeriod(DWORD dwPeriod)
{
	HRESULT hr;
	ULONG returnValue;
	try {
		hr = COutPort.KsProperty(GUID_DMUS_PROP_WritePeriod, 0, KSPROPERTY_TYPE_SET,
			(LPVOID)&dwPeriod, sizeof(dwPeriod), &returnValue);
	}
	catch (CDMusicException CDMusicEx)
	{
		OutputDebugString(CDMusicEx.GetErrorDescription());
		pRoot->AddLogItem(new LogItem(CDMusicEx.GetErrorDescription(), LOG_LEVEL_ERR, L"MusicPlayer"));
	}
}

void MusicPlayer::SetupReverb(VGMSeq* vgmseq)
{
	if (vgmseq->bReverb)
	{
		LPVOID pPropertyData;
		HRESULT hr;
		ULONG returnValue;
		DMUS_WAVES_REVERB_PARAMS reverbParams;
		reverbParams.fHighFreqRTRatio = 0.001;
		reverbParams.fInGain = 0;
		reverbParams.fReverbTime = 3000;//4000;
		reverbParams.fReverbMix = -10;//-8;
		pPropertyData = &reverbParams;
		try {
			hr = COutPort.KsProperty(GUID_DMUS_PROP_WavesReverb, 0, KSPROPERTY_TYPE_SET/*KSPROPERTY_TYPE_BASICSUPPORT*/, pPropertyData, sizeof(reverbParams), &returnValue);
		}
		catch (CDMusicException CDMusicEx)
		{
			OutputDebugString(CDMusicEx.GetErrorDescription());
			pRoot->AddLogItem(new LogItem(CDMusicEx.GetErrorDescription(), LOG_LEVEL_ERR, L"MusicPlayer"));
		}
		COutPort.SetEffect(SET_REVERB /*| SET_CHORUS*/);	// Activate effects
	}
	else
		COutPort.SetEffect(0);
}

void MusicPlayer::SetVolume(long vol)
{
	LPVOID pPropertyData;
	HRESULT hr;
	ULONG returnValue;
	//DMUS_ITEM_Volume vol;
	//DMUS_VOLUME_MAX 
	pPropertyData = &vol;
	try {
		hr = COutPort.KsProperty(GUID_DMUS_PROP_Volume, 0, KSPROPERTY_TYPE_SET/*KSPROPERTY_TYPE_BASICSUPPORT*/, pPropertyData, sizeof(vol), &returnValue);
	}
	catch (CDMusicException CDMusicEx)
	{
		OutputDebugString(CDMusicEx.GetErrorDescription());
		pRoot->AddLogItem(new LogItem(CDMusicEx.GetErrorDescription(), LOG_LEVEL_ERR, L"MusicPlayer"));
	}
}

void MusicPlayer::ResetMidi()
{
	for (int grp=0; grp<NUM_CHANNEL_GROUPS; grp++)
	{
		for (int chan=0; chan < 16; chan++)
		{
			//my understanding is that control 121 should reset EVERYTHING.  But not so with MS's DLS synth.
			COutPort.SendMidiMsg(COutputPort::EncodeMidiMsg(0xB0+chan, 121, 0), grp+1);
			COutPort.SendMidiMsg(COutputPort::EncodeMidiMsg(0xB0+chan, 0, 0), grp+1);			//Reset Bank Select
			COutPort.SendMidiMsg(COutputPort::EncodeMidiMsg(0xB0+chan, 32, 0), grp+1);			//Reset Bank Select Fine
			COutPort.SendMidiMsg(COutputPort::EncodeMidiMsg(0xB0+chan, 10, 64), grp+1);			//reset pan
			COutPort.SendMidiMsg(COutputPort::EncodeMidiMsg(0xB0+chan, 7, 100), grp+1);			//reset volume
			COutPort.SendMidiMsg(COutputPort::EncodeMidiMsg(0xB0+chan, 11, 127), grp+1);		//reset expression
			COutPort.SendMidiMsg(COutputPort::EncodeMidiMsg(0xE0+chan, 0, 0x40), grp+1);		//reset pitchbend -> 0x2000
			COutPort.SendMidiMsg(COutputPort::EncodeMidiMsg(0xB0+chan, 101, 0), grp+1);			//Reset RPN Pitch Bend Range Coarse
			COutPort.SendMidiMsg(COutputPort::EncodeMidiMsg(0xB0+chan, 100, 0), grp+1);			//Reset RPN Pitch Bend Range Fine
			COutPort.SendMidiMsg(COutputPort::EncodeMidiMsg(0xB0+chan, 6, 2), grp+1);//12), grp+1);		// +/- 2 semitones
			COutPort.SendMidiMsg(COutputPort::EncodeMidiMsg(0xB0+chan, 38, 0), grp+1);					// +/- 0 cents

		}
	}
}

volatile bool testy = false;
UINT theID = 1;

//CALLBACK *LPDXUTCALLBACKTIMER)( UINT idEvent, void* pUserContext );
void MusicPlayer::CallbackTestFunc(UINT idEvent, void* pUserContext)
{
	testy = true;

}

DWORD MusicPlayer::ProcessSeqPlayback(PVOID pParam)
{
	MidiFile* pMidi = reinterpret_cast<MidiFile*>(pParam);
	vector<MidiEvent*> pvEvent;
	UINT reserveSize = 0;
	for (UINT i=0; i<pMidi->aTracks.size(); i++)
		reserveSize += pMidi->aTracks[i]->aEvents.size();
	pvEvent.reserve(reserveSize);

	for (UINT i=0; i<pMidi->aTracks.size(); i++)
		pvEvent.insert(pvEvent.end(), pMidi->aTracks[i]->aEvents.begin(), pMidi->aTracks[i]->aEvents.end());

	//Add global events
	pMidi->globalTranspose = 0;
	pvEvent.insert(pvEvent.end(), pMidi->globalTrack.aEvents.begin(), pMidi->globalTrack.aEvents.end());


	if (pvEvent.size() == 0)
	{
		delete pMidi;
		musicplayer.bPlaying = false;
		musicplayer.bPaused = false;
		musicplayer.ReleaseAllKeys();
		pMainFrame->UIEnable(ID_STOP, 0);
		pMainFrame->UIEnable(ID_PAUSE, 0);
		return true;
	}
	stable_sort(pvEvent.begin(), pvEvent.end(), PriorityCmp());	//Sort all the events by priority
	stable_sort(pvEvent.begin(), pvEvent.end(), AbsTimeCmp());	//Sort all the events by absolute time, so that delta times can be recorded correctly

	UINT ppqn = pMidi->GetPPQN();
	ULONG microsPerQuarter = 500000;		//default to 120 bpm (0.5 seconds per beat)
	double millisPerTick = ((double)microsPerQuarter/(double)ppqn) / (double)1000;


	double remainderAdjust = 0;
	double prevWaitTime = 0;

	musicplayer.ResetMidi();

	vector<MidiTrack*>* pvTrack = &pMidi->aTracks;

	UINT prevTime = pvEvent[0]->AbsTime;
	UINT time = pvEvent[0]->AbsTime;
	vector<BYTE> msg;
	for (UINT i=0; i<pvEvent.size() && musicplayer.bPlaying; i++)		//for every event in the MIDI
	{
		BYTE d = 0;
		msg.clear();
		MidiEvent* pEvent = pvEvent[i];
		int channelGroup = pEvent->prntTrk->channelGroup;
		
		time = pEvent->WriteEvent(msg, time);
		if (time-prevTime > 0)
		{
			double waitTime = (double)millisPerTick * (double)(time-prevTime) + remainderAdjust;// - executionAdjust;
			remainderAdjust = waitTime - (DWORD)waitTime;

			WaitForSingleObject(musicplayer.unpausedPlaybackEvent, INFINITE);
			if (WaitForSingleObject(musicplayer.stopPlaybackEvent, (DWORD)waitTime) == WAIT_OBJECT_0)	//If the stopplaybackEvent was signalled
				break;
			//DXUTSetTimer(CallbackTestFunc, waitTime/1000.0, &theID);
			//while (testy != true)
			//	Sleep(0);
			//testy = false;
			//DXUTKillTimer(theID);
			
			
		}
		prevTime = time;
		if (msg.empty())
			continue;
		while (msg[0+d] & 0x80)
			d++;
		if (msg.size() < d+4)
			msg.resize(d+4);
		if (msg[1+d] == 0xFF && msg[2+d] == 0x51 && msg[3+d] == 0x03)			//if it's a tempo event
		{
			microsPerQuarter = (msg[4+d]<<16) + (msg[5+d]<<8) + msg[6+d];		//get the microseconds per quarter value
			millisPerTick = ((double)microsPerQuarter/(double)ppqn) / (double)1000;
		}
		else if (msg[1+d] != 0xFF /*&& (msg[1]&0xF0) != 0xB0 /*&& (msg[1]&0xF0) != 0xC0*/)
		{
			BYTE channel = msg[1+d] & 0x0F;
			if (channel == 9)					//if it's trying to play on the drum track channel
			{									//directmusic behaves oddly.  It expects the DLS to load a drum bank
				msg[1+d] |= 0x0F;				//and will not play on this channel otherwise.  So change the channel to one that should be unique
				channelGroup++;					//(we wouldn't be playing on the drum chan in the first place if the seq format supported > 16 tracks, most likely)
			}
			try
			{
				musicplayer.COutPort.SendMidiMsg(COutputPort::EncodeMidiMsg(msg[1+d],msg[2+d],msg[3+d]),channelGroup+1);
			}
			catch(CDMusicException CDMusicEx)
			{
				OutputDebugString(CDMusicEx.GetErrorDescription());
				pRoot->AddLogItem(new LogItem(CDMusicEx.GetErrorDescription(), LOG_LEVEL_ERR, L"MusicPlayer"));
			}
		}
	}
	delete pMidi;
	musicplayer.bPlaying = false;
	musicplayer.bPaused = false;
	musicplayer.ReleaseAllKeys();
	pMainFrame->UIEnable(ID_STOP, 0);
	pMainFrame->UIEnable(ID_PAUSE, 0);
	return true;
}

