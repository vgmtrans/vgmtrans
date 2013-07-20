#include "stdafx.h"
#include "MidiFile.h"
#include "VGMSeq.h"
#include "common.h"
#include "Root.h"
#include <math.h>
#include <algorithm>

using namespace std;


MidiFile::MidiFile(VGMSeq* theAssocSeq)
: assocSeq(theAssocSeq),
  globalTrack(this, false),
  globalTranspose(0),
  bMonophonicTracks(false)
{
	this->bMonophonicTracks = assocSeq->bMonophonicTracks;
	this->globalTrack.bMonophonic = this->bMonophonicTracks;

	this->ppqn = assocSeq->ppqn;
}


MidiFile::~MidiFile(void)
{
	DeleteVect<MidiTrack>(aTracks);
}

MidiTrack* MidiFile::AddTrack(void)
{
	aTracks.push_back(new MidiTrack(this, bMonophonicTracks));
		return aTracks.back();
}

MidiTrack* MidiFile::InsertTrack(int trackNum)
{
	if (trackNum+1 > aTracks.size())
		aTracks.resize(trackNum+1, NULL);
	aTracks[trackNum] = new MidiTrack(this, bMonophonicTracks);
		return aTracks[trackNum];
}

void MidiFile::SetPPQN(WORD ppqn)
{
	this->ppqn = ppqn;
}

UINT MidiFile::GetPPQN()
{
	return ppqn;
}

void MidiFile::Sort(void)
{
	for (UINT i=0; i < aTracks.size(); i++)
	{
		if (aTracks[i])
		{
			if (aTracks[i]->aEvents.size() == 0)
			{
				delete aTracks[i];
				aTracks.erase(aTracks.begin() + i--);
			}
			else
				aTracks[i]->Sort();
		}
	}
}


bool MidiFile::SaveMidiFile(const wchar_t* filepath)
{
	vector<BYTE> midiBuf;
	WriteMidiToBuffer(midiBuf);
	return pRoot->UI_WriteBufferToFile(filepath, &midiBuf[0], midiBuf.size());
}

void MidiFile::WriteMidiToBuffer(vector<BYTE> & buf)
{
	//int nNumTracks = 0;
	//for (UINT i=0; i < aTracks.size(); i++)
	//{
	//	if (aTracks[i])
	//		nNumTracks++;
	//}
	int nNumTracks = aTracks.size();
	buf.push_back('M');
	buf.push_back('T');
	buf.push_back('h');
	buf.push_back('d');
	buf.push_back(0);
	buf.push_back(0);
	buf.push_back(0);
	buf.push_back(6);				//MThd length - always 6
	buf.push_back(0);
	buf.push_back(1);				//Midi format - type 1
	buf.push_back((nNumTracks & 0xFF00) >> 8);		//num tracks hi
	buf.push_back(nNumTracks & 0x00FF);				//num tracks lo
	buf.push_back((ppqn & 0xFF00) >> 8);
	buf.push_back(ppqn & 0xFF);

	//if (!bAddedTimeSig)
	//	aTracks[0]->InsertTimeSig(4, 4, 16, 0);
	//if (!bAddedTempo)
	//	aTracks[0]->InsertTempoBPM(148, 0);

	Sort();

	for (UINT i=0; i < aTracks.size(); i++)
	{
		if (aTracks[i])
		{
			vector<BYTE> trackBuf;
			globalTranspose = 0;
			aTracks[i]->WriteTrack(trackBuf);
			buf.insert(buf.end(), trackBuf.begin(), trackBuf.end());
		}
	}
	globalTranspose = 0;
}


//  *********
//  MidiTrack
//  *********

MidiTrack::MidiTrack(MidiFile* theParentSeq, bool monophonic)
: parentSeq(theParentSeq),
  bMonophonic(monophonic),
  DeltaTime(0),
  prevDurEvent(NULL),
  prevDurNoteOff(NULL),
  prevKey(0),
  channelGroup(0),
  bHasEndOfTrack(false)
{

}

MidiTrack::~MidiTrack(void)
{
	DeleteVect<MidiEvent>(aEvents);
}

