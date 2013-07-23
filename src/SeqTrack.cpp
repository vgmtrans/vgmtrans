#include "stdafx.h"
#include "SeqTrack.h"
#include "SeqEvent.h"
#include "VGMSeq.h"
#include "MidiFile.h"
#include "ScaleConversion.h"
#include "Options.h"

#include <iostream>
#include <iomanip>
#include <string>
#include <sstream>

//  ********
//  SeqTrack
//  ********


SeqTrack::SeqTrack(VGMSeq* parentFile, ULONG offset, ULONG length)
: VGMContainerItem(parentFile, offset, length),
  parentSeq(parentFile)
{
	dwStartOffset = offset;
	bMonophonic = parentSeq->bMonophonicTracks;
	pMidiTrack = NULL;
	ResetVars();
	bDetermineTrackLengthEventByEvent = false;
	bWriteGenericEventAsTextEvent = false;

	swprintf(numberedName, sizeof(numberedName)/sizeof(numberedName[0]), L"Track %d", parentSeq->aTracks.size()+1);
	name = numberedName;
	AddContainer<SeqEvent>(aEvents);
}

SeqTrack::~SeqTrack()
{
	DeleteVect<SeqEvent>(aEvents);
}

void SeqTrack::ResetVars()
{
	bInLoop = false;
	foreverLoops = 0;
	deltaLength = -1;
	deltaTime = 0;	
	vol = 100;
	expression = 127;
	prevPan = 64;
	prevReverb = 40;
	channelGroup = 0;
	transpose = 0;
	cDrumNote = -1;
	cKeyCorrection = 0;
}

/*void SeqTrack::AddToUI(VGMItem* parent, VGMFile* theVGMFile)
{
	pRoot->UI_AddItem(vgmfile, this, parent, name);
	for (UINT i=0; i<aEvents.size(); i++)
		aEvents[i]->AddToUI(this);
}*/


bool SeqTrack::ReadEvent(void)
{
	return false;		//by default, don't add any events, just stop immediately.
}

bool SeqTrack::LoadTrack( int trackNum, ULONG stopOffset /*= 0xFFFFFFFF*/, long stopDelta /*= -1*/ )
{
	if (!LoadTrackInit(trackNum))
		return false;
	if (!LoadTrackMainLoop(stopOffset, stopDelta))
		return false;
	return true;
}

bool SeqTrack::LoadTrackInit(int trackNum)
{
	ResetVars();
	if (readMode == READMODE_CONVERT_TO_MIDI)
		pMidiTrack = parentSeq->midi->AddTrack();
	SetChannelAndGroupFromTrkNum(trackNum);

	if (readMode == READMODE_CONVERT_TO_MIDI)
	{
		if (trackNum == 0)
			pMidiTrack->AddSeqName(parentSeq->GetName()->c_str());
		wostringstream ssTrackName;
		ssTrackName << L"Track: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << dwStartOffset << std::endl;
		pMidiTrack->AddTrackName(ssTrackName.str().c_str());

		if (trackNum == 0) {
			pMidiTrack->AddGMReset();
			if (parentSeq->bWriteInitialTempo)
				pMidiTrack->AddTempoBPM(parentSeq->tempoBPM);
		}
		if (parentSeq->bAlwaysWriteInitialVol)
			AddVolNoItem(parentSeq->initialVol);
		if (parentSeq->bAlwaysWriteInitialExpression)
			AddExpressionNoItem(parentSeq->initialExpression);
		if (parentSeq->bAlwaysWriteInitialPitchBendRange)
			AddPitchBendRangeNoItem(parentSeq->initialPitchBendRangeSemiTones, parentSeq->initialPitchBendRangeCents);
	}
	return true;
}

bool SeqTrack::LoadTrackMainLoop(ULONG stopOffset, long stopDelta)
{
	bInLoop = false;
	curOffset = dwStartOffset;	//start at beginning of track
	if (stopDelta == -1)
		stopDelta = 0x7FFFFFFF;
	while ((curOffset < stopOffset) && GetDelta() < (ULONG)stopDelta &&  ReadEvent())
		;
	if (unLength == 0)			//if unLength has not been changed from default value of 0
	{
		//unLength = curOffset-dwOffset;
		//SeqEvent* lastTrkEvent = *((vector<SeqEvent*>::const_iterator)max_element(aEvents.begin(), aEvents.end()));
		SeqEvent* lastTrkEvent = aEvents.back();
		unLength = lastTrkEvent->dwOffset + lastTrkEvent->unLength - dwOffset;
	}
		

	return true;
}

void SeqTrack::SetChannelAndGroupFromTrkNum(int theTrackNum)
{
	if (theTrackNum > 39)
		theTrackNum += 3;
	else if (theTrackNum > 23)
		theTrackNum += 2;					//compensate for channel 10 - drum track.  we'll skip it, by default
	else if (theTrackNum > 8)
		theTrackNum++;						//''
	channel = theTrackNum%16;
	channelGroup = theTrackNum/16;
	if (readMode == READMODE_CONVERT_TO_MIDI)
		pMidiTrack->SetChannelGroup(channelGroup);
}

ULONG SeqTrack::GetDelta()
{
	return deltaTime;
	//return pMidiTrack->GetDelta();
}

void SeqTrack::SetDelta(ULONG NewDelta)
{
	deltaTime = NewDelta;
	if (readMode == READMODE_CONVERT_TO_MIDI)
		pMidiTrack->SetDelta(NewDelta);
}

