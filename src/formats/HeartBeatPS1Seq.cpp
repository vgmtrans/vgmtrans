// If you want to convert it without losing any events,
// check seqq2mid tool created by loveemu <https://code.google.com/p/loveemu/>

#include "stdafx.h"
#include "HeartBeatPS1Seq.h"
#include "HeartBeatPS1Format.h"

DECLARE_FORMAT(HeartBeatPS1)

HeartBeatPS1Seq::HeartBeatPS1Seq(RawFile* file, uint32_t offset)
: VGMSeqNoTrks(HeartBeatPS1Format::name, file, offset)
{
	//UseReverb();
	bWriteInitialTempo = true;
}

HeartBeatPS1Seq::~HeartBeatPS1Seq(void)
{
}


bool HeartBeatPS1Seq::GetHeaderInfo(void)
{
	name() = L"HeartBeatPS1Seq";

	if (offset() + 0x10 > rawfile->size())
	{
		return false;
	}

	SetPPQN(GetShortBE(offset()+8));
	//TryExpandMidiTracks(16);
	nNumTracks = 16;
	channel = 0;
	SetCurTrack(channel);
	AddTempo(offset()+10, 3, GetWordBE(offset()+0x0A) & 0xFFFFFF);
	uint8_t numer = GetByte(offset()+0x0D);
	uint8_t denom = GetByte(offset()+0x0E);
	if (numer == 0 || numer > 32)				//sanity check
		return false;
	AddTimeSig(offset()+0x0D, 2, numer, 1<<denom, (uint8_t)GetPPQN());

	//name().append(L" blah");

	SetEventsOffset(offset() + 0x10);

	return true;
}

void HeartBeatPS1Seq::ResetVars(void)
{
	VGMSeqNoTrks::ResetVars();
}