//UINT MidiTrack::GetSize(void)
//{
//	ULONG size = 0;
//	int nNumEvents = aEvents.size();
//	for (int i=0; i < nNumEvents; i++)
//		size += aEvents[i]->GetSize();
//	return size;
//}

void MidiTrack::Sort(void)
{
	stable_sort(aEvents.begin(), aEvents.end(), PriorityCmp());	//Sort all the events by priority
	stable_sort(aEvents.begin(), aEvents.end(), AbsTimeCmp());	//Sort all the events by absolute time, so that delta times can be recorded correctly
	if (!bHasEndOfTrack && aEvents.size())
	{
		aEvents.push_back(new EndOfTrackEvent(this, aEvents.back()->AbsTime));
		bHasEndOfTrack = true;
	}
}

void MidiTrack::WriteTrack(vector<BYTE> & buf)
{
	buf.push_back('M');
	buf.push_back('T');
	buf.push_back('r');
	buf.push_back('k');
	buf.push_back(0);
	buf.push_back(0);
	buf.push_back(0);
	buf.push_back(0);
	UINT time = 0;				//start at 0 ticks

	//For all the events, call their preparewrite function to put appropriate (writable) events in aFinalEvents
	//int nNumEvents = aEvents.size();
	//for (int i=0; i<nNumEvents; i++)
	//	aEvents[i]->PrepareWrite(aFinalEvents);

	vector<MidiEvent*> finalEvents(aEvents);
	vector<MidiEvent*>& globEvents = parentSeq->globalTrack.aEvents;
	finalEvents.insert(finalEvents.end(), globEvents.begin(), globEvents.end());

	stable_sort(finalEvents.begin(), finalEvents.end(), PriorityCmp());	//Sort all the events by priority
	stable_sort(finalEvents.begin(), finalEvents.end(), AbsTimeCmp());	//Sort all the events by absolute time, so that delta times can be recorded correctly

	int numEvents = finalEvents.size();

	//sort(aFinalEvents.begin(), aFinalEvents.end(), PriorityCmp());	//Sort all the events by priority
	//stable_sort(aFinalEvents.begin(), aFinalEvents.end(), AbsTimeCmp());	//Sort all the events by absolute time, so that delta times can be recorded correctly
	//if (!bHasEndOfTrack && aFinalEvents.size())
	//	aFinalEvents.push_back(new EndOfTrackEvent(this, aFinalEvents.back()->AbsTime));
	//int nNumEvents = aEvents.size();
	for (int i=0; i<numEvents; i++)
		time = finalEvents[i]->WriteEvent(buf, time);		//write all events into the buffer

	ULONG trackSize = buf.size() - 8;						//-8 for MTrk and size that shouldn't be accounted for
	buf[4] = (BYTE)((trackSize & 0xFF000000) >> 24);
	buf[5] = (BYTE)((trackSize & 0x00FF0000) >> 16);
	buf[6] = (BYTE)((trackSize & 0x0000FF00) >> 8);
	buf[7] = (BYTE)(trackSize & 0x000000FF);
}

void MidiTrack::SetChannelGroup(int theChannelGroup)
{
	channelGroup = theChannelGroup;
}

//Delta Time Functions
ULONG MidiTrack::GetDelta(void)
{
	return DeltaTime;
}

void MidiTrack::SetDelta(ULONG NewDelta)
{
	DeltaTime = NewDelta;
}

void MidiTrack::AddDelta(ULONG AddDelta)
{
	DeltaTime += AddDelta;
}

void MidiTrack::SubtractDelta(ULONG SubtractDelta)
{
	DeltaTime -= SubtractDelta;
}

void MidiTrack::ResetDelta(void)
{
	DeltaTime = 0;
}


void MidiTrack::AddNoteOn(BYTE channel, char key, char vel)
{
	aEvents.push_back(new NoteEvent(this, channel, GetDelta(), true, key, vel));

	//WriteVarLen(delta_time+rest_time, hFile);
	//aMidi.Add(0x90 + channel_num);
	//aMidi.Add(key);
	//aMidi.Add(vel);
	//}
	//current_vel = vel;	
	//prev_key = key;
	//rest_time = 0;
}