void SeqTrack::AddDelta(ULONG AddDelta)
{
	deltaTime += AddDelta;
	if (readMode == READMODE_CONVERT_TO_MIDI)
		pMidiTrack->AddDelta(AddDelta);
}

void SeqTrack::SubtractDelta(ULONG SubtractDelta)
{
	deltaTime -= SubtractDelta;
	if (readMode == READMODE_CONVERT_TO_MIDI)
		pMidiTrack->SubtractDelta(SubtractDelta);
}

void SeqTrack::ResetDelta(void)
{
	deltaTime = 0;
	if (readMode == READMODE_CONVERT_TO_MIDI)
		pMidiTrack->ResetDelta();
}

ULONG SeqTrack::ReadVarLen(ULONG& offset)
{
    register ULONG value;
    register UCHAR c;

    if ( (value = GetByte(offset++)) & 0x80 )
    {
       value &= 0x7F;
       do
       {
         value = (value << 7) + ((c = GetByte(offset++)) & 0x7F);
       } while (c & 0x80);
    }

    return value;
}

void SeqTrack::AddControllerSlide(ULONG offset, ULONG length, ULONG dur, BYTE& prevVal, BYTE targVal, 
								  void (MidiTrack::*insertFunc)(BYTE, BYTE, ULONG))
{
	if (readMode != READMODE_CONVERT_TO_MIDI)
		return;

	double valInc = (double)((double)(targVal-prevVal)/(double)dur);
	char newVal = -1;
	for (unsigned int i=0; i<dur; i++)
	{
		char prevValInSlide = newVal;
		newVal=round(prevVal+(valInc*(i+1)));
		//only create an event if the pan value has changed since the last iteration
		if (prevValInSlide != newVal)
		{
			if (newVal < 0)
				newVal = 0;
			if (newVal > 0x7F)
				newVal = 0x7F;
			(pMidiTrack->*insertFunc)(channel, newVal, GetDelta()+i);
		}
	}
	prevVal = targVal;
}


ULONG SeqTrack::offsetInQuestion;

struct SeqTrack::IsEventAtOffset : unary_function< SeqEvent, bool >
{
	inline bool operator()( const SeqEvent* theEvent ) const
	{
		return (theEvent->dwOffset == offsetInQuestion);
	}
};


bool SeqTrack::IsOffsetUsed(ULONG offset)
{
	offsetInQuestion = offset;
	vector<SeqEvent*>::iterator iter = find_if(aEvents.begin(), aEvents.end(), IsEventAtOffset());
	return (iter != aEvents.end());
}


void SeqTrack::AddEvent(SeqEvent* pSeqEvent)
{
	if (readMode != READMODE_ADD_TO_UI)
		return;

	//if (!bInLoop)
		aEvents.push_back(pSeqEvent);
	//else
	//	delete pSeqEvent;

	// care for a case where the new event is located before the start address
	// (example: Donkey Kong Country - Map, Track 7 of 8)
	if (dwOffset > pSeqEvent->dwOffset)
	{
		unLength += (dwOffset - pSeqEvent->dwOffset);
		dwOffset = pSeqEvent->dwOffset;
	}

	if (bDetermineTrackLengthEventByEvent)
	{
		ULONG length = pSeqEvent->dwOffset + pSeqEvent->unLength - dwOffset;
		if (unLength < length)
			unLength = length;
	}
}

void SeqTrack::AddGenericEvent(ULONG offset, ULONG length, const wchar_t* sEventName, const wchar_t* sEventDesc, BYTE color, Icon icon)
{
	if (readMode == READMODE_ADD_TO_UI && !IsOffsetUsed(offset))
	{
		AddEvent(new SeqEvent(this, offset, length, sEventName, color, icon, sEventDesc));
	}
	else if (readMode == READMODE_CONVERT_TO_MIDI)
	{
		if (bWriteGenericEventAsTextEvent)
		{
			wstring miditext(sEventName);
			if (sEventDesc != NULL && sEventDesc[0] != L'\0')
			{
				miditext += L" - ";
				miditext += sEventDesc;
			}
			pMidiTrack->AddText(miditext.c_str());
		}
	}
}


void SeqTrack::AddUnknown(ULONG offset, ULONG length, const wchar_t* sEventName, const wchar_t* sEventDesc)
{
	if (readMode == READMODE_ADD_TO_UI && !IsOffsetUsed(offset))
	{
		AddEvent(new SeqEvent(this, offset, length, sEventName, CLR_UNKNOWN, ICON_BINARY, sEventDesc));
	}
	else if (readMode == READMODE_CONVERT_TO_MIDI)
	{
		if (bWriteGenericEventAsTextEvent)
		{
			wstring miditext(sEventName);
			if (sEventDesc != NULL && sEventDesc[0] != L'\0')
			{
				miditext += L" - ";
				miditext += sEventDesc;
			}
			pMidiTrack->AddText(miditext.c_str());
		}
	}
}

void SeqTrack::AddSetOctave(ULONG offset, ULONG length, BYTE newOctave,  const wchar_t* sEventName)
{
	octave = newOctave; 
	if (readMode == READMODE_ADD_TO_UI && !IsOffsetUsed(offset))
		AddEvent(new SetOctaveSeqEvent(this, newOctave, offset, length, sEventName));
}

void SeqTrack::AddIncrementOctave(ULONG offset, ULONG length, const wchar_t* sEventName)
{
	octave++;
	if (readMode == READMODE_ADD_TO_UI && !IsOffsetUsed(offset))
		AddEvent(new SeqEvent(this, offset, length, sEventName, CLR_CHANGESTATE));
}

