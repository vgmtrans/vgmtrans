#ifdef _WIN32
	#include "stdafx.h"
#endif
#include "OrgSeq.h"

DECLARE_FORMAT(Org);

OrgSeq::OrgSeq(RawFile* file, uint32_t offset)
: VGMSeq(OrgFormat::name, file, offset)
{
}

OrgSeq::~OrgSeq(void)
{
}

bool OrgSeq::GetHeaderInfo(void)
{
	waitTime = GetShort(dwOffset+6);
	beatsPerMeasure = GetByte(dwOffset+8);
	SetPPQN(GetByte(dwOffset+9));
	name = L"Org Seq";

	uint32_t notesSoFar = 0;		//this must be used to determine the length of the entire seq

	for (int i=0; i<16; i++)
	{
		if (GetShort(dwOffset+0x16 + i*6))			//if there are notes in this track
		{
			nNumTracks++;					//well then, we might as well say it exists
			OrgTrack* newOrgTrack = new OrgTrack(this, dwOffset+0x12 + 16*6 + notesSoFar*8, GetShort(0x16 + i*6)*8, i);
			newOrgTrack->numNotes = GetShort(dwOffset+0x16 + i*6);
			newOrgTrack->freq = GetShort(dwOffset+0x12 + i*6);
			newOrgTrack->waveNum = GetByte(dwOffset+0x14 + i*6);
			newOrgTrack->numNotes = GetShort(dwOffset+0x16 + i*6);
			aTracks.push_back(newOrgTrack);	

			notesSoFar += newOrgTrack->numNotes;
		}
	}
	unLength = 0x12 + 6*16 + notesSoFar*8;

	return true;		//successful
}

OrgTrack::OrgTrack(OrgSeq* parentFile, long offset, long length, uint8_t realTrk)
: SeqTrack(parentFile, offset, length), realTrkNum(realTrk)
{
}

bool OrgTrack::LoadTrack(uint32_t trackNum, uint32_t stopOffset, long stopDelta)
{
	pMidiTrack = parentSeq->midi->AddTrack();
	//SetChannelAndGroupFromTrkNum(trackNum);
	
	if (realTrkNum > 7)
		channel = 9;		//all tracks above 8 are drum tracks (channel 10)
	else
		channel = trackNum;
	channelGroup = 0;
	pMidiTrack->SetChannelGroup(channelGroup);

	if (trackNum == 0)
	{
		AddTempo(0, 0, ((OrgSeq*)parentSeq)->waitTime*4000);
		AddTimeSig(0, 0, 4, 4, (uint8_t)parentSeq->GetPPQN());
	}
	if (channel == 10)
		AddProgramChange(0, 0, waveNum);

	bInLoop = false;
	curOffset = dwOffset;	//start at beginning of track
	curNote = 0;
	for (uint16_t i=0; i<numNotes; i++)
		ReadEvent();

	return true;
}

bool OrgTrack::ReadEvent(void)
{
	uint8_t key = GetByte(curOffset + (numNotes-curNote)*4 + curNote);
	uint8_t vel = GetByte(curOffset + (numNotes-curNote)*4 + numNotes*2 + curNote)/2;
	uint8_t dur = GetByte(curOffset + (numNotes-curNote)*4 + numNotes + curNote);
	if (key == 0xFF)
		key = prevKey;
	uint32_t absTime = GetWord(curOffset);

	InsertNoteByDur(curOffset, 4, key, vel, dur, absTime);
	uint8_t pan = GetByte(curOffset + (numNotes-curNote)*4 + numNotes*3 + curNote);
	if (pan == 0xFF)
		pan = prevPan;
	else
		pan = (uint8_t)(GetByte(curOffset + (numNotes-curNote)*4 + numNotes*3 + curNote) * 10.66666666666666666666666666666);
	//if (newPan > 0x7F)	//sometimes the value is 0xFF, even though the range would seem to be 0-C according to the org editor
	//	newPan = 64;	//in this case, set it to the center position, i can't distinguish it from center on hearing tests
	if (pan != prevPan)
	{
		InsertPan(curOffset + (numNotes-curNote)*4 + numNotes*3 + curNote, 1, pan, absTime);
		prevPan = pan;
	}
	
	curOffset +=4;
	curNote++;

	return true;
}