void MidiTrack::InsertNoteOn(BYTE channel, char key, char vel, ULONG absTime)
{
	aEvents.push_back(new NoteEvent(this, channel, absTime, true, key, vel));
}

void MidiTrack::AddNoteOff(BYTE channel, char key)
{
	aEvents.push_back(new NoteEvent(this, channel, GetDelta(), false, key));
}

void MidiTrack::InsertNoteOff(BYTE channel, char key, ULONG absTime)
{
	aEvents.push_back(new NoteEvent(this, channel, absTime, false, key));
}

void MidiTrack::AddNoteByDur(BYTE channel, char key, char vel, ULONG duration)
{
	aEvents.push_back(new NoteEvent(this, channel, GetDelta(), true, key, vel));		//add note on
	prevDurNoteOff = new NoteEvent(this, channel, GetDelta()+duration, false, key);
	aEvents.push_back(prevDurNoteOff);	//add note off at end of dur
}

void MidiTrack::InsertNoteByDur(BYTE channel, char key, char vel, ULONG duration, ULONG absTime)
{
	aEvents.push_back(new NoteEvent(this, channel, absTime, true, key, vel));		//add note on
	prevDurNoteOff = new NoteEvent(this, channel, absTime+duration, false, key);
	aEvents.push_back(prevDurNoteOff);	//add note off at end of dur
}

/*void MidiTrack::AddVolMarker(BYTE channel, BYTE vol, char priority)
{
	MidiEvent* newEvent = new VolMarkerEvent(this, channel, GetDelta(), vol);
	aEvents.push_back(newEvent);
}

void MidiTrack::InsertVolMarker(BYTE channel, BYTE vol, ULONG absTime, char priority)
{
	MidiEvent* newEvent = new VolMarkerEvent(this, channel, absTime, vol);
	aEvents.push_back(newEvent);
}*/

void MidiTrack::AddVol(BYTE channel, BYTE vol)
{
	aEvents.push_back(new VolumeEvent(this, channel, GetDelta(), vol));
}

void MidiTrack::InsertVol(BYTE channel, BYTE vol, ULONG absTime)
{
	aEvents.push_back(new VolumeEvent(this, channel, absTime, vol));
}

//mast volume has a full byte of resolution
void MidiTrack::AddMasterVol(BYTE channel, BYTE mastVol)
{
	MidiEvent* newEvent = new MastVolEvent(this, channel, GetDelta(), mastVol);
	aEvents.push_back(newEvent);
}

void MidiTrack::InsertMasterVol(BYTE channel, BYTE mastVol, ULONG absTime)
{
	MidiEvent* newEvent = new MastVolEvent(this, channel, absTime, mastVol);
	aEvents.push_back(newEvent);
}

void MidiTrack::AddExpression(BYTE channel, BYTE expression)
{
	aEvents.push_back(new ExpressionEvent(this, channel, GetDelta(), expression));
}

void MidiTrack::InsertExpression(BYTE channel, BYTE expression, ULONG absTime)
{
	aEvents.push_back(new ExpressionEvent(this, channel, absTime, expression));
}

void MidiTrack::AddSustain(BYTE channel, bool bOn)
{
	aEvents.push_back(new SustainEvent(this, channel, GetDelta(), bOn));
}

void MidiTrack::InsertSustain(BYTE channel, bool bOn, ULONG absTime)
{
	aEvents.push_back(new SustainEvent(this, channel, absTime, bOn));
}

void MidiTrack::AddPortamento(BYTE channel, bool bOn)
{
	aEvents.push_back(new PortamentoEvent(this, channel, GetDelta(), bOn));
}

void MidiTrack::InsertPortamento(BYTE channel, bool bOn, ULONG absTime)
{
	aEvents.push_back(new PortamentoEvent(this, channel, absTime, bOn));
}

void MidiTrack::AddPortamentoTime(BYTE channel, BYTE time)
{
	aEvents.push_back(new PortamentoTimeEvent(this, channel, GetDelta(), time));
}

void MidiTrack::InsertPortamentoTime(BYTE channel, BYTE time, ULONG absTime)
{
	aEvents.push_back(new PortamentoTimeEvent(this, channel, absTime, time));
}