void SeqTrack::AddDecrementOctave(ULONG offset, ULONG length, const wchar_t* sEventName)
{
	octave--;
	if (readMode == READMODE_ADD_TO_UI && !IsOffsetUsed(offset))
		AddEvent(new SeqEvent(this, offset, length, sEventName, CLR_CHANGESTATE));
}

void SeqTrack::AddRest(ULONG offset, ULONG length, UINT restTime,  const wchar_t* sEventName)
{
	
	if (readMode == READMODE_ADD_TO_UI && !IsOffsetUsed(offset))
		AddEvent(new RestSeqEvent(this, restTime, offset, length, sEventName));
	AddDelta(restTime);
}

void SeqTrack::AddHold(ULONG offset, ULONG length, const wchar_t* sEventName)
{
	if (readMode == READMODE_ADD_TO_UI && !IsOffsetUsed(offset))
		AddEvent(new SeqEvent(this, offset, length, sEventName, CLR_TIE));
}

void SeqTrack::AddNoteOn(ULONG offset, ULONG length, char key, char vel, const wchar_t* sEventName)
{
	if (readMode == READMODE_ADD_TO_UI && !IsOffsetUsed(offset))
		AddEvent(new NoteOnSeqEvent(this, key, vel, offset, length, sEventName));
	AddNoteOnNoItem(key, vel);
}

void SeqTrack::AddNoteOnNoItem(char key, char vel)
{
	if (readMode == READMODE_CONVERT_TO_MIDI)
	{
		BYTE finalVel = vel;
		if (parentSeq->bUseLinearAmplitudeScale)
			finalVel = Convert7bitPercentVolValToStdMidiVal(vel);

		if (cDrumNote == -1)
		{
			pMidiTrack->AddNoteOn(channel, key+cKeyCorrection+transpose, finalVel);
		}
		else
			AddPercNoteOnNoItem(cDrumNote, finalVel);
	}
	prevKey = key;
	prevVel = vel;
	return;
}


void SeqTrack::AddPercNoteOn(ULONG offset, ULONG length, char key, char vel, const wchar_t* sEventName)
{
	BYTE origChan = channel;
	channel = 9;
	char origDrumNote = cDrumNote;
	cDrumNote = -1;
//	DrumAssoc* pDrumAssoc = parentSeq->GetDrumAssoc(key);
//	if (pDrumAssoc)
//		key = pDrumAssoc->GMDrumNote;
	AddNoteOn(offset, length, key-transpose, vel, sEventName);
	cDrumNote = origDrumNote;
	channel = origChan;
}

void SeqTrack::AddPercNoteOnNoItem(char key, char vel)
{
	BYTE origChan = channel;
	channel = 9;
	char origDrumNote = cDrumNote;
	cDrumNote = -1;
//	DrumAssoc* pDrumAssoc = parentSeq->GetDrumAssoc(key);
//	if (pDrumAssoc)
//		key = pDrumAssoc->GMDrumNote;
	AddNoteOnNoItem(key-transpose, vel);
	cDrumNote = origDrumNote;
	channel = origChan;
}

void SeqTrack::InsertNoteOn(ULONG offset, ULONG length, char key, char vel, ULONG absTime, const wchar_t* sEventName)
{
	BYTE finalVel = vel;
	if (parentSeq->bUseLinearAmplitudeScale)
		finalVel = Convert7bitPercentVolValToStdMidiVal(vel);

	if (readMode == READMODE_ADD_TO_UI && !IsOffsetUsed(offset))
		AddEvent(new NoteOnSeqEvent(this, key, vel, offset, length, sEventName));
	else if (readMode == READMODE_CONVERT_TO_MIDI)
		pMidiTrack->InsertNoteOn(channel, key+cKeyCorrection+transpose, finalVel, absTime);
	prevKey = key;
	prevVel = vel;
}

void SeqTrack::AddNoteOff(ULONG offset, ULONG length, char key, const wchar_t* sEventName)
{
	if (readMode == READMODE_ADD_TO_UI && !IsOffsetUsed(offset))
		AddEvent(new NoteOffSeqEvent(this, key, offset, length, sEventName));
	AddNoteOffNoItem(key);
}

void SeqTrack::AddNoteOffNoItem(char key)
{
	if (readMode != READMODE_CONVERT_TO_MIDI)
		return;

	if (cDrumNote == -1)
		pMidiTrack->AddNoteOff(channel, key+cKeyCorrection+transpose);
	else
		AddPercNoteOffNoItem(cDrumNote);
	return;
}


void SeqTrack::AddPercNoteOff(ULONG offset, ULONG length, char key,const wchar_t* sEventName)
{
	BYTE origChan = channel;
	channel = 9;
	char origDrumNote = cDrumNote;
	cDrumNote = -1;
//	DrumAssoc* pDrumAssoc = parentSeq->GetDrumAssoc(key);
//	if (pDrumAssoc)
//		key = pDrumAssoc->GMDrumNote;
	AddNoteOff(offset, length, key-transpose, sEventName);
	cDrumNote = origDrumNote;
	channel = origChan;
}

void SeqTrack::AddPercNoteOffNoItem(char key)
{
	BYTE origChan = channel;
	channel = 9;
	char origDrumNote = cDrumNote;
	cDrumNote = -1;
//	DrumAssoc* pDrumAssoc = parentSeq->GetDrumAssoc(key);
//	if (pDrumAssoc)
//		key = pDrumAssoc->GMDrumNote;
	AddNoteOffNoItem(key-transpose);
	cDrumNote = origDrumNote;
	channel = origChan;
}