bool HeartBeatPS1Seq::ReadEvent(void)
{
	uint32_t beginOffset = curOffset;

	// in this format, end of track (FF 2F 00) comes without delta-time.
	// so handle that crazy sequence the first.
	if (curOffset + 3 <= rawfile->size())
	{
		if (GetByte(curOffset) == 0xff &&
			GetByte(curOffset + 1) == 0x2f &&
			GetByte(curOffset + 2) == 0x00)
		{
			curOffset += 3;
			AddEndOfTrack(beginOffset, curOffset-beginOffset);
			return false;
		}
	}

	uint32_t delta = ReadVarLen(curOffset);
	if (curOffset >= rawfile->size())
		return false;
	AddTime(delta);

	uint8_t status_byte = GetByte(curOffset++);

	if (status_byte <= 0x7F)			// Running Status
	{
		status_byte = runningStatus;
		curOffset--;
	}
	else
		runningStatus = status_byte;

	channel = status_byte&0x0F;
	SetCurTrack(channel);

	switch (status_byte & 0xF0)
	{
	case 0x80 :						//note off
		key = GetByte(curOffset++);
		vel = GetByte(curOffset++);
		AddNoteOff(beginOffset, curOffset-beginOffset, key);
		break;

	case 0x90 :						//note event
		key = GetByte(curOffset++);
		vel = GetByte(curOffset++);
		if (vel > 0)													//if the velocity is > 0, it's a note on
			AddNoteOn(beginOffset, curOffset-beginOffset, key, vel);
		else															//otherwise it's a note off
			AddNoteOff(beginOffset, curOffset-beginOffset, key);
		break;

	case 0xA0 :
		AddUnknown(beginOffset, curOffset-beginOffset);
		return false;

	case 0xB0 :
		{
			uint8_t controlNum = GetByte(curOffset++);
			uint8_t value = GetByte(curOffset++);
			switch (controlNum)		//control number
			{
			case 1:
				AddUnknown(beginOffset, curOffset-beginOffset, L"Modulation");
				break;

			case 2: // identical to CC#11?
				AddUnknown(beginOffset, curOffset-beginOffset, L"Breath Controller?");
				break;

			case 4:
				AddUnknown(beginOffset, curOffset-beginOffset, L"Foot Controller?");
				break;

			case 5:
				AddUnknown(beginOffset, curOffset-beginOffset, L"Portamento Time?");
				break;

			case 6 :
				AddGenericEvent(beginOffset, curOffset-beginOffset, L"NRPN Data Entry", NULL, CLR_UNKNOWN);
				break;

			case 7 :							//volume
				AddVol(beginOffset, curOffset-beginOffset, value);
				break;

			case 9:
				AddUnknown(beginOffset, curOffset-beginOffset, L"Control 9");
				break;

			case 10 :							//pan
				AddPan(beginOffset, curOffset-beginOffset, value);
				break;

			case 11 :							//expression
				AddExpression(beginOffset, curOffset-beginOffset, value);
				break;

			case 20:
				AddUnknown(beginOffset, curOffset-beginOffset, L"Control 20");
				break;

			case 21:
				AddUnknown(beginOffset, curOffset-beginOffset, L"Control 21");
				break;

			case 22:
				AddUnknown(beginOffset, curOffset-beginOffset, L"Control 22");
				break;

			case 23:
				AddUnknown(beginOffset, curOffset-beginOffset, L"Control 23");
				break;

			case 32:
				AddUnknown(beginOffset, curOffset-beginOffset, L"Bank LSB?");
				break;

			case 52:
				AddUnknown(beginOffset, curOffset-beginOffset, L"Control 52");
				break;

			case 53:
				AddUnknown(beginOffset, curOffset-beginOffset, L"Control 53");
				break;

			case 54:
				AddUnknown(beginOffset, curOffset-beginOffset, L"Control 54");
				break;

			case 55:
				AddUnknown(beginOffset, curOffset-beginOffset, L"Control 55");
				break;

			case 56:
				AddUnknown(beginOffset, curOffset-beginOffset, L"Control 56");
				break;

			case 64:
				AddUnknown(beginOffset, curOffset-beginOffset, L"Hold 1?");
				break;

			case 69:
				AddUnknown(beginOffset, curOffset-beginOffset, L"Hold 2?");
				break;

			case 71:
				AddUnknown(beginOffset, curOffset-beginOffset, L"Resonance?");
				break;

			case 72:
				AddUnknown(beginOffset, curOffset-beginOffset, L"Release Time?");
				break;

			case 73:
				AddUnknown(beginOffset, curOffset-beginOffset, L"Attack Time?");
				break;

			case 74:
				AddUnknown(beginOffset, curOffset-beginOffset, L"Cut Off Frequency?");
				break;

			case 75:
				AddUnknown(beginOffset, curOffset-beginOffset, L"Decay Time?");
				break;

			case 76:
				AddUnknown(beginOffset, curOffset-beginOffset, L"Vibrato Late?");
				break;

			case 77:
				AddUnknown(beginOffset, curOffset-beginOffset, L"Vibrato Depth?");
				break;

			case 78:
				AddUnknown(beginOffset, curOffset-beginOffset, L"Vibrato Delay?");
				break;

			case 79:
				AddUnknown(beginOffset, curOffset-beginOffset, L"Control 79");
				break;

			case 91:
				AddUnknown(beginOffset, curOffset-beginOffset, L"Reverb?");
				break;

			case 92:
				AddUnknown(beginOffset, curOffset-beginOffset, L"Tremolo Depth?");
				break;

			case 98:
				AddUnknown(beginOffset, curOffset-beginOffset, L"NRPN LSB?");
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

				default:
					AddUnknown(beginOffset, curOffset-beginOffset, L"NRPN MSB");
					break;
				}
				break;

			case 121:
				AddUnknown(beginOffset, curOffset-beginOffset, L"Reset All Controller?");
				break;

			case 126:
				AddUnknown(beginOffset, curOffset-beginOffset, L"MONO?");
				break;

			case 127:
				AddUnknown(beginOffset, curOffset-beginOffset, L"Poly?");
				break;

			default:
				AddUnknown(beginOffset, curOffset-beginOffset);
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

	case 0xD0 :
		AddUnknown(beginOffset, curOffset-beginOffset);
		return false;

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
				if (curOffset + 1 > rawfile->size())
					return false;

				uint8_t metaNum = GetByte(curOffset++);
				uint32_t metaLen = ReadVarLen(curOffset);
				if (curOffset + metaLen > rawfile->size())
					return false;

				switch (metaNum)
				{
				case 0x51 :
					AddTempo(beginOffset, curOffset+metaLen-beginOffset, (GetShortBE(curOffset) << 8) | GetByte(curOffset + 2));
					curOffset += metaLen;
					break;

				case 0x58 :
				{
					uint8_t numer = GetByte(curOffset);
					uint8_t denom = GetByte(curOffset + 1);
					AddTimeSig(beginOffset, curOffset+metaLen-beginOffset, numer, 1<<denom, (uint8_t)GetPPQN());
					curOffset += metaLen;
					break;
				}

				case 0x2F : // apparently not used, but just in case.
					AddEndOfTrack(beginOffset, curOffset+metaLen-beginOffset);
					curOffset += metaLen;
					return false;

				default :
					AddUnknown(beginOffset, curOffset+metaLen-beginOffset);
					curOffset += metaLen;
					break;
				}
			}
			else
			{
				AddUnknown(beginOffset, curOffset-beginOffset);
				return false;
			}
		}
		break;
	}
	return true;
}
