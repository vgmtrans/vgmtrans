#include "stdafx.h"
#include "KonamiGXSeq.h"
#include "KonamiGXFormat.h"

DECLARE_FORMAT(KonamiGX);


// ***********
// KonamiGXSeq
// ***********

KonamiGXSeq::KonamiGXSeq(RawFile* file, ULONG offset)
: VGMSeq(KonamiGXFormat::name, file, offset)
{
	UseReverb();
	AlwaysWriteInitialVol(127);
}

KonamiGXSeq::~KonamiGXSeq(void)
{
}

int KonamiGXSeq::GetHeaderInfo(void)
{
	//nNumTracks = GetByte(dwOffset+8);
	SetPPQN(0x30);

	wostringstream	theName;
	theName << L"Konami GX Seq";
	name = theName.str();
	return true;
}

int KonamiGXSeq::GetTrackPointers(void)
{
	UINT pos = dwOffset;
	for(int i=0; i<17; i++)
	{
		UINT trackOffset = GetWordBE(pos);
		if (GetByte(trackOffset) == 0xFF)	// skip empty tracks. don't even bother making them tracks
			continue;
		nNumTracks++;
		aTracks.push_back(new KonamiGXTrack(this, trackOffset, 0));
		pos += 4;
	}
	return true;
}

//int KonamiGXSeq::LoadTracks(void)
//{
//	for (UINT i=0; i<nNumTracks; i++)
//	{
//		if (!aTracks[i]->LoadTrack(i, 0xFFFFFFFF))
//			return false;
//	}
//	aTracks[0]->InsertTimeSig(0, 0, 4, 4, GetPPQN(), 0);
//}

// *************
// KonamiGXTrack
// *************


KonamiGXTrack::KonamiGXTrack(KonamiGXSeq* parentSeq, long offset, long length)
: SeqTrack(parentSeq, offset, length), bInJump(false)
{
}