void SeqTrack::InsertNoteOff(ULONG offset, ULONG length, char key, ULONG absTime, const wchar_t* sEventName)
{
	if (readMode == READMODE_ADD_TO_UI && !IsOffsetUsed(offset))
		AddEvent(new NoteOffSeqEvent(this, key, offset, length, sEventName));
	else if (readMode == READMODE_CONVERT_TO_MIDI)
		pMidiTrack->InsertNoteOff(channel, key+cKeyCorrection+transpose, absTime);
}

void SeqTrack::AddNoteByDur(ULONG offset, ULONG length, char key, char vel, UINT dur, const wchar_t* sEventName)
{
	if (readMode == READMODE_ADD_TO_UI && !IsOffsetUsed(offset))
		AddEvent(new DurNoteSeqEvent(this, key, vel, dur, offset, length, sEventName));
	AddNoteByDurNoItem(key, vel, dur);
}

void SeqTrack::AddNoteByDurNoItem(char key, char vel, UINT dur)
{
	if (readMode == READMODE_CONVERT_TO_MIDI)
	{
		BYTE finalVel = vel;
		if (parentSeq->bUseLinearAmplitudeScale)
			finalVel = Convert7bitPercentVolValToStdMidiVal(vel);

		if (cDrumNote == -1)
		{
			pMidiTrack->AddNoteByDur(channel, key+cKeyCorrection+transpose, finalVel, dur);
		}
		else
			AddPercNoteByDurNoItem(cDrumNote, vel, dur);
	}
	prevKey = key;
	prevVel = vel;
	return;
}

void SeqTrack::AddPercNoteByDur(ULONG offset, ULONG length, char key, char vel, UINT dur, const wchar_t* sEventName)
{
	BYTE origChan = channel;
	channel = 9;
	char origDrumNote = cDrumNote;
	cDrumNote = -1;
//	DrumAssoc* pDrumAssoc = parentSeq->GetDrumAssoc(key);
//	if (pDrumAssoc)
//		key = pDrumAssoc->GMDrumNote;
	AddNoteByDur(offset, length, key-transpose, vel, dur, sEventName);
	cDrumNote = origDrumNote;
	channel = origChan;
}

void SeqTrack::AddPercNoteByDurNoItem(char key, char vel, UINT dur)
{
	BYTE origChan = channel;
	channel = 9;
	char origDrumNote = cDrumNote;
	cDrumNote = -1;
//	DrumAssoc* pDrumAssoc = parentSeq->GetDrumAssoc(key);
//	if (pDrumAssoc)
//		key = pDrumAssoc->GMDrumNote;
	AddNoteByDurNoItem(key-transpose, vel, dur);
	cDrumNote = origDrumNote;
	channel = origChan;
}

/*void SeqTrack::AddNoteByDur(ULONG offset, ULONG length, char key, char vel, UINT dur, BYTE chan, const wchar_t* sEventName)
{
	BYTE origChan = channel;
	channel = chan;
	AddNoteByDur(offset, length, key, vel, dur, selectMsg, sEventName);
	channel = origChan;
}*/

void SeqTrack::InsertNoteByDur(ULONG offset, ULONG length, char key, char vel, UINT dur, ULONG absTime, const wchar_t* sEventName)
{
	if (readMode == READMODE_CONVERT_TO_MIDI)
	{
		BYTE finalVel = vel;
		if (parentSeq->bUseLinearAmplitudeScale)
			finalVel = Convert7bitPercentVolValToStdMidiVal(vel);

		if (!IsOffsetUsed(offset))
			AddEvent(new DurNoteSeqEvent(this, key, vel, dur, offset, length, sEventName));
		pMidiTrack->InsertNoteByDur(channel, key+cKeyCorrection+transpose, finalVel, dur, absTime);
	}
	prevKey = key;
	prevVel = vel;
}

void SeqTrack::MakePrevDurNoteEnd()
{
	if (readMode == READMODE_CONVERT_TO_MIDI && pMidiTrack->prevDurNoteOff)
		pMidiTrack->prevDurNoteOff->AbsTime = GetDelta();
}

void SeqTrack::AddVol(ULONG offset, ULONG length, BYTE newVol, const wchar_t* sEventName)
{	
	if (readMode == READMODE_ADD_TO_UI && !IsOffsetUsed(offset))
		AddEvent(new VolSeqEvent(this, newVol, offset, length, sEventName));
	AddVolNoItem(newVol);
}

void SeqTrack::AddVolNoItem(BYTE newVol)
{
	if (readMode == READMODE_CONVERT_TO_MIDI)
	{
		BYTE finalVol = newVol;
		if (parentSeq->bUseLinearAmplitudeScale)
			finalVol = Convert7bitPercentVolValToStdMidiVal(newVol);

		pMidiTrack->AddVol(channel, finalVol);
	}
	vol = newVol;
	return;
}

void SeqTrack::AddVolSlide(ULONG offset, ULONG length, ULONG dur, BYTE targVol, const wchar_t* sEventName)
{
	if (readMode == READMODE_ADD_TO_UI && !IsOffsetUsed(offset))
		AddEvent(new VolSlideSeqEvent(this, targVol, dur, offset, length, sEventName));
	else if (readMode == READMODE_CONVERT_TO_MIDI)
		AddControllerSlide(offset, length, dur, vol, targVol, &MidiTrack::InsertVol);
}

