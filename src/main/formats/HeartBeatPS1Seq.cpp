// If you want to convert it without losing any events,
// check seqq2mid tool created by loveemu <https://code.google.com/p/loveemu/>

#include "pch.h"
#include "HeartBeatPS1Seq.h"
#include "HeartBeatPS1Format.h"

DECLARE_FORMAT(HeartBeatPS1)

HeartBeatPS1Seq::HeartBeatPS1Seq(RawFile* file, uint32_t offset)
		: VGMSeqNoTrks(HeartBeatPS1Format::name, file, offset)
{
	UseReverb();
	//bWriteInitialTempo = false; // false, because the initial tempo is added by tempo event
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
	nNumTracks = 16;

	uint8_t numer = GetByte(offset()+0x0D);
	uint8_t denom = GetByte(offset()+0x0E);
	if (numer == 0 || numer > 32)				//sanity check
		return false;

	uint8_t trackCount = GetByte(offset()+0x0F);
	if (trackCount > 0 && trackCount <= 16)
	{
		nNumTracks = trackCount;
	}

	SetEventsOffset(offset() + 0x10);

	return true;
}

void HeartBeatPS1Seq::ResetVars(void)
{
	VGMSeqNoTrks::ResetVars();

	uint32_t initialTempo = (GetShortBE(offset()+10) << 8) | GetByte(offset()+10+2);
	AddTempo(offset()+10, 3, initialTempo);

	uint8_t numer = GetByte(offset()+0x0D);
	uint8_t denom = GetByte(offset()+0x0E);
	AddTimeSig(offset()+0x0D, 2, numer, 1<<denom, (uint8_t)GetPPQN());
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
				AddGenericEvent(beginOffset, curOffset-beginOffset, L"Modulation", L"", CLR_UNKNOWN);
				if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
					pMidiTrack->AddControllerEvent(channel, controlNum, value);
				}
				break;

			case 2: // identical to CC#11?
				AddGenericEvent(beginOffset, curOffset-beginOffset, L"Breath Controller?", L"", CLR_UNKNOWN);
				if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
					pMidiTrack->AddControllerEvent(channel, controlNum, value);
				}
				break;

			case 4:
				AddGenericEvent(beginOffset, curOffset-beginOffset, L"Foot Controller?", L"", CLR_UNKNOWN);
				if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
					pMidiTrack->AddControllerEvent(channel, controlNum, value);
				}
				break;

			case 5:
				AddGenericEvent(beginOffset, curOffset-beginOffset, L"Portamento Time?", L"", CLR_UNKNOWN);
				if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
					pMidiTrack->AddControllerEvent(channel, controlNum, value);
				}
				break;

			case 6 :
				AddGenericEvent(beginOffset, curOffset-beginOffset, L"NRPN Data Entry", L"", CLR_MISC);
				if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
					pMidiTrack->AddControllerEvent(channel, controlNum, value);
				}
				break;

			case 7 :							//volume
				AddVol(beginOffset, curOffset-beginOffset, value);
				break;

			case 9:
				AddGenericEvent(beginOffset, curOffset-beginOffset, L"Control 9", L"", CLR_UNKNOWN);
				if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
					pMidiTrack->AddControllerEvent(channel, controlNum, value);
				}
				break;

			case 10 :							//pan
				AddPan(beginOffset, curOffset-beginOffset, value);
				break;

			case 11 :							//expression
				AddExpression(beginOffset, curOffset-beginOffset, value);
				break;

			case 20:
				AddGenericEvent(beginOffset, curOffset-beginOffset, L"Control 20", L"", CLR_UNKNOWN);
				if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
					pMidiTrack->AddControllerEvent(channel, controlNum, value);
				}
				break;

			case 21:
				AddGenericEvent(beginOffset, curOffset-beginOffset, L"Control 21", L"", CLR_UNKNOWN);
				if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
					pMidiTrack->AddControllerEvent(channel, controlNum, value);
				}
				break;

			case 22:
				AddGenericEvent(beginOffset, curOffset-beginOffset, L"Control 22", L"", CLR_UNKNOWN);
				if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
					pMidiTrack->AddControllerEvent(channel, controlNum, value);
				}
				break;

			case 23:
				AddGenericEvent(beginOffset, curOffset-beginOffset, L"Control 23", L"", CLR_UNKNOWN);
				if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
					pMidiTrack->AddControllerEvent(channel, controlNum, value);
				}
				break;

			case 32:
				AddGenericEvent(beginOffset, curOffset-beginOffset, L"Bank LSB?", L"", CLR_UNKNOWN);
				if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
					pMidiTrack->AddControllerEvent(channel, controlNum, value);
				}
				break;

			case 52:
				AddGenericEvent(beginOffset, curOffset-beginOffset, L"Control 52", L"", CLR_UNKNOWN);
				if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
					pMidiTrack->AddControllerEvent(channel, controlNum, value);
				}
				break;

			case 53:
				AddGenericEvent(beginOffset, curOffset-beginOffset, L"Control 53", L"", CLR_UNKNOWN);
				if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
					pMidiTrack->AddControllerEvent(channel, controlNum, value);
				}
				break;

			case 54:
				AddGenericEvent(beginOffset, curOffset-beginOffset, L"Control 54", L"", CLR_UNKNOWN);
				if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
					pMidiTrack->AddControllerEvent(channel, controlNum, value);
				}
				break;

			case 55:
				AddGenericEvent(beginOffset, curOffset-beginOffset, L"Control 55", L"", CLR_UNKNOWN);
				if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
					pMidiTrack->AddControllerEvent(channel, controlNum, value);
				}
				break;

			case 56:
				AddGenericEvent(beginOffset, curOffset-beginOffset, L"Control 56", L"", CLR_UNKNOWN);
				if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
					pMidiTrack->AddControllerEvent(channel, controlNum, value);
				}
				break;

			case 64:
				AddGenericEvent(beginOffset, curOffset-beginOffset, L"Hold 1?", L"", CLR_UNKNOWN);
				if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
					pMidiTrack->AddControllerEvent(channel, controlNum, value);
				}
				break;

			case 69:
				AddGenericEvent(beginOffset, curOffset-beginOffset, L"Hold 2?", L"", CLR_UNKNOWN);
				if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
					pMidiTrack->AddControllerEvent(channel, controlNum, value);
				}
				break;

			case 71:
				AddGenericEvent(beginOffset, curOffset-beginOffset, L"Resonance?", L"", CLR_UNKNOWN);
				if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
					pMidiTrack->AddControllerEvent(channel, controlNum, value);
				}
				break;

			case 72:
				AddGenericEvent(beginOffset, curOffset-beginOffset, L"Release Time?", L"", CLR_UNKNOWN);
				if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
					pMidiTrack->AddControllerEvent(channel, controlNum, value);
				}
				break;

			case 73:
				AddGenericEvent(beginOffset, curOffset-beginOffset, L"Attack Time?", L"", CLR_UNKNOWN);
				if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
					pMidiTrack->AddControllerEvent(channel, controlNum, value);
				}
				break;

			case 74:
				AddGenericEvent(beginOffset, curOffset-beginOffset, L"Cut Off Frequency?", L"", CLR_UNKNOWN);
				if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
					pMidiTrack->AddControllerEvent(channel, controlNum, value);
				}
				break;

			case 75:
				AddGenericEvent(beginOffset, curOffset-beginOffset, L"Decay Time?", L"", CLR_UNKNOWN);
				if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
					pMidiTrack->AddControllerEvent(channel, controlNum, value);
				}
				break;

			case 76:
				AddGenericEvent(beginOffset, curOffset-beginOffset, L"Vibrato Rate?", L"", CLR_UNKNOWN);
				if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
					pMidiTrack->AddControllerEvent(channel, controlNum, value);
				}
				break;

			case 77:
				AddGenericEvent(beginOffset, curOffset-beginOffset, L"Vibrato Depth?", L"", CLR_UNKNOWN);
				if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
					pMidiTrack->AddControllerEvent(channel, controlNum, value);
				}
				break;

			case 78:
				AddGenericEvent(beginOffset, curOffset-beginOffset, L"Vibrato Delay?", L"", CLR_UNKNOWN);
				if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
					pMidiTrack->AddControllerEvent(channel, controlNum, value);
				}
				break;

			case 79:
				AddGenericEvent(beginOffset, curOffset-beginOffset, L"Control 79", L"", CLR_UNKNOWN);
				if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
					pMidiTrack->AddControllerEvent(channel, controlNum, value);
				}
				break;

			case 91:
				AddReverb(beginOffset, curOffset-beginOffset, value);
				break;

			case 92:
				AddGenericEvent(beginOffset, curOffset-beginOffset, L"Tremolo Depth?", L"", CLR_UNKNOWN);
				if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
					pMidiTrack->AddControllerEvent(channel, controlNum, value);
				}
				break;

			case 98:
				switch (value)
				{
				case 20 :
					AddGenericEvent(beginOffset, curOffset-beginOffset, L"NRPN 1 #20", L"", CLR_MISC);
					break;

				case 30 :
					AddGenericEvent(beginOffset, curOffset-beginOffset, L"NRPN 1 #30", L"", CLR_MISC);
					break;

				default:
					AddGenericEvent(beginOffset, curOffset-beginOffset, L"NRPN 1", L"", CLR_MISC);
					break;
				}

				if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
					pMidiTrack->AddControllerEvent(channel, controlNum, value);
				}
				break;

			case 99 :							//(0x63) nrpn msb
				switch (value)
				{
				case 20 :
					AddGenericEvent(beginOffset, curOffset-beginOffset, L"Loop Start", L"", CLR_LOOP);
					break;

				case 30 :
					AddGenericEvent(beginOffset, curOffset-beginOffset, L"Loop End", L"", CLR_LOOP);
					break;

				default:
					AddGenericEvent(beginOffset, curOffset-beginOffset, L"NRPN 2", L"", CLR_MISC);
					break;
				}

				if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
					pMidiTrack->AddControllerEvent(channel, controlNum, value);
				}
				break;

			case 121:
				AddGenericEvent(beginOffset, curOffset-beginOffset, L"Reset All Controllers", L"", CLR_MISC);
				if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
					pMidiTrack->AddControllerEvent(channel, controlNum, value);
				}
				break;

			case 126:
				AddGenericEvent(beginOffset, curOffset-beginOffset, L"MONO?", L"", CLR_UNKNOWN);
				if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
					pMidiTrack->AddControllerEvent(channel, controlNum, value);
				}
				break;

			case 127:
				AddGenericEvent(beginOffset, curOffset-beginOffset, L"Poly?", L"", CLR_UNKNOWN);
				if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
					pMidiTrack->AddControllerEvent(channel, controlNum, value);
				}
				break;

			default:
				AddGenericEvent(beginOffset, curOffset-beginOffset, L"Control Event", L"", CLR_UNKNOWN);
				if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
					pMidiTrack->AddControllerEvent(channel, controlNum, value);
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