void MidiTrack::AddPan(BYTE channel, BYTE pan)
{
	aEvents.push_back(new PanEvent(this, channel, GetDelta(), pan));
}

void MidiTrack::InsertPan(BYTE channel, BYTE pan, ULONG absTime)
{
	aEvents.push_back(new PanEvent(this, channel, absTime, pan));
}

void MidiTrack::AddReverb(BYTE channel, BYTE reverb)
{
	aEvents.push_back(new ControllerEvent(this, channel, GetDelta(), 91, reverb));
}

void MidiTrack::InsertReverb(BYTE channel, BYTE reverb, ULONG absTime)
{
	aEvents.push_back(new ControllerEvent(this, channel, absTime, 91, reverb));
}

void MidiTrack::AddModulation(BYTE channel, BYTE depth)
{
	aEvents.push_back(new ModulationEvent(this, channel, GetDelta(), depth));
}

void MidiTrack::InsertModulation(BYTE channel, BYTE depth, ULONG absTime)
{
	aEvents.push_back(new ControllerEvent(this, channel, absTime, 1, depth));
}

void MidiTrack::AddBreath(BYTE channel, BYTE depth)
{
	aEvents.push_back(new BreathEvent(this, channel, GetDelta(), depth));
}

void MidiTrack::InsertBreath(BYTE channel, BYTE depth, ULONG absTime)
{
	aEvents.push_back(new ControllerEvent(this, channel, absTime, 2, depth));
}

void MidiTrack::AddPitchBend(BYTE channel, SHORT bend)
{
	aEvents.push_back(new PitchBendEvent(this, channel, GetDelta(), bend));
}

void MidiTrack::InsertPitchBend(BYTE channel, SHORT bend, ULONG absTime)
{
	aEvents.push_back(new PitchBendEvent(this, channel, absTime, bend));
}

void MidiTrack::AddPitchBendRange(BYTE channel, BYTE semitones, BYTE cents)
{
	InsertPitchBendRange(channel, semitones, cents, GetDelta());
}

void MidiTrack::InsertPitchBendRange(BYTE channel, BYTE semitones, BYTE cents, ULONG absTime)
{
	aEvents.push_back(new ControllerEvent(this, channel, absTime, 101, 0, PRIORITY_HIGHER-1));	//want to give them a unique priority so they are grouped together correction
	aEvents.push_back(new ControllerEvent(this, channel, absTime, 100, 0, PRIORITY_HIGHER-1));
	aEvents.push_back(new ControllerEvent(this, channel, absTime,  6, semitones, PRIORITY_HIGHER-1));
	aEvents.push_back(new ControllerEvent(this, channel, absTime, 38, cents, PRIORITY_HIGHER-1));
}

//void MidiTrack::AddTranspose(BYTE channel, int transpose)
//{
//	aEvents.push_back(new ControllerEvent(this, channel, GetDelta(), 101, 0));
//	aEvents.push_back(new ControllerEvent(this, channel, GetDelta(), 100, 2));
//	aEvents.push_back(new ControllerEvent(this, channel, GetDelta(), 6, 64 - transpose));
//}

void MidiTrack::AddProgramChange(BYTE channel, BYTE progNum)
{
	aEvents.push_back(new ProgChangeEvent(this, channel, GetDelta(), progNum));
}

void MidiTrack::AddBankSelect(BYTE channel, BYTE bank)
{
	aEvents.push_back(new BankSelectEvent(this, channel, GetDelta(), bank));
}

void MidiTrack::AddBankSelectFine(BYTE channel, BYTE lsb)
{
	aEvents.push_back(new BankSelectFineEvent(this, channel, GetDelta(), lsb));
}

void MidiTrack::InsertBankSelect(BYTE channel, BYTE bank, ULONG absTime)
{
	aEvents.push_back(new ControllerEvent(this, channel, absTime, 0, bank));
}


void MidiTrack::AddTempo(ULONG microSeconds)
{
	aEvents.push_back(new TempoEvent(this, GetDelta(), microSeconds));
	//bAddedTempo = true;
}