void SeqTrack::InsertVol(ULONG offset, ULONG length, BYTE newVol, ULONG absTime, const wchar_t* sEventName)
{
	BYTE finalVol = newVol;
	if (parentSeq->bUseLinearAmplitudeScale)
		finalVol = Convert7bitPercentVolValToStdMidiVal(newVol);

	if (readMode == READMODE_ADD_TO_UI && !IsOffsetUsed(offset))
		AddEvent(new VolSeqEvent(this, newVol, offset, length, sEventName));
	else if (readMode == READMODE_CONVERT_TO_MIDI)
		pMidiTrack->InsertVol(channel, finalVol, absTime);
	vol = newVol;
}

void SeqTrack::AddExpression(ULONG offset, ULONG length, BYTE level, const wchar_t* sEventName)
{
	if (readMode == READMODE_ADD_TO_UI && !IsOffsetUsed(offset))
		AddEvent(new ExpressionSeqEvent(this, level, offset, length, sEventName));
	AddExpressionNoItem(level);
}

void SeqTrack::AddExpressionNoItem(BYTE level)
{
	if (readMode == READMODE_CONVERT_TO_MIDI)
	{
		BYTE finalExpression = level;
		if (parentSeq->bUseLinearAmplitudeScale)
			finalExpression = Convert7bitPercentVolValToStdMidiVal(level);

		pMidiTrack->AddExpression(channel, finalExpression);
	}
	expression = level;
}

void SeqTrack::AddExpressionSlide(ULONG offset, ULONG length, ULONG dur, BYTE targExpr, const wchar_t* sEventName)
{
	if (readMode == READMODE_ADD_TO_UI && !IsOffsetUsed(offset))
		AddEvent(new ExpressionSlideSeqEvent(this, targExpr, dur, offset, length, sEventName));
	else if (readMode == READMODE_CONVERT_TO_MIDI)
		AddControllerSlide(offset, length, dur, expression, targExpr, &MidiTrack::InsertExpression);
}

void SeqTrack::InsertExpression(ULONG offset, ULONG length, BYTE level, ULONG absTime, const wchar_t* sEventName)
{
	BYTE finalExpression = level;
	if (parentSeq->bUseLinearAmplitudeScale)
		finalExpression = Convert7bitPercentVolValToStdMidiVal(level);

	if (readMode == READMODE_ADD_TO_UI && !IsOffsetUsed(offset))
		AddEvent(new ExpressionSeqEvent(this, level, offset, length, sEventName));
	else if (readMode == READMODE_CONVERT_TO_MIDI)
		pMidiTrack->InsertExpression(channel, finalExpression, absTime);
	expression = level;
}


void SeqTrack::AddMasterVol(ULONG offset, ULONG length, BYTE newVol, const wchar_t* sEventName)
{
	if (readMode == READMODE_ADD_TO_UI && !IsOffsetUsed(offset))
		AddEvent(new MastVolSeqEvent(this, newVol, offset, length, sEventName));
	AddMasterVolNoItem(newVol);
}

void SeqTrack::AddMasterVolNoItem(BYTE newVol)
{
	if (readMode != READMODE_CONVERT_TO_MIDI)
		return;

	BYTE finalVol = newVol;
	if (parentSeq->bUseLinearAmplitudeScale)
		finalVol = Convert7bitPercentVolValToStdMidiVal(newVol);

	pMidiTrack->AddMasterVol(channel, finalVol);
}

void SeqTrack::AddPan(ULONG offset, ULONG length, BYTE pan, const wchar_t* sEventName)
{
	if (readMode == READMODE_ADD_TO_UI && !IsOffsetUsed(offset))
		AddEvent(new PanSeqEvent(this, pan, offset, length, sEventName));
	AddPanNoItem(pan);
}

void SeqTrack::AddPanNoItem(BYTE pan)
{
	if (readMode == READMODE_CONVERT_TO_MIDI)
	{
		pMidiTrack->AddPan(channel, pan);
	}
	prevPan = pan;
}

void SeqTrack::AddPanSlide(ULONG offset, ULONG length, ULONG dur, BYTE targPan, const wchar_t* sEventName)
{
	if (readMode == READMODE_ADD_TO_UI && !IsOffsetUsed(offset))
		AddEvent(new PanSlideSeqEvent(this, targPan, dur, offset, length, sEventName));
	else if (readMode == READMODE_CONVERT_TO_MIDI)
		AddControllerSlide(offset, length, dur, prevPan, targPan, &MidiTrack::InsertPan);
}


void SeqTrack::InsertPan(ULONG offset, ULONG length, BYTE pan, ULONG absTime, const wchar_t* sEventName)
{
	if (readMode == READMODE_ADD_TO_UI && !IsOffsetUsed(offset))
		AddEvent(new PanSeqEvent(this, pan, offset, length, sEventName));
	else if (readMode == READMODE_CONVERT_TO_MIDI)
		pMidiTrack->InsertPan(channel, pan, absTime);
}

void SeqTrack::AddReverb(ULONG offset, ULONG length, BYTE reverb, const wchar_t* sEventName)
{
	if (readMode == READMODE_ADD_TO_UI && !IsOffsetUsed(offset))
		AddEvent(new ReverbSeqEvent(this, reverb, offset, length, sEventName));
	AddReverbNoItem(reverb);
}

