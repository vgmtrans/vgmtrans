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
  bAllowDiscontinuousTrackData(false),
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

bool VGMSeq::Load()
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
	return true;
}

MidiFile* VGMSeq::ConvertToMidi()
{
	this->readMode = READMODE_FIND_DELTA_LENGTH;
	int numTracks = aTracks.size();
	long stopDelta = -1;

	ResetVars();
	for (int i = 0; i < numTracks; i++)
	{
		aTracks[i]->readMode = READMODE_FIND_DELTA_LENGTH;
		aTracks[i]->deltaLength = -1;
		assert(aTracks[i]->unLength != 0);
		if (!aTracks[i]->LoadTrack(i, aTracks[i]->dwOffset + aTracks[i]->unLength))
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
	ResetVars();
	for (int i = 0; i < numTracks; i++)
	{
		aTracks[i]->readMode = READMODE_CONVERT_TO_MIDI;
		assert(aTracks[i]->unLength != 0);
		if (!aTracks[i]->LoadTrack(i, aTracks[i]->dwOffset + aTracks[i]->unLength, stopDelta))
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
bool VGMSeq::LoadMain()
{
	if (!GetHeaderInfo())
		return false;
	if (!GetTrackPointers())
		return false;
	nNumTracks = aTracks.size();
	if (nNumTracks == 0)
		return false;

	ResetVars();
	for (UINT i = 0; i < nNumTracks; i++)
	{
		U32 stopOffset = 0xFFFFFFFF;
		if (unLength != 0)
		{
			stopOffset = dwOffset + unLength;
		}
		else
		{
			if (!bAllowDiscontinuousTrackData)
			{
				// set length from the next track by offset
				for (UINT j = 0; j < nNumTracks; j++)
				{
					if (aTracks[j]->dwOffset > aTracks[i]->dwOffset &&
						aTracks[j]->dwOffset < stopOffset)
					{
						stopOffset = aTracks[j]->dwOffset;
					}
				}
			}
		}

		aTracks[i]->readMode = READMODE_ADD_TO_UI;
		if (!aTracks[i]->LoadTrack(i, stopOffset))
			return false;
	}

	/*if (!LoadTracks())
		return false;*/

	if (unLength == 0) {
		// a track can sometimes cover other ones (i.e. a track has a "hole" between the head and tail)
		// it means that the tail of the last track is not always the tail of a sequence
		// therefore, check the length of each tracks

		for (vector<SeqTrack*>::iterator itr = aTracks.begin(); itr != aTracks.end(); ++itr) {
			assert(dwOffset <= (*itr)->dwOffset);

			ULONG expectedLength = (*itr)->dwOffset + (*itr)->unLength - dwOffset;
			if (unLength < expectedLength)
			{
				unLength = expectedLength;
			}
		}
	}

	// do not sort tracks until the loading has been finished,
	// some engines are sensitive about the track order.
	// TODO: aTracks must not be sorted because the order of tracks must be kept until MIDI conversion.
	//sort(aTracks.begin(), aTracks.end(), ItemPtrOffsetCmp());

	return true;
}

bool VGMSeq::PostLoad()
{
	if (readMode == READMODE_ADD_TO_UI)
	{
		std::sort(aInstrumentsUsed.begin(), aInstrumentsUsed.end());
	}
	else if (readMode == READMODE_CONVERT_TO_MIDI)
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

bool VGMSeq::GetHeaderInfo(void)
{
	return true;
}


//GetTrackPointers() should contain logic for parsing track pointers 
// and instantiating/adding each track in the sequence
bool VGMSeq::GetTrackPointers(void)
{
	return true;
}

void VGMSeq::ResetVars(void)
{
	if (readMode == READMODE_ADD_TO_UI)
	{
		aInstrumentsUsed.clear();
	}
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

void VGMSeq::AddInstrumentRef(ULONG progNum)
{
	if (std::find(aInstrumentsUsed.begin(), aInstrumentsUsed.end(), progNum) == aInstrumentsUsed.end())
	{
		aInstrumentsUsed.push_back(progNum);
	}
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
	bool result = midi->SaveMidiFile(filepath);
	delete midi;
	return result;
}