// I'm going to try to follow closely to the original Salamander 2 code at 0x30C6
int KonamiGXTrack::ReadEvent(void)
{
	ULONG beginOffset = curOffset;
	ULONG deltatest = GetDelta();
	//AddDelta(ReadVarLen(curOffset));

	BYTE status_byte = GetByte(curOffset++);

	if (status_byte == 0xFF)
	{
		if (bInJump)
		{
			bInJump = false;
			curOffset = jump_return_offset;
		}
		else
		{
			AddEndOfTrack(beginOffset, curOffset-beginOffset);
			return 0;
		}
	}
	else if (status_byte == 0x60)
	{
		return 1;
	}
	else if (status_byte == 0x61)
	{
		return 1;
	}
	else if (status_byte < 0xC0)		//note event
	{
		BYTE note, delta;
		if (status_byte < 0x62)
		{
			delta = GetByte(curOffset++);
			note = status_byte;
			prevDelta = delta;
		}
		else
		{
			delta = prevDelta;
			note = status_byte - 0x62;
		}
		
		BYTE nextDataByte = GetByte(curOffset++);
		BYTE dur, vel;
		if (nextDataByte < 0x80)
		{
			dur = nextDataByte;
			prevDur = dur;
			vel = GetByte(curOffset++);
		}
		else
		{
			dur = prevDur;
			vel = nextDataByte - 0x80;
		}
		
		//AddNoteOn(beginOffset, curOffset-beginOffset, note, vel);
		//AddDelta(dur);
		//AddNoteOffNoItem(note);
		UINT newdur = (delta*dur)/0x64;
		if (newdur == 0)
			newdur = 1;
		AddNoteByDur(beginOffset, curOffset-beginOffset, note, vel, newdur);
		AddDelta(delta);
		if (newdur > delta)
			ATLTRACE("newdur > delta.  %X > %X.  occurring at %X\n", newdur, delta, beginOffset);
		//AddDelta(dur);
	}
	else switch (status_byte)
	{
	case 0xC0:
		curOffset++;
		AddUnknown(beginOffset, curOffset-beginOffset);
		break;
	case 0xCE:
		curOffset+=2;
		AddUnknown(beginOffset, curOffset-beginOffset);
		break;
	case 0xD0:
		curOffset+=3;
		AddUnknown(beginOffset, curOffset-beginOffset);
		break;
	case 0xD1:
	case 0xD2:
	case 0xD3:
	case 0xD4:
	case 0xD5:
	case 0xD6:
		curOffset+=2;
		AddUnknown(beginOffset, curOffset-beginOffset);
		break;
	case 0xDE:
		curOffset++;
		AddUnknown(beginOffset, curOffset-beginOffset);
		break;
	case 0xE0:		//Rest
		{
			BYTE delta = GetByte(curOffset++);
			AddDelta(delta);
		}
		break;
	case 0xE1:		//Hold
		{
			BYTE delta = GetByte(curOffset++);
			BYTE dur = GetByte(curOffset++);
			UINT newdur = (delta*dur)/0x64;
			AddDelta(newdur);
			this->MakePrevDurNoteEnd();
			AddDelta(delta-newdur);
		}
		break;
	case 0xE2:			//program change
		{
			BYTE progNum = GetByte(curOffset++);
			AddProgramChange(beginOffset, curOffset-beginOffset, progNum);
		}
		break;
	case 0xE3:			//stereo related
		curOffset++;
		AddUnknown(beginOffset, curOffset-beginOffset);
		break;
	case 0xE4:
	case 0xE5:
		curOffset+=3;
		AddUnknown(beginOffset, curOffset-beginOffset);
		break;
	case 0xE6:
		AddUnknown(beginOffset, curOffset-beginOffset);
		break;
	case 0xE7:
		curOffset+=3;
		AddUnknown(beginOffset, curOffset-beginOffset);
		break;
	case 0xE8:
		AddUnknown(beginOffset, curOffset-beginOffset);
		break;
	case 0xE9:
		curOffset+=3;
		AddUnknown(beginOffset, curOffset-beginOffset);
		break;
	case 0xEA:			//tempo
		{
			BYTE bpm = GetByte(curOffset++);
			AddTempoBPM(beginOffset, curOffset-beginOffset, bpm);
		}
		break;
	case 0xEC:
		curOffset++;
		AddUnknown(beginOffset, curOffset-beginOffset);
		break;
	case 0xEE:			//master vol
		{
			BYTE vol = GetByte(curOffset++);
			//AddMasterVol(beginOffset, curOffset-beginOffset, vol);
		}
		break;
	case 0xEF:
		curOffset+=2;
		AddUnknown(beginOffset, curOffset-beginOffset);
		break;
	case 0xF0:
		curOffset++;
		AddUnknown(beginOffset, curOffset-beginOffset);
		break;
	case 0xF1:
		curOffset+=3;
		AddUnknown(beginOffset, curOffset-beginOffset);
		break;
	case 0xF2:
		curOffset++;
		AddUnknown(beginOffset, curOffset-beginOffset);
		break;
	case 0xF3:
		curOffset+=3;
		AddUnknown(beginOffset, curOffset-beginOffset);
		break;
	case 0xF6:
	case 0xF7:
		AddUnknown(beginOffset, curOffset-beginOffset);
		break;
	case 0xF8:
		curOffset+=2;
		AddUnknown(beginOffset, curOffset-beginOffset);
		break;
	case 0xFA:			//release rate
		curOffset++;
		AddUnknown(beginOffset, curOffset-beginOffset);
		break;
	case 0xFD:
		curOffset += 4;
		AddGenericEvent(beginOffset, curOffset-beginOffset, L"Loop", CLR_LOOP);
		break;
	case 0xFE:
		bInJump = true;
		jump_return_offset = curOffset+4;
		AddGenericEvent(beginOffset, jump_return_offset-beginOffset, L"Jump", CLR_LOOP);
		curOffset = GetWordBE(curOffset);
		//if (curOffset > 0x100000)
		//	return 0;
		break;
	default:
		AddEndOfTrack(beginOffset, curOffset-beginOffset);
		return false;
	}
	return true;
}