void MidiTrack::AddTempoBPM(double BPM)
{
	ULONG microSecs = (ULONG)round((double)60000000/BPM);
	aEvents.push_back(new TempoEvent(this, GetDelta(), microSecs));
	//bAddedTempo = true;
}

void MidiTrack::InsertTempo(ULONG microSeconds, ULONG absTime)
{
	aEvents.push_back(new TempoEvent(this, absTime, microSeconds));
	//bAddedTempo = true;
}

void MidiTrack::InsertTempoBPM(double BPM, ULONG absTime)
{
	ULONG microSecs = (ULONG)round((double)60000000/BPM);
	aEvents.push_back(new TempoEvent(this, absTime, microSecs));
	//bAddedTempo = true;
}


void MidiTrack::AddTimeSig(BYTE numer, BYTE denom, BYTE ticksPerQuarter)
{
	aEvents.push_back(new TimeSigEvent(this, GetDelta(), numer, denom, ticksPerQuarter));
	//bAddedTimeSig = true;
}

void MidiTrack::InsertTimeSig(BYTE numer, BYTE denom, BYTE ticksPerQuarter, ULONG absTime)
{
	aEvents.push_back(new TimeSigEvent(this, absTime, numer, denom, ticksPerQuarter));
	//bAddedTimeSig = true;
}

void MidiTrack::AddEndOfTrack(void)
{
	aEvents.push_back(new EndOfTrackEvent(this, GetDelta()));
	bHasEndOfTrack = true;
}

void MidiTrack::InsertEndOfTrack(ULONG absTime)
{
	aEvents.push_back(new EndOfTrackEvent(this, absTime));
	bHasEndOfTrack = true;
}

void MidiTrack::AddText(const wchar_t* wstr)
{
	aEvents.push_back(new TextEvent(this, GetDelta(), wstr));
}

void MidiTrack::InsertText(const wchar_t* wstr, ULONG absTime)
{
	aEvents.push_back(new TextEvent(this, absTime, wstr));
}

void MidiTrack::AddSeqName(const wchar_t* wstr)
{
	aEvents.push_back(new SeqNameEvent(this, GetDelta(), wstr));
}

void MidiTrack::InsertSeqName(const wchar_t* wstr, ULONG absTime)
{
	aEvents.push_back(new SeqNameEvent(this, absTime, wstr));
}

void MidiTrack::AddTrackName(const wchar_t* wstr)
{
	aEvents.push_back(new TrackNameEvent(this, GetDelta(), wstr));
}

void MidiTrack::InsertTrackName(const wchar_t* wstr, ULONG absTime)
{
	aEvents.push_back(new TrackNameEvent(this, absTime, wstr));
}

// SPECIAL NON-MIDI EVENTS

// Transpose events offset the key when we write the Midi file.
//  used to implement global transpose events found in QSound

//void MidiTrack::AddTranspose(char semitones)
//{
//	aEvents.push_back(new TransposeEvent(this, GetDelta(), semitones));
//}

void MidiTrack::InsertGlobalTranspose(ULONG absTime, char semitones)
{
	aEvents.push_back(new GlobalTransposeEvent(this, absTime, semitones));
}



void MidiTrack::AddMarker(BYTE channel, string& markername, BYTE databyte1, BYTE databyte2, char priority)
{
	aEvents.push_back(new MarkerEvent(this, channel, GetDelta(), markername, databyte1, databyte2, priority));
}



//  *********
//  MidiEvent
//  *********

MidiEvent::MidiEvent(MidiTrack* thePrntTrk, ULONG absoluteTime, BYTE theChannel, char thePriority)
: prntTrk(thePrntTrk), AbsTime(absoluteTime), channel(theChannel), priority(thePriority)
{
}

MidiEvent::~MidiEvent(void)
{
}

void MidiEvent::WriteVarLength(vector<BYTE> & buf, ULONG value)
{
   register unsigned long buffer;
   buffer = value & 0x7F;

   while ( (value >>= 7) )
   {
     buffer <<= 8;
     buffer |= ((value & 0x7F) | 0x80);
   }

   while (true)
   {
	  buf.push_back((BYTE)buffer);
      if (buffer & 0x80)
          buffer >>= 8;
      else
          break;
   }
}