void SeqTrack::AddReverbNoItem(BYTE reverb)
{
	if (readMode == READMODE_CONVERT_TO_MIDI)
	{
		pMidiTrack->AddReverb(channel, reverb);
	}
	prevReverb = reverb;
}

void SeqTrack::InsertReverb(ULONG offset, ULONG length, BYTE reverb, ULONG absTime, const wchar_t* sEventName)
{
	if (readMode == READMODE_ADD_TO_UI && !IsOffsetUsed(offset))
		AddEvent(new ReverbSeqEvent(this, reverb, offset, length, sEventName));
	else if (readMode == READMODE_CONVERT_TO_MIDI)
		pMidiTrack->InsertReverb(channel, reverb, absTime);
}

void SeqTrack::AddPitchBendMidiFormat(ULONG offset, ULONG length, BYTE lo, BYTE hi, const wchar_t* sEventName)
{
	AddPitchBend(offset, length, lo+(hi<<7)-0x2000, sEventName);
}

void SeqTrack::AddPitchBend(ULONG offset, ULONG length, SHORT bend, const wchar_t* sEventName)
{
	if (readMode == READMODE_ADD_TO_UI && !IsOffsetUsed(offset))
		AddEvent(new PitchBendSeqEvent(this, bend, offset, length, sEventName));
	else if (readMode == READMODE_CONVERT_TO_MIDI)
		pMidiTrack->AddPitchBend(channel, bend);
}

void SeqTrack::AddPitchBendRange(ULONG offset, ULONG length, BYTE semitones, BYTE cents, const wchar_t* sEventName)
{
	if (readMode == READMODE_ADD_TO_UI && !IsOffsetUsed(offset))
		AddEvent(new PitchBendRangeSeqEvent(this, semitones, cents, offset, length, sEventName));
	else if (readMode == READMODE_CONVERT_TO_MIDI)
		pMidiTrack->AddPitchBendRange(channel, semitones, cents);
}

void SeqTrack::AddPitchBendRangeNoItem(BYTE semitones, BYTE cents)
{
	if (readMode == READMODE_CONVERT_TO_MIDI)
		pMidiTrack->AddPitchBendRange(channel, semitones, cents);
}

void SeqTrack::AddTranspose(ULONG offset, ULONG length, char theTranspose, const wchar_t* sEventName)
{
	if (readMode == READMODE_ADD_TO_UI && !IsOffsetUsed(offset))
		AddEvent(new TransposeSeqEvent(this, transpose, offset, length, sEventName));
	//pMidiTrack->AddTranspose(transpose);
	transpose = theTranspose;
}


void SeqTrack::AddModulation(ULONG offset, ULONG length, BYTE depth, const wchar_t* sEventName)
{
	if (readMode == READMODE_ADD_TO_UI && !IsOffsetUsed(offset))
		AddEvent(new ModulationSeqEvent(this, depth, offset, length, sEventName));
	else if (readMode == READMODE_CONVERT_TO_MIDI)
		pMidiTrack->AddModulation(channel, depth);
}

void SeqTrack::InsertModulation(ULONG offset, ULONG length, BYTE depth, ULONG absTime, const wchar_t* sEventName)
{
	if (readMode == READMODE_ADD_TO_UI && !IsOffsetUsed(offset))
		AddEvent(new ModulationSeqEvent(this, depth, offset, length, sEventName));
	else if (readMode == READMODE_CONVERT_TO_MIDI)
		pMidiTrack->InsertModulation(channel, depth, absTime);
}

void SeqTrack::AddBreath(ULONG offset, ULONG length, BYTE depth, const wchar_t* sEventName)
{
	if (readMode == READMODE_ADD_TO_UI && !IsOffsetUsed(offset))
		AddEvent(new BreathSeqEvent(this, depth, offset, length, sEventName));
	else if (readMode == READMODE_CONVERT_TO_MIDI)
		pMidiTrack->AddBreath(channel, depth);
}

void SeqTrack::InsertBreath(ULONG offset, ULONG length, BYTE depth, ULONG absTime, const wchar_t* sEventName)
{
	if (readMode == READMODE_ADD_TO_UI && !IsOffsetUsed(offset))
		AddEvent(new BreathSeqEvent(this, depth, offset, length, sEventName));
	else if (readMode == READMODE_CONVERT_TO_MIDI)
		pMidiTrack->InsertBreath(channel, depth, absTime);
}

void SeqTrack::AddSustainEvent(ULONG offset, ULONG length, bool bOn, const wchar_t* sEventName)
{
	if (readMode == READMODE_ADD_TO_UI && !IsOffsetUsed(offset))
		AddEvent(new SustainSeqEvent(this, bOn, offset, length, sEventName));
	else if (readMode == READMODE_CONVERT_TO_MIDI)
		pMidiTrack->AddSustain(channel, bOn);
}

void SeqTrack::InsertSustainEvent(ULONG offset, ULONG length, bool bOn, ULONG absTime, const wchar_t* sEventName)
{
	if (readMode == READMODE_ADD_TO_UI && !IsOffsetUsed(offset))
		AddEvent(new SustainSeqEvent(this, bOn, offset, length, sEventName));
	else if (readMode == READMODE_CONVERT_TO_MIDI)
		pMidiTrack->InsertSustain(channel, bOn, absTime);
}

void SeqTrack::AddPortamento(ULONG offset, ULONG length, bool bOn, const wchar_t* sEventName)
{
	if (readMode == READMODE_ADD_TO_UI && !IsOffsetUsed(offset))
		AddEvent(new PortamentoSeqEvent(this, bOn, offset, length, sEventName));
	AddPortamentoNoItem(bOn);
}

