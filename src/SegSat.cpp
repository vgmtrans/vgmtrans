#include "stdafx.h"
#include "SegSat.h"

DECLARE_FORMAT(SegSat);

SegSatSeq::SegSatSeq(RawFile* file, ULONG offset)
: VGMSeqNoTrks(SegSatFormat::name, file, offset)
{
}

SegSatSeq::~SegSatSeq(void)
{
}

int SegSatSeq::GetHeaderInfo(void)
{
	//unLength = GetShort(dwOffset+8);
	SetPPQN(GetShortBE(offset()));
	SetEventsOffset(GetShortBE(offset()+4) + offset());
	//TryExpandMidiTracks(16);
	nNumTracks = 16;
	
	//nNumTracks = GetShort(offset()+2);
	//headerFlag = GetByte(offset()+3);
	//length() = GetShort(offset()+4);
	//if (nNumTracks == 0 || nNumTracks > 24)		//if there are no tracks or there are more tracks than allowed
	//	return FALSE;							//return an error, the sequence shall be deleted
	//ppqn = 0x30;


	//name.Format("MP2000 Seq %d", GetCount());
	name() = L"Sega Saturn Seq";

	return true;		//successful
}

int counter = 0;

int SegSatSeq::ReadEvent(void)
{
	if(bInLoop)
	{
		remainingEventsInLoop--;
		if (remainingEventsInLoop == -1)
		{
			bInLoop = false;
			curOffset = loopEndPos;
		}
	}
	
	ULONG beginOffset = curOffset;
	BYTE status_byte = GetByte(curOffset++);

	if (status_byte <= 0x7F)			// note on
	{
		//if (status_byte > 0xF)
		//{
			//char blah[200];
			//sprintf(blah, "whoa, status byte is > 0x0F at %X", curOffset);
			//Alert(blah);
		//}
		channel = status_byte&0x0F;
		SetCurTrack(channel);
		key = GetByte(curOffset++);
		vel = GetByte(curOffset++);
		dur = GetByte(curOffset++);
		AddDelta(GetByte(curOffset++));
		AddNoteByDur(beginOffset, curOffset-beginOffset, key, vel, dur);
	}
	else
	{
		if ((status_byte & 0xF0) == 0xB0)
		{
			curOffset+=3;
			AddUnknown(beginOffset, curOffset-beginOffset);
		}
		else if ((status_byte & 0xF0) == 0xC0)
		{
			channel = status_byte&0x0F;
			SetCurTrack(channel);
			BYTE progNum = GetByte(curOffset++);
			curOffset++;
			AddProgramChange(beginOffset, curOffset-beginOffset, progNum);
		}
		else if ((status_byte & 0xF0) == 0xE0)
		{
			curOffset += 2;
			AddUnknown(beginOffset, curOffset-beginOffset);
		}
		else if (status_byte == 0x81)		//loop x # of events
		{
			USHORT test1 = GetShortBE(curOffset);
			ULONG test2 = GetShortBE(curOffset);
			ULONG test3 = eventsOffset();
			BYTE test4 = GetByte(curOffset);
			ULONG loopOffset = eventsOffset() + GetShortBE(curOffset);
			curOffset += 2;
			remainingEventsInLoop = GetByte(curOffset++);
			loopEndPos = curOffset;
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Reference Event", CLR_LOOP);
			curOffset = loopOffset;
			bInLoop = true;
		}
		else if (status_byte == 0x82)
		{
			curOffset++;
			AddUnknown(beginOffset, curOffset-beginOffset);
		}
		else if (status_byte == 0x83)
		{
			AddEndOfTrack(beginOffset, curOffset-beginOffset);
			return false;
		}
	}
	::counter++;
	if (counter == 7000)
	{
		counter = 0;
		return false;
	}
	return true;
}