ULONG MidiEvent::WriteSysexEvent(vector<BYTE> & buf, UINT time, BYTE* data, size_t dataSize)
{
	WriteVarLength(buf, AbsTime-time);
	buf.push_back(0xF0);
	WriteVarLength(buf, dataSize + 1);
	for (size_t dataIndex = 0; dataIndex < dataSize; dataIndex++)
	{
		buf.push_back(data[dataIndex]);
	}
	buf.push_back(0xF7);
	return AbsTime;
}

ULONG MidiEvent::WriteMetaEvent(vector<BYTE> & buf, UINT time, BYTE metaType, BYTE* data, size_t dataSize)
{
	WriteVarLength(buf, AbsTime-time);
	buf.push_back(0xFF);
	buf.push_back(metaType);
	WriteVarLength(buf, dataSize);
	for (size_t dataIndex = 0; dataIndex < dataSize; dataIndex++)
	{
		buf.push_back(data[dataIndex]);
	}
	return AbsTime;
}

ULONG MidiEvent::WriteMetaTextEvent(vector<BYTE> & buf, UINT time, BYTE metaType, wstring wstr)
{
	string str = wstring2string(wstr);
	return WriteMetaEvent(buf, time, metaType, (BYTE*)str.c_str(), str.length());
}

//void MidiEvent::PrepareWrite(vector<MidiEvent*> & aEvents)
//{
	//aEvents.push_back(MakeCopy());
//}


bool MidiEvent::operator<(const MidiEvent &theMidiEvent) const
{
	return (AbsTime < theMidiEvent.AbsTime);
}

bool MidiEvent::operator>(const MidiEvent &theMidiEvent) const
{
	return (AbsTime > theMidiEvent.AbsTime);
}


//  *********
//  NoteEvent
//  *********


NoteEvent::NoteEvent(MidiTrack* prntTrk, BYTE channel, ULONG absoluteTime, bool bnoteDown, BYTE theKey, BYTE theVel)
: MidiEvent(prntTrk, absoluteTime, channel, PRIORITY_LOWER),
  bNoteDown(bnoteDown),
  key(theKey),
  vel(theVel)
{
}

//NoteEvent* NoteEvent::MakeCopy()
//{
//	return new NoteEvent(prntTrk, channel, AbsTime, bNoteDown, key, vel);
//}

ULONG NoteEvent::WriteEvent(vector<BYTE> & buf, UINT time)
{
	WriteVarLength(buf, AbsTime-time);
	if (bNoteDown)
		buf.push_back(0x90+channel);
	else
		buf.push_back(0x80+channel);

	if (prntTrk->bMonophonic && !bNoteDown)
		buf.push_back(prntTrk->prevKey);
	else
	{
		prntTrk->prevKey = key + ((channel == 9) ? 0 : prntTrk->parentSeq->globalTranspose);
		buf.push_back(prntTrk->prevKey);
	}
	//buf.push_back(key);

	buf.push_back(vel); 

	return AbsTime;
}

//  ************
//  DurNoteEvent
//  ************

//DurNoteEvent::DurNoteEvent(MidiTrack* prntTrk, BYTE channel, ULONG absoluteTime, BYTE theKey, BYTE theVel, ULONG theDur)
//: MidiEvent(prntTrk, absoluteTime, channel, PRIORITY_LOWER), key(theKey), vel(theVel), duration(theDur)
//{
//}
/*
DurNoteEvent* DurNoteEvent::MakeCopy()
{
	return new DurNoteEvent(prntTrk, channel, AbsTime, key, vel, duration);
}*/

/*void DurNoteEvent::PrepareWrite(vector<MidiEvent*> & aEvents)
{
	prntTrk->aEvents.push_back(new NoteEvent(prntTrk, channel, AbsTime, true, key, vel));	//add note on
	prntTrk->aEvents.push_back(new NoteEvent(prntTrk, channel, AbsTime+duration, false, key, vel));  //add note off at end of dur
}*/

