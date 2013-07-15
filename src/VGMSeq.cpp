#include "stdafx.h"
#include "common.h"
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "SeqEvent.h"
#include "Root.h"
#include <math.h>


DECLARE_MENU(VGMSeq)

VGMSeq::VGMSeq(const string& format, RawFile* file, ULONG offset, ULONG length, wstring name)
: VGMFile(FILETYPE_SEQ, format, file, offset, length, name),
  //midi(this),
  midi(NULL),
  bMonophonicTracks(false),
  bReverb(false),
  bUseLinearAmplitudeScale(false),
  bWriteInitialTempo(false),
  bAlwaysWriteInitialVol(false),
  bAlwaysWriteInitialExpression(false),
  bAlwaysWriteInitialPitchBendRange(false),
  initialVol(100),					//GM standard (dls1 spec p16)
  initialExpression(127),			//''
  initialPitchBendRangeSemiTones(2), //GM standard.  Means +/- 2 semitones (4 total range)
  initialPitchBendRangeCents(0)
{
	AddContainer<SeqTrack>(aTracks);
}

VGMSeq::~VGMSeq(void)
{
	DeleteVect<SeqTrack>(aTracks);
}

int VGMSeq::Load()
{
	this->readMode = READMODE_ADD_TO_UI;
	if (!LoadMain())
		return false;
	if (!PostLoad())
		return false;

	for (UINT i=0; i<aTracks.size(); i++)
		sort(aTracks[i]->aEvents.begin(), aTracks[i]->aEvents.end(), ItemPtrOffsetCmp());

	//LoadLocalData();
	//UseLocalData();
	pRoot->AddVGMFile(this);
}

MidiFile* VGMSeq::ConvertToMidi()
{
	this->readMode = READMODE_FIND_DELTA_LENGTH;
	int numTracks = aTracks.size();
	long stopDelta = -1;

	for (int i = 0; i < numTracks; i++)
	{
		aTracks[i]->readMode = READMODE_FIND_DELTA_LENGTH;
		aTracks[i]->deltaLength = -1;
		if (!aTracks[i]->LoadTrack(i, (i != aTracks.size()-1) ? aTracks[i+1]->dwOffset :  ((unLength) ? dwOffset+unLength : 0xFFFFFFFF)))
			return NULL;
	}
	if (!PostLoad())
		return NULL;

	// Find the greatest delta length of all tracks to use as stop point for every track
	for (int i = 0; i < numTracks; i++)
		stopDelta = max(stopDelta, aTracks[i]->deltaLength);

	MidiFile* newmidi = new MidiFile(this);
	this->midi = newmidi;
	this->readMode = READMODE_CONVERT_TO_MIDI;
	for (int i = 0; i < numTracks; i++)
	{
		aTracks[i]->readMode = READMODE_CONVERT_TO_MIDI;
		if (!aTracks[i]->LoadTrack(i, (i != aTracks.size()-1) ? aTracks[i+1]->dwOffset :  ((unLength) ? dwOffset+unLength : 0xFFFFFFFF), stopDelta))
		{
			delete midi;
			this->midi = NULL;
			return NULL;
		}
	}
	if (!PostLoad())
	{
		delete midi;
		this->midi = NULL;
		return NULL;
	}
	this->midi = NULL;
	return newmidi;
}

//Load() - Function to load all the sequence data into the class
int VGMSeq::LoadMain()
{
	if (!GetHeaderInfo())
		return false;
	if (!GetTrackPointers())
		return false;
	nNumTracks = aTracks.size();
	if (nNumTracks == 0)
		return false;

	sort(aTracks.begin(), aTracks.end(), ItemPtrOffsetCmp());
	for (UINT i=0; i<nNumTracks; i++)
	{
		aTracks[i]->readMode = READMODE_ADD_TO_UI;
		if (!aTracks[i]->LoadTrack(i, (i != aTracks.size()-1) ? aTracks[i+1]->dwOffset :  ((unLength) ? dwOffset+unLength : 0xFFFFFFFF)))
			return false;
	}
	/*if (!LoadTracks())
		return false;*/
	if (unLength == 0) {		//length will extend to the end of the last track (last by offset)
		SeqTrack* lastTrk = *((vector<SeqTrack*>::const_iterator)max_element(aTracks.begin(), aTracks.end()));
		unLength = lastTrk->dwOffset + lastTrk->unLength - dwOffset;
	}
	return true;
}

int VGMSeq::PostLoad()
{
	if (readMode == READMODE_CONVERT_TO_MIDI)
	{
		midi->Sort();
	}
	
	return true;
}

//int VGMSeq::LoadTracks(void)
//{
//	for (UINT i=0; i<nNumTracks; i++)
//	{
//		if (!aTracks[i]->LoadTrack(i, (i != aTracks.size()-1) ? aTracks[i+1]->dwOffset :  ((unLength) ? dwOffset+unLength : 0xFFFFFFFF)))
//			return false;
//	}
//}

int VGMSeq::GetHeaderInfo(void)
{
	return true;
}


//GetTrackPointers() should contain logic for parsing track pointers 
// and instantiating/adding each track in the sequence
int VGMSeq::GetTrackPointers(void)
{
	return true;
}

void VGMSeq::SetPPQN(WORD ppqn)
{
	this->ppqn = ppqn;
	if (readMode == READMODE_CONVERT_TO_MIDI)
		midi->SetPPQN(ppqn);
}

WORD VGMSeq::GetPPQN(void)
{
	return this->ppqn;
	//return midi->GetPPQN();
}

bool VGMSeq::OnSaveAsMidi(void)
{
	wstring filepath = pRoot->UI_GetSaveFilePath(name.c_str(), L"mid");
	if (filepath.length() != 0)
		return SaveAsMidi(filepath.c_str());
	return false;
}


bool VGMSeq::SaveAsMidi(const wchar_t* filepath)
{
	MidiFile* midi = this->ConvertToMidi();
	if (!midi)
		return false;
	int result = midi->SaveMidiFile(filepath);
	delete midi;
	return result;
}
