#include "stdafx.h"
#include "VGMSeqNoTrks.h"
#include "SeqEvent.h"
#include "Root.h"

VGMSeqNoTrks::VGMSeqNoTrks(const string& format, RawFile* file, ULONG offset)
: VGMSeq(format, file, offset),
  SeqTrack(this)
{
	ResetVars();
	AddContainer<SeqEvent>(aEvents);
}

VGMSeqNoTrks::~VGMSeqNoTrks(void)
{
}

void VGMSeqNoTrks::ResetVars()
{
	midiTracks.clear();		//no need to delete the contents, that happens when the midi is deleted
	VGMSeq::ResetVars();
	SeqTrack::ResetVars();
}

//LoadMain() - loads all sequence data into the class
bool VGMSeqNoTrks::LoadMain()
{
	this->SeqTrack::readMode = this->VGMSeq::readMode = READMODE_ADD_TO_UI;
	if (!GetHeaderInfo())
		return false;

	//if (name == "")
	//	name.Format(_T("Sequence %d"), GetCount());

	if (!LoadEvents())
		return false;
	if (length() == 0)
		length() = (aEvents.back()->dwOffset+aEvents.back()->unLength) - offset();			//length == to the end of the last event
	
	return true;
}

bool VGMSeqNoTrks::LoadEvents(void)
{
	ResetVars();
	TryExpandMidiTracks(nNumTracks);
	SetCurTrack(0);
	if (bWriteInitialTempo)
		AddTempoBPMNoItem(tempoBPM);
	if (bAlwaysWriteInitialVol)
		for (int i=0; i<16; i++) { channel = i; AddVolNoItem(initialVol); }
	if (bAlwaysWriteInitialExpression)
		for (int i=0; i<16; i++) { channel = i; AddExpressionNoItem(initialExpression); }
	if (bAlwaysWriteInitialPitchBendRange)
		for (int i=0; i<16; i++) 
		{
			channel = i;
			AddPitchBendRangeNoItem(initialPitchBendRangeSemiTones, initialPitchBendRangeCents);
		}
	channel = 0;


	bInLoop = false;
	curOffset = eventsOffset();	//start at beginning of track
	while (curOffset < rawfile->size())
	{
		if (!ReadEvent())
		{
			break;
		}
	}
	if (VGMSeq::unLength == 0)
		VGMSeq::unLength = curOffset - VGMSeq::dwOffset;
	return true;
}


MidiFile* VGMSeqNoTrks::ConvertToMidi()
{
	this->SeqTrack::readMode = this->VGMSeq::readMode = READMODE_FIND_DELTA_LENGTH;
	long stopDelta = -1;

	if (!LoadEvents())
		return false;
	if (!PostLoad())
		return NULL;

	// Find the greatest delta length of all tracks to use as stop point for every track
	//for (int i = 0; i < numTracks; i++)
	//	stopDelta = max(stopDelta, aTracks[i]->deltaLength);

	MidiFile* newmidi = new MidiFile(this);
	this->midi = newmidi;
	this->SeqTrack::readMode = this->VGMSeq::readMode = READMODE_CONVERT_TO_MIDI;
	if (!LoadEvents())
	{
		delete midi;
		this->midi = NULL;
		return NULL;
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

//bool VGMSeqNoTrks::SaveAsMidi(const wchar_t* filepath)
//{
//	return midi->SaveMidiFile(filepath);
//}


// checks whether or not we have already created the given number of MidiTracks.  If not, it appends the extra tracks.
// doesn't ever need to be called directly by format code, since SetCurMidiTrack does so automatically.
void VGMSeqNoTrks::TryExpandMidiTracks(ULONG numTracks)
{
	if (VGMSeq::readMode != READMODE_CONVERT_TO_MIDI)
		return;
	if (midiTracks.size() < numTracks)
	{
		int initialTrackSize = midiTracks.size();
		for (UINT i=0; i<numTracks-initialTrackSize; i++)
			midiTracks.push_back( midi->AddTrack());
	}
}

void VGMSeqNoTrks::SetCurTrack(ULONG trackNum)
{
	if (VGMSeq::readMode != READMODE_CONVERT_TO_MIDI)
		return;

	TryExpandMidiTracks(trackNum+1);
	pMidiTrack = midiTracks[trackNum];
}


void VGMSeqNoTrks::AddTime(ULONG delta)
{
	time += delta;
	if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI)
	{
		for (UINT i=0; i<midiTracks.size(); i++)
			midiTracks[i]->AddDelta(delta);
	}
}


//bool VGMSeqNoTrks::AddEndOfTrack(ULONG offset, ULONG length, const wchar_t* sEventName)
//{
//	if (bInLoop == false)
//		AddEvent(new TrackEndSeqEvent(this, offset, length, sEventName));
//	return false;
//}
