#include "stdafx.h"
#include "PS1Seq.h"
#include "PS1Format.h"

DECLARE_FORMAT(PS1)

PS1Seq::PS1Seq(RawFile* file, uint32_t offset)
: VGMSeqNoTrks(PS1Format::name, file, offset)
{
	//UseReverb();
	//bWriteInitialTempo = false; // false, because the initial tempo is added by tempo event
}

PS1Seq::~PS1Seq(void)
{
}


bool PS1Seq::GetHeaderInfo(void)
{
	name() = L"PS1 SEQ";

	SetPPQN(GetShortBE(offset()+8));
	nNumTracks = 16;

	uint8_t numer = GetByte(offset()+0x0D);
	uint8_t denom = GetByte(offset()+0x0E);
	if (numer == 0 || numer > 32)				//sanity check
		return false;

	if (GetByte(offset()+0xF) == 0 && GetByte(offset()+0x10) == 0)
	{
		SetEventsOffset(offset() + 0x0F + 4);
		PS1Seq* newPS1Seq = new PS1Seq(rawfile, offset()+GetShortBE(offset()+0x11)+0x13 - 6);
		if (!newPS1Seq->LoadVGMFile())
			delete newPS1Seq;
		//short relOffset = (short)GetShortBE(curOffset);
		//AddGenericEvent(beginOffset, 4, L"Jump Relative", NULL, BG_CLR_PINK);
		//curOffset += relOffset;
	}
	else
		SetEventsOffset(offset() + 0x0F);
	
	return true;
}

void PS1Seq::ResetVars(void)
{
	VGMSeqNoTrks::ResetVars();

	uint32_t initialTempo = (GetShortBE(offset()+10) << 8) | GetByte(offset()+12);
	AddTempo(offset()+10, 3, initialTempo);

	uint8_t numer = GetByte(offset()+0x0D);
	uint8_t denom = GetByte(offset()+0x0E);
	AddTimeSig(offset()+0x0D, 2, numer, 1<<denom, (uint8_t)GetPPQN());
}

bool PS1Seq::ReadEvent(void)
{
	uint32_t beginOffset = curOffset;
	uint32_t delta = ReadVarLen(curOffset);
	if (curOffset >= rawfile->size())
		return false;
	AddTime(delta);

	uint8_t status_byte = GetByte(curOffset++);

	//if (status_byte == 0)				//Jump Relative
	//{
	//	short relOffset = (short)GetShortBE(curOffset);
	//	AddGenericEvent(beginOffset, 4, L"Jump Relative", NULL, BG_CLR_PINK);
	//	curOffset += relOffset;

	//	curOffset += 4;		//skip the first 4 bytes (no idea)
	//	SetPPQN(GetShortBE(curOffset));
	//	curOffset += 2;
	//	AddTempo(curOffset, 3, GetWordBE(curOffset-1) & 0xFFFFFF);
	//	curOffset += 3;
	//	uint8_t numer = GetByte(curOffset++);
	//	uint8_t denom = GetByte(curOffset++);
	//	if (numer == 0 || numer > 32)				//sanity check
	//		return false;
	//	AddTimeSig(curOffset-2, 2, numer, 1<<denom, GetPPQN());
	//	SetEventsOffset(offset() + 0x0F);
	//}
//	else
		if (status_byte <= 0x7F)			// Running Status
	{
		if (status_byte == 0)			// some games were ripped to PSF with the EndTrack event missing, so
		{
			if (GetWord(curOffset) == 0)	//if we read a sequence of four 0 bytes, then just treat that
				return false;				//as the end of the track
		}
		status_byte = runningStatus;
		curOffset--;
	}
	else
		runningStatus = status_byte;


	channel = status_byte&0x0F;
	SetCurTrack(channel);

	switch (status_byte & 0xF0)
	{
	case 0x90 :						//note event
		key = GetByte(curOffset++);
		vel = GetByte(curOffset++);
		if (vel > 0)													//if the velocity is > 0, it's a note on
			AddNoteOn(beginOffset, curOffset-beginOffset, key, vel);
		else															//otherwise it's a note off
			AddNoteOff(beginOffset, curOffset-beginOffset, key);
		break;

	case 0xB0 :
		{
			uint8_t controlNum = GetByte(curOffset++);
			uint8_t value = GetByte(curOffset++);
			switch (controlNum)		//control number
			{
			case 6 :
				AddGenericEvent(beginOffset, curOffset-beginOffset, L"NRPN Data Entry", NULL, CLR_UNKNOWN);
				break;

			case 7 :							//volume
				AddVol(beginOffset, curOffset-beginOffset, value);
				break;

			case 10 :							//pan
				AddPan(beginOffset, curOffset-beginOffset, value);
				break;

			case 11 :							//expression
				AddExpression(beginOffset, curOffset-beginOffset, value);
				break;

			case 99 :							//(0x63) nrpn msb
				switch (value)
				{
				case 20 :
					AddGenericEvent(beginOffset, curOffset-beginOffset, L"Loop Start", NULL, CLR_LOOP);
					break;

				case 30 :
					AddGenericEvent(beginOffset, curOffset-beginOffset, L"Loop End", NULL, CLR_LOOP);
					break;
				}
				break;
			}
		}
		break;

	case 0xC0 :
		{
			uint8_t progNum = GetByte(curOffset++);
			AddProgramChange(beginOffset, curOffset-beginOffset, progNum);
		}
		break;

	case 0xE0 :
		{
			uint8_t hi = GetByte(curOffset++);
			uint8_t lo = GetByte(curOffset++);
			AddPitchBendMidiFormat(beginOffset, curOffset-beginOffset, hi, lo);
		}
		break;

	case 0xF0 : 
		{
			if (status_byte == 0xFF)
			{
				switch (GetByte(curOffset++))
				{
				case 0x51 :			//tempo.  This is different from SMF, where we'd expect a 51 then 03.  Also, supports
									//a string of tempo events
					AddTempo(beginOffset, curOffset+3-beginOffset, (GetShortBE(curOffset) << 8) | GetByte(curOffset + 2));
					curOffset += 3;
					break;
				
				case 0x2F :
					AddEndOfTrack(beginOffset, curOffset-beginOffset);
					return false;

				default :
					AddUnknown(beginOffset, curOffset-beginOffset, L"Unknown SysEx Event");
					return false;
				}
			}
			else
			{
				AddUnknown(beginOffset, curOffset-beginOffset);
				return false;
			}
		}
		break;

	default:
		AddUnknown(beginOffset, curOffset-beginOffset);
		return false;
	}
	return true;
}