//ULONG DurNoteEvent::WriteEvent(vector<BYTE> & buf, UINT time)		//we do note use WriteEvent on DurNoteEvents... this is what PrepareWrite is for, to create NoteEvents in substitute
//{
//	return false;
//}


//  ********
//  VolEvent
//  ********

/*VolEvent::VolEvent(MidiTrack *prntTrk, BYTE channel, ULONG absoluteTime, BYTE theVol, char thePriority)
: ControllerEvent(prntTrk, channel, absoluteTime, 7), vol(theVol)
{
}

VolEvent* VolEvent::MakeCopy()
{
	return new VolEvent(prntTrk, channel, AbsTime, vol, priority);
}*/


//  ************
//  MastVolEvent
//  ************

MastVolEvent::MastVolEvent(MidiTrack *prntTrk, BYTE channel, ULONG absoluteTime, BYTE theMastVol)
: MidiEvent(prntTrk, absoluteTime, channel, PRIORITY_HIGHER), mastVol(theMastVol)
{
}

ULONG MastVolEvent::WriteEvent(vector<BYTE> & buf, UINT time)
{
	BYTE data[6] = { 0x7F, 0x7F, 0x04, 0x01, /*LSB*/0, mastVol & 0x7F };
	return WriteSysexEvent(buf, time, data, 6);
}

//  ***************
//  ControllerEvent
//  ***************

ControllerEvent::ControllerEvent(MidiTrack* prntTrk, BYTE channel, ULONG absoluteTime, BYTE controllerNum, BYTE theDataByte, char thePriority)
: MidiEvent(prntTrk, absoluteTime, channel, thePriority), controlNum(controllerNum), dataByte(theDataByte)
{
}

ULONG ControllerEvent::WriteEvent(vector<BYTE> & buf, UINT time)
{
 	WriteVarLength(buf, AbsTime-time);
	buf.push_back(0xB0+channel);
	buf.push_back(controlNum & 0x7F);
	buf.push_back(dataByte);
	return AbsTime;
}

//  ***************
//  ProgChangeEvent
//  ***************

ProgChangeEvent::ProgChangeEvent(MidiTrack* prntTrk, BYTE channel, ULONG absoluteTime, BYTE progNum)
: MidiEvent(prntTrk, absoluteTime, channel, PRIORITY_HIGH), programNum(progNum)
{
}

ULONG ProgChangeEvent::WriteEvent(vector<BYTE> & buf, UINT time)
{
	WriteVarLength(buf, AbsTime-time);
	buf.push_back(0xC0+channel);
	buf.push_back(programNum & 0x7F);
	return AbsTime;
}


//  **************
//  PitchBendEvent
//  **************

PitchBendEvent::PitchBendEvent(MidiTrack* prntTrk, BYTE channel, ULONG absoluteTime, SHORT bendAmt)
: MidiEvent(prntTrk, absoluteTime, channel, PRIORITY_MIDDLE), bend(bendAmt)
{
}

ULONG PitchBendEvent::WriteEvent(vector<BYTE> & buf, UINT time)
{
	BYTE loByte = (bend+0x2000) & 0x7F;
	BYTE hiByte = ((bend+0x2000) & 0x3F80) >> 7;
	WriteVarLength(buf, AbsTime-time);
	buf.push_back(0xE0+channel);
	buf.push_back(loByte);
	buf.push_back(hiByte);
	return AbsTime;
}

//  **********
//  TempoEvent
//  **********

TempoEvent::TempoEvent(MidiTrack* prntTrk, ULONG absoluteTime, ULONG microSeconds)
: MidiEvent(prntTrk, absoluteTime, 0, PRIORITY_HIGHEST), microSecs(microSeconds)
{
}

ULONG TempoEvent::WriteEvent(vector<BYTE> & buf, UINT time)
{
	BYTE data[3] = {
		(BYTE)((microSecs & 0xFF0000) >> 16),
		(BYTE)((microSecs & 0x00FF00) >> 8),
		(BYTE)(microSecs & 0x0000FF)
	};
	return WriteMetaEvent(buf, time, 0x51, data, 3);
}


//  ************
//  TimeSigEvent
//  ************