void SeqTrack::AddPortamentoNoItem(bool bOn)
{
	if (readMode == READMODE_CONVERT_TO_MIDI)
		pMidiTrack->AddPortamento(channel, bOn);
}

void SeqTrack::InsertPortamento(ULONG offset, ULONG length, bool bOn, ULONG absTime, const wchar_t* sEventName)
{
	if (readMode == READMODE_ADD_TO_UI && !IsOffsetUsed(offset))
		AddEvent(new PortamentoSeqEvent(this, bOn, offset, length, sEventName));
	else if (readMode == READMODE_CONVERT_TO_MIDI)
		pMidiTrack->InsertPortamento(channel, bOn, absTime);
}

void SeqTrack::AddPortamentoTime(ULONG offset, ULONG length, BYTE time, const wchar_t* sEventName)
{
	if (readMode == READMODE_ADD_TO_UI && !IsOffsetUsed(offset))
		AddEvent(new PortamentoTimeSeqEvent(this, time, offset, length, sEventName));
	else if (readMode == READMODE_CONVERT_TO_MIDI)
		pMidiTrack->AddPortamentoTime(channel, time);
}

void SeqTrack::InsertPortamentoTime(ULONG offset, ULONG length, BYTE time, ULONG absTime, const wchar_t* sEventName)
{
	if (readMode == READMODE_ADD_TO_UI && !IsOffsetUsed(offset))
		AddEvent(new PortamentoTimeSeqEvent(this, time, offset, length, sEventName));
	else if (readMode == READMODE_CONVERT_TO_MIDI)
		pMidiTrack->InsertPortamentoTime(channel, time, absTime);
}



/*void InsertNoteOnEvent(char key, char vel, ULONG absTime);
void AddNoteOffEvent(char key);
void InsertNoteOffEvent(char key, char vel, ULONG absTime);
void AddNoteByDur(char key, char vel);
void InsertNoteByDur(char key, char vel, ULONG absTime);
void AddVolumeEvent(BYTE vol);
void InsertVolumeEvent(BYTE vol, ULONG absTime);
void AddExpression(BYTE expression);
void InsertExpression(BYTE expression, ULONG absTime);
void AddPanEvent(BYTE pan);
void InsertPanEvent(BYTE pan, ULONG absTime);*/

void SeqTrack::AddProgramChange(ULONG offset, ULONG length, ULONG progNum, const wchar_t* sEventName)
{
	AddProgramChange(offset, length, progNum, false, sEventName);
}

void SeqTrack::AddProgramChange(ULONG offset, ULONG length, ULONG progNum, BYTE chan, const wchar_t* sEventName)
{
	AddProgramChange(offset, length, progNum, false, chan, sEventName);
}

void SeqTrack::AddProgramChange(ULONG offset, ULONG length, ULONG progNum, bool requireBank, const wchar_t* sEventName)
{
/*	InstrAssoc* pInstrAssoc = parentSeq->GetInstrAssoc(progNum);
	if (pInstrAssoc)
	{
		if (pInstrAssoc->drumNote == -1)		//if this program uses a drum note
		{
			progNum = pInstrAssoc->GMProgNum;
			cKeyCorrection = pInstrAssoc->keyCorrection;
			cDrumNote = -1;
		}
		else
			cDrumNote = pInstrAssoc->drumNote;
	}
	else
		cDrumNote = -1;
*/
	if (readMode == READMODE_ADD_TO_UI)
	{
		if (!IsOffsetUsed(offset))
		{
			AddEvent(new ProgChangeSeqEvent(this, progNum, offset, length, sEventName));
		}
		parentSeq->AddInstrumentRef(progNum);
	}
	else if (readMode == READMODE_CONVERT_TO_MIDI)
	{
//		if (cDrumNote == -1)
//		{
		if (requireBank)
		{
			pMidiTrack->AddBankSelect(channel, (progNum >> 14) & 0x7f);
			pMidiTrack->AddBankSelectFine(channel, (progNum >> 7) & 0x7f);
		}
		pMidiTrack->AddProgramChange(channel, progNum & 0x7f);
//		}
	}
}

void SeqTrack::AddProgramChange(ULONG offset, ULONG length, ULONG progNum, bool requireBank, BYTE chan, const wchar_t* sEventName)
{
	//if (selectMsg = NULL)
	//	selectMsg.Forma
	BYTE origChan = channel;
	channel = chan;
	AddProgramChange(offset, length, progNum, requireBank, sEventName);
	channel = origChan;
}

void SeqTrack::AddBankSelectNoItem(BYTE bank)
{
	if (readMode == READMODE_CONVERT_TO_MIDI)
	{
		pMidiTrack->AddBankSelect(channel, bank/128);				
		pMidiTrack->AddBankSelectFine(channel, bank%128); 
	}
}

void SeqTrack::AddTempo(ULONG offset, ULONG length, ULONG microsPerQuarter, const wchar_t* sEventName)
{
	parentSeq->tempoBPM = ((double)60000000/microsPerQuarter);
	if (readMode == READMODE_ADD_TO_UI && !IsOffsetUsed(offset))
		AddEvent(new TempoSeqEvent(this, parentSeq->tempoBPM, offset, length, sEventName));
	else if (readMode == READMODE_CONVERT_TO_MIDI)
	{
		// Some MIDI tool can recognise tempo event only in the first track.
		parentSeq->aTracks[0]->pMidiTrack->InsertTempo(microsPerQuarter, pMidiTrack->GetDelta());
	}
}