TimeSigEvent::TimeSigEvent(MidiTrack* prntTrk, ULONG absoluteTime, BYTE numerator, BYTE denominator, BYTE clicksPerQuarter)
: MidiEvent(prntTrk, absoluteTime, 0, PRIORITY_HIGHEST), numer(numerator), denom(denominator), ticksPerQuarter(clicksPerQuarter)
{
}

ULONG TimeSigEvent::WriteEvent(vector<BYTE> & buf, UINT time)
{
	BYTE data[4] = {
		numer,
		(BYTE)(log((double)denom)/0.69314718055994530941723212145818),				//denom is expressed in power of 2... so if we have 6/8 time.  it's 6 = 2^x  ==  ln6 / ln2
		ticksPerQuarter/*/4*/,
		8
	};
	return WriteMetaEvent(buf, time, 0x58, data, 4);}


//  ***************
//  EndOfTrackEvent
//  ***************

EndOfTrackEvent::EndOfTrackEvent(MidiTrack* prntTrk, ULONG absoluteTime)
: MidiEvent(prntTrk, absoluteTime, 0, PRIORITY_LOWEST)
{
}


ULONG EndOfTrackEvent::WriteEvent(vector<BYTE> & buf, UINT time)
{
	return WriteMetaEvent(buf, time, 0x2F, NULL, 0);
}

//  *********
//  TextEvent
//  *********

TextEvent::TextEvent(MidiTrack* prntTrk, ULONG absoluteTime, const wchar_t* wstr)
: MidiEvent(prntTrk, absoluteTime, 0, PRIORITY_LOWEST), text(wstr)
{
}

ULONG TextEvent::WriteEvent(vector<BYTE> & buf, UINT time)
{
	return WriteMetaTextEvent(buf, time, 0x01, text);
}

//  ************
//  SeqNameEvent
//  ************

SeqNameEvent::SeqNameEvent(MidiTrack* prntTrk, ULONG absoluteTime, const wchar_t* wstr)
: MidiEvent(prntTrk, absoluteTime, 0, PRIORITY_LOWEST), text(wstr)
{
}

ULONG SeqNameEvent::WriteEvent(vector<BYTE> & buf, UINT time)
{
	return WriteMetaTextEvent(buf, time, 0x03, text);
}

//  **************
//  TrackNameEvent
//  **************

TrackNameEvent::TrackNameEvent(MidiTrack* prntTrk, ULONG absoluteTime, const wchar_t* wstr)
: MidiEvent(prntTrk, absoluteTime, 0, PRIORITY_LOWEST), text(wstr)
{
}

ULONG TrackNameEvent::WriteEvent(vector<BYTE> & buf, UINT time)
{
	return WriteMetaTextEvent(buf, time, 0x03, text);
}

//  ************
//  GMResetEvent
//  ************

GMResetEvent::GMResetEvent(MidiTrack *prntTrk, ULONG absoluteTime)
: MidiEvent(prntTrk, absoluteTime, 0, PRIORITY_HIGHEST)
{
}

ULONG GMResetEvent::WriteEvent(vector<BYTE> & buf, UINT time)
{
	BYTE data[5] = { 0x7E, 0x7F, 0x09, 0x01, 0xF7 };
	return WriteSysexEvent(buf, time, data, 5);
}

void MidiTrack::AddGMReset()
{
	aEvents.push_back(new GMResetEvent(this, GetDelta()));
}

void MidiTrack::InsertGMReset(ULONG absTime)
{
	aEvents.push_back(new GMResetEvent(this, absTime));
}

//***************
// SPECIAL EVENTS
//***************

//  **************
//  TransposeEvent
//  **************


GlobalTransposeEvent::GlobalTransposeEvent( MidiTrack* prntTrk, ULONG absoluteTime, char theSemitones )
	: MidiEvent(prntTrk, absoluteTime, 0, PRIORITY_HIGHEST)
{
	semitones = theSemitones;
}

ULONG GlobalTransposeEvent::WriteEvent( vector<BYTE> & buf, UINT time )
{
	this->prntTrk->parentSeq->globalTranspose = this->semitones;
	return time;
}