void SeqTrack::AddTempoSlide(ULONG offset, ULONG length, ULONG dur, ULONG targMicrosPerQuarter, const wchar_t* sEventName)
{
	AddTempoBPMSlide(offset, length, dur, ((double)60000000/targMicrosPerQuarter), sEventName);
}

void SeqTrack::AddTempoBPM(ULONG offset, ULONG length, double bpm, const wchar_t* sEventName)
{
	if (readMode == READMODE_ADD_TO_UI && !IsOffsetUsed(offset))
		AddEvent(new TempoSeqEvent(this, bpm, offset, length, sEventName));
	AddTempoBPMNoItem(bpm);
}

void SeqTrack::AddTempoBPMNoItem(double bpm)
{
	parentSeq->tempoBPM = bpm;
	if (readMode == READMODE_CONVERT_TO_MIDI)
	{
		// Some MIDI tool can recognise tempo event only in the first track.
		parentSeq->aTracks[0]->pMidiTrack->InsertTempoBPM(bpm, pMidiTrack->GetDelta());
	}
}

void SeqTrack::AddTempoBPMSlide(ULONG offset, ULONG length, ULONG dur, double targBPM, const wchar_t* sEventName)
{
	if (readMode == READMODE_ADD_TO_UI && !IsOffsetUsed(offset))
		AddEvent(new TempoSlideSeqEvent(this, targBPM, dur, offset, length, sEventName));
	else if (readMode == READMODE_CONVERT_TO_MIDI)
	{
		double tempoInc = (targBPM-parentSeq->tempoBPM)/((double)dur);
		double newTempo;
		for (unsigned int i=0; i<dur; i++)
		{
			newTempo=parentSeq->tempoBPM+(tempoInc*(i+1));
			pMidiTrack->InsertTempoBPM(newTempo, GetDelta()+i);
		}
	}
	parentSeq->tempoBPM = targBPM;
}

void SeqTrack::AddTimeSig(ULONG offset, ULONG length, BYTE numer, BYTE denom, BYTE ticksPerQuarter,  const wchar_t* sEventName)
{
	if (readMode == READMODE_ADD_TO_UI && !IsOffsetUsed(offset))
		AddEvent(new TimeSigSeqEvent(this, numer, denom, ticksPerQuarter, offset, length, sEventName));
	else if (readMode == READMODE_CONVERT_TO_MIDI)
		pMidiTrack->AddTimeSig(numer, denom, ticksPerQuarter);
}

void SeqTrack::InsertTimeSig(ULONG offset, ULONG length, BYTE numer, BYTE denom, BYTE ticksPerQuarter,ULONG absTime,const wchar_t* sEventName)
{
	if (readMode == READMODE_ADD_TO_UI && !IsOffsetUsed(offset))
		AddEvent(new TimeSigSeqEvent(this, numer, denom, ticksPerQuarter, offset, length, sEventName));
	else if (readMode == READMODE_CONVERT_TO_MIDI)
		pMidiTrack->InsertTimeSig(numer, denom, ticksPerQuarter, absTime);
}

bool SeqTrack::AddEndOfTrack(ULONG offset, ULONG length, const wchar_t* sEventName)
{
	if (readMode == READMODE_ADD_TO_UI && !IsOffsetUsed(offset))
		AddEvent(new TrackEndSeqEvent(this, offset, length, sEventName));
	else if (readMode == READMODE_CONVERT_TO_MIDI)
		pMidiTrack->AddEndOfTrack();
	return AddEndOfTrackNoItem();
}

bool SeqTrack::AddEndOfTrackNoItem()
{
	if (readMode == READMODE_FIND_DELTA_LENGTH)
		deltaLength = GetDelta();
	return false;
}

void SeqTrack::AddGlobalTranspose(ULONG offset, ULONG length, char semitones, const wchar_t* sEventName)
{
	if (readMode == READMODE_ADD_TO_UI && !IsOffsetUsed(offset))
		AddEvent(new TransposeSeqEvent(this, semitones, offset, length, sEventName));
	else if (readMode == READMODE_CONVERT_TO_MIDI)
		parentSeq->midi->globalTrack.InsertGlobalTranspose(GetDelta(), semitones);
	//pMidiTrack->(channel, transpose);
}

void SeqTrack::AddMarker(ULONG offset, ULONG length, string& markername, BYTE databyte1, BYTE databyte2,
						 const wchar_t* sEventName, char priority, BYTE color)
{
	if (!IsOffsetUsed(offset))
		AddEvent(new MarkerSeqEvent(this, markername, databyte1, databyte2, offset, length, sEventName, color));
	else if (readMode == READMODE_CONVERT_TO_MIDI)
		pMidiTrack->AddMarker(channel, markername, databyte1, databyte2, priority);
}

// when in FIND_DELTA_LENGTH mode, returns false when we've hit the max number of loops defined in options
bool SeqTrack::AddLoopForever(ULONG offset, ULONG length, const wchar_t* sEventName)
{
	if (readMode == READMODE_ADD_TO_UI)
	{
		if (!IsOffsetUsed(offset))
			AddEvent(new LoopForeverSeqEvent(this, offset, length, sEventName));
		return false;
	}
	else if (readMode == READMODE_FIND_DELTA_LENGTH)
	{
		deltaLength = GetDelta();
		return (this->foreverLoops++ < ConversionOptions::GetNumSequenceLoops());
	}
	return true;

}
