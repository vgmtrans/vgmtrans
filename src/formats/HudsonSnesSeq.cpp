#include "stdafx.h"
#include "HudsonSnesSeq.h"
#include "HudsonSnesFormat.h"
#include "ScaleConversion.h"

DECLARE_FORMAT(HudsonSnes);

//  *************
//  HudsonSnesSeq
//  *************
#define MAX_TRACKS  8
#define SEQ_PPQN    48

HudsonSnesSeq::HudsonSnesSeq(RawFile* file, HudsonSnesVersion ver, uint32_t seqdataOffset, std::wstring newName)
	: VGMSeq(HudsonSnesFormat::name, file, seqdataOffset, 0, newName),
	version(ver),
	TrackAvailableBits(0)
{
	bLoadTickByTick = true;
	bAllowDiscontinuousTrackData = true;

	UseReverb();
	AlwaysWriteInitialReverb(0);

	LoadEventMap(this);
}

HudsonSnesSeq::~HudsonSnesSeq(void)
{
}

void HudsonSnesSeq::ResetVars(void)
{
	VGMSeq::ResetVars();
}

bool HudsonSnesSeq::GetHeaderInfo(void)
{
	SetPPQN(SEQ_PPQN);

	VGMHeader* header = AddHeader(dwOffset, 0);
	uint32_t curOffset = dwOffset;

	// track addresses
	if (version == HUDSONSNES_V0 || version == HUDSONSNES_V1) {
		VGMHeader* trackPtrHeader = header->AddHeader(curOffset, 0, L"Track Pointers");
		if (!GetTrackPointersInHeaderInfo(trackPtrHeader, curOffset)) {
			return false;
		}
		trackPtrHeader->SetGuessedLength();
	}

	while (true) {
		if (curOffset + 1 > 0x10000) {
			return false;
		}

		uint32_t beginOffset = curOffset;
		uint8_t statusByte = GetByte(curOffset++);

		HudsonSnesSeqHeaderEventType eventType = (HudsonSnesSeqHeaderEventType)0;
		std::map<uint8_t, HudsonSnesSeqHeaderEventType>::iterator pEventType = HeaderEventMap.find(statusByte);
		if (pEventType != HeaderEventMap.end()) {
			eventType = pEventType->second;
		}

		switch (eventType)
		{
		case HEADER_EVENT_END:
		{
			header->AddSimpleItem(beginOffset, curOffset - beginOffset, L"Header End");
			goto header_end;
		}

		case HEADER_EVENT_TIMEBASE:
		{
			VGMHeader* aHeader = header->AddHeader(beginOffset, 1, L"Timebase");
			aHeader->AddSimpleItem(beginOffset, 1, L"Event ID");

			aHeader->AddSimpleItem(curOffset, 1, L"Timebase");
			uint8_t timebaseShift = GetByte(curOffset++) & 3;

			aHeader->unLength = curOffset - beginOffset;
			break;
		}

		case HEADER_EVENT_TRACKS:
		{
			VGMHeader* trackPtrHeader = header->AddHeader(beginOffset, 0, L"Track Pointers");
			trackPtrHeader->AddSimpleItem(beginOffset, 1, L"Event ID");
			if (!GetTrackPointersInHeaderInfo(trackPtrHeader, curOffset)) {
				return false;
			}
			trackPtrHeader->unLength = curOffset - beginOffset;
			break;
		}

		case HEADER_EVENT_INSTRUMENTS_V1:
		{
			VGMHeader* instrHeader = header->AddHeader(beginOffset, 1, L"Instruments");
			instrHeader->AddSimpleItem(beginOffset, 1, L"Event ID");

			instrHeader->AddSimpleItem(curOffset, 1, L"Size");
			uint8_t tableSize = GetByte(curOffset++);
			if (curOffset + tableSize > 0x10000) {
				return false;
			}

			instrHeader->AddUnknownItem(curOffset, tableSize);
			curOffset += tableSize;

			instrHeader->unLength = curOffset - beginOffset;
			break;
		}

		case HEADER_EVENT_PERCUSSIONS_V1:
		{
			VGMHeader* percHeader = header->AddHeader(beginOffset, 1, L"Percussions");
			percHeader->AddSimpleItem(beginOffset, 1, L"Event ID");

			percHeader->AddSimpleItem(curOffset, 1, L"Size");
			uint8_t tableSize = GetByte(curOffset++);
			if (curOffset + tableSize > 0x10000) {
				return false;
			}

			percHeader->AddUnknownItem(curOffset, tableSize);
			curOffset += tableSize;

			percHeader->unLength = curOffset - beginOffset;
			break;
		}

		case HEADER_EVENT_INSTRUMENTS_V2:
		{
			VGMHeader* instrHeader = header->AddHeader(beginOffset, 1, L"Instruments");
			instrHeader->AddSimpleItem(beginOffset, 1, L"Event ID");

			instrHeader->AddSimpleItem(curOffset, 1, L"Number of Items");
			uint8_t tableSize = GetByte(curOffset++) * 4;
			if (curOffset + tableSize > 0x10000) {
				return false;
			}

			instrHeader->AddUnknownItem(curOffset, tableSize);
			curOffset += tableSize;

			instrHeader->unLength = curOffset - beginOffset;
			break;
		}

		case HEADER_EVENT_PERCUSSIONS_V2:
		{
			VGMHeader* percHeader = header->AddHeader(beginOffset, 1, L"Percussions");
			percHeader->AddSimpleItem(beginOffset, 1, L"Event ID");

			percHeader->AddSimpleItem(curOffset, 1, L"Number of Items");
			uint8_t tableSize = GetByte(curOffset++) * 4;
			if (curOffset + tableSize > 0x10000) {
				return false;
			}

			percHeader->AddUnknownItem(curOffset, tableSize);
			curOffset += tableSize;

			percHeader->unLength = curOffset - beginOffset;
			break;
		}

		case HEADER_EVENT_05:
		{
			VGMHeader* aHeader = header->AddHeader(beginOffset, 1, L"Unknown");
			aHeader->AddSimpleItem(beginOffset, 1, L"Event ID");

			aHeader->AddSimpleItem(curOffset, 1, L"Number of Items");
			uint8_t tableSize = GetByte(curOffset++) * 2;
			if (curOffset + tableSize > 0x10000) {
				return false;
			}

			aHeader->AddUnknownItem(curOffset, tableSize);
			curOffset += tableSize;

			aHeader->unLength = curOffset - beginOffset;
			break;
		}

		case HEADER_EVENT_06:
		{
			VGMHeader* aHeader = header->AddHeader(beginOffset, 1, L"Unknown");
			aHeader->AddSimpleItem(beginOffset, 1, L"Event ID");

			aHeader->AddSimpleItem(curOffset, 1, L"Number of Items");
			uint8_t tableSize = GetByte(curOffset++) * 2;
			if (curOffset + tableSize > 0x10000) {
				return false;
			}

			aHeader->AddUnknownItem(curOffset, tableSize);
			curOffset += tableSize;

			aHeader->unLength = curOffset - beginOffset;
			break;
		}

		case HEADER_EVENT_ECHO_PARAM:
		{
			VGMHeader* aHeader = header->AddHeader(beginOffset, 1, L"Echo Param");
			aHeader->AddSimpleItem(beginOffset, 1, L"Event ID");

			aHeader->AddUnknownItem(curOffset, 1);
			uint8_t arg1 = GetByte(curOffset++);

			if (arg1 == 0) {
				aHeader->AddUnknownItem(curOffset, 6);
				curOffset += 6;
			}

			aHeader->unLength = curOffset - beginOffset;
			break;
		}

		case HEADER_EVENT_08:
		{
			VGMHeader* aHeader = header->AddHeader(beginOffset, 1, L"Unknown");
			aHeader->AddSimpleItem(beginOffset, 1, L"Event ID");

			aHeader->AddUnknownItem(curOffset, 1);
			uint8_t arg1 = GetByte(curOffset++);

			aHeader->unLength = curOffset - beginOffset;
			break;
		}

		case HEADER_EVENT_09:
		{
			VGMHeader* aHeader = header->AddHeader(beginOffset, 1, L"Unknown");
			aHeader->AddSimpleItem(beginOffset, 1, L"Event ID");

			aHeader->AddSimpleItem(curOffset, 1, L"Number of Items");
			uint8_t tableSize = GetByte(curOffset++) * 2;
			if (curOffset + tableSize > 0x10000) {
				return false;
			}

			aHeader->AddUnknownItem(curOffset, tableSize);
			curOffset += tableSize;

			aHeader->unLength = curOffset - beginOffset;
			break;
		}

		default:
			header->AddUnknownItem(beginOffset, curOffset - beginOffset);
			goto header_end;
		}
	}

header_end:
	header->SetGuessedLength();

	return true;
}

bool HudsonSnesSeq::GetTrackPointersInHeaderInfo(VGMHeader* header, uint32_t & offset)
{
	uint32_t beginOffset = offset;
	uint32_t curOffset = beginOffset;

	if (curOffset + 1 > 0x10000) {
		return false;
	}

	// flag field that indicates track availability
	header->AddSimpleItem(curOffset, 1, L"Track Availability");
	TrackAvailableBits = GetByte(curOffset++);

	// read track addresses (DSP channel 8 to 1)
	for (int trackIndex = 0; trackIndex < MAX_TRACKS; trackIndex++) {
		if ((TrackAvailableBits & (1 << trackIndex)) != 0) {
			if (curOffset + 2 > 0x10000) {
				offset = curOffset;
				return false;
			}

			std::wstringstream trackName;
			trackName << L"Track Pointer " << (trackIndex + 1);
			header->AddSimpleItem(curOffset, 2, trackName.str().c_str());
			TrackAddresses[trackIndex] = GetShort(curOffset); curOffset += 2;
		}
	}

	offset = curOffset;
	return true;
}

bool HudsonSnesSeq::GetTrackPointers(void)
{
	for (int trackIndex = 0; trackIndex < MAX_TRACKS; trackIndex++) {
		if ((TrackAvailableBits & (1 << trackIndex)) != 0) {
			HudsonSnesTrack* track = new HudsonSnesTrack(this, TrackAddresses[trackIndex]);
			aTracks.push_back(track);
		}
	}

	return true;
}

void HudsonSnesSeq::LoadEventMap(HudsonSnesSeq *pSeqFile)
{
	if (version == HUDSONSNES_V0 || version == HUDSONSNES_V1) {
		HeaderEventMap[0x00] = HEADER_EVENT_END;
		HeaderEventMap[0x01] = HEADER_EVENT_TIMEBASE;
		HeaderEventMap[0x02] = HEADER_EVENT_INSTRUMENTS_V1;
		HeaderEventMap[0x03] = HEADER_EVENT_PERCUSSIONS_V1;
		HeaderEventMap[0x04] = HEADER_EVENT_INSTRUMENTS_V2;
		HeaderEventMap[0x05] = HEADER_EVENT_05;
	}
	else { // HUDSONSNES_V2
		HeaderEventMap[0x00] = HEADER_EVENT_END;
		HeaderEventMap[0x01] = HEADER_EVENT_TRACKS;
		HeaderEventMap[0x02] = HEADER_EVENT_TIMEBASE;
		HeaderEventMap[0x03] = HEADER_EVENT_INSTRUMENTS_V2;
		HeaderEventMap[0x04] = HEADER_EVENT_PERCUSSIONS_V2;
		HeaderEventMap[0x05] = HEADER_EVENT_05;
		HeaderEventMap[0x06] = HEADER_EVENT_06;
		HeaderEventMap[0x07] = HEADER_EVENT_ECHO_PARAM;
		HeaderEventMap[0x08] = HEADER_EVENT_08;
		HeaderEventMap[0x09] = HEADER_EVENT_09;
	}

	for (int statusByte = 0x00; statusByte <= 0xcf; statusByte++) {
		pSeqFile->EventMap[statusByte] = EVENT_NOTE;
	}

	pSeqFile->EventMap[0xd0] = EVENT_NOP;
	pSeqFile->EventMap[0xd1] = EVENT_TEMPO;
	pSeqFile->EventMap[0xd2] = EVENT_OCTAVE;
	pSeqFile->EventMap[0xd3] = EVENT_OCTAVE_UP;
	pSeqFile->EventMap[0xd4] = EVENT_OCTAVE_DOWN;
	pSeqFile->EventMap[0xd5] = EVENT_QUANTIZE;
	pSeqFile->EventMap[0xd6] = EVENT_PROGCHANGE;
	pSeqFile->EventMap[0xd7] = EVENT_NOP1;
	pSeqFile->EventMap[0xd8] = EVENT_NOP1;
	pSeqFile->EventMap[0xd9] = EVENT_VOLUME;
	pSeqFile->EventMap[0xda] = EVENT_PAN;
	pSeqFile->EventMap[0xdb] = EVENT_REVERSE_PHASE;
	pSeqFile->EventMap[0xdc] = EVENT_VOLUME_REL;
	pSeqFile->EventMap[0xdd] = EVENT_LOOP_START;
	pSeqFile->EventMap[0xde] = EVENT_LOOP_END;
	pSeqFile->EventMap[0xdf] = EVENT_SUBROUTINE;
	pSeqFile->EventMap[0xe0] = EVENT_GOTO;
	pSeqFile->EventMap[0xe1] = EVENT_TUNING;
	pSeqFile->EventMap[0xe2] = EVENT_VIBRATO;
	pSeqFile->EventMap[0xe3] = EVENT_VIBRATO_DELAY;
	pSeqFile->EventMap[0xe4] = EVENT_ECHO_VOLUME;
	pSeqFile->EventMap[0xe5] = EVENT_ECHO_PARAM;
	pSeqFile->EventMap[0xe6] = EVENT_ECHO_ON;
	pSeqFile->EventMap[0xe7] = EVENT_TRANSPOSE_ABS;
	pSeqFile->EventMap[0xe8] = EVENT_TRANSPOSE_REL;
	pSeqFile->EventMap[0xe9] = EVENT_PITCH_ATTACK_ENV_ON;
	pSeqFile->EventMap[0xea] = EVENT_PITCH_ATTACK_ENV_OFF;
	pSeqFile->EventMap[0xeb] = EVENT_LOOP_POSITION;
	pSeqFile->EventMap[0xec] = EVENT_JUMP_TO_LOOP_POSITION;
	pSeqFile->EventMap[0xed] = EVENT_LOOP_POSITION_ALT;
	pSeqFile->EventMap[0xee] = EVENT_VOLUME_FROM_TABLE;
	pSeqFile->EventMap[0xef] = EVENT_UNKNOWN2;
	pSeqFile->EventMap[0xf0] = EVENT_UNKNOWN1;
	pSeqFile->EventMap[0xf1] = EVENT_PORTAMENTO;
	pSeqFile->EventMap[0xf2] = EVENT_NOP;
	pSeqFile->EventMap[0xf3] = EVENT_NOP;
	pSeqFile->EventMap[0xf4] = EVENT_NOP;
	pSeqFile->EventMap[0xf5] = EVENT_NOP;
	pSeqFile->EventMap[0xf6] = EVENT_NOP;
	pSeqFile->EventMap[0xf7] = EVENT_NOP;
	pSeqFile->EventMap[0xf8] = EVENT_NOP;
	pSeqFile->EventMap[0xf9] = EVENT_NOP;
	pSeqFile->EventMap[0xfa] = EVENT_NOP;
	pSeqFile->EventMap[0xfb] = EVENT_NOP;
	pSeqFile->EventMap[0xfc] = EVENT_NOP;
	pSeqFile->EventMap[0xfd] = EVENT_NOP;
	pSeqFile->EventMap[0xfe] = EVENT_SUBEVENT;
	pSeqFile->EventMap[0xff] = EVENT_END;

	pSeqFile->SubEventMap[0x00] = SUBEVENT_END;
	pSeqFile->SubEventMap[0x01] = SUBEVENT_ECHO_OFF;
	pSeqFile->SubEventMap[0x02] = SUBEVENT_UNKNOWN0;
	pSeqFile->SubEventMap[0x03] = SUBEVENT_PERC_ON;
	pSeqFile->SubEventMap[0x04] = SUBEVENT_PERC_OFF;
	pSeqFile->SubEventMap[0x05] = SUBEVENT_VIBRATO_TYPE;
	pSeqFile->SubEventMap[0x06] = SUBEVENT_VIBRATO_TYPE;
	pSeqFile->SubEventMap[0x07] = SUBEVENT_VIBRATO_TYPE;
	pSeqFile->SubEventMap[0x08] = SUBEVENT_UNKNOWN0;
	pSeqFile->SubEventMap[0x09] = SUBEVENT_UNKNOWN0;

	if (version == HUDSONSNES_V2) {
		pSeqFile->EventMap[0xd0] = EVENT_UNKNOWN0;
		pSeqFile->EventMap[0xd7] = EVENT_NOP;
		pSeqFile->EventMap[0xd8] = EVENT_NOP;
		pSeqFile->EventMap[0xe2] = EVENT_VIBRATO;
		pSeqFile->EventMap[0xee] = EVENT_NOP;
		pSeqFile->EventMap[0xf2] = EVENT_UNKNOWN1;
		pSeqFile->EventMap[0xf3] = EVENT_UNKNOWN1;

		pSeqFile->SubEventMap[0x05] = SUBEVENT_NOP;
		pSeqFile->SubEventMap[0x06] = SUBEVENT_NOP;
		pSeqFile->SubEventMap[0x07] = SUBEVENT_NOP;
		pSeqFile->SubEventMap[0x0a] = SUBEVENT_UNKNOWN0;
		pSeqFile->SubEventMap[0x0b] = SUBEVENT_UNKNOWN0;
		pSeqFile->SubEventMap[0x0c] = SUBEVENT_UNKNOWN0;
		pSeqFile->SubEventMap[0x0d] = SUBEVENT_UNKNOWN1;
		pSeqFile->SubEventMap[0x0e] = SUBEVENT_NOP;
		pSeqFile->SubEventMap[0x0f] = SUBEVENT_NOP;
		pSeqFile->SubEventMap[0x10] = SUBEVENT_MOV_IMM;
		pSeqFile->SubEventMap[0x11] = SUBEVENT_MOV;
		pSeqFile->SubEventMap[0x12] = SUBEVENT_CMP_IMM;
		pSeqFile->SubEventMap[0x13] = SUBEVENT_CMP;
		pSeqFile->SubEventMap[0x14] = SUBEVENT_BNE;
		pSeqFile->SubEventMap[0x15] = SUBEVENT_BEQ;
		pSeqFile->SubEventMap[0x16] = SUBEVENT_BCS;
		pSeqFile->SubEventMap[0x17] = SUBEVENT_BCC;
		pSeqFile->SubEventMap[0x18] = SUBEVENT_BMI;
		pSeqFile->SubEventMap[0x19] = SUBEVENT_BPL;
		pSeqFile->SubEventMap[0x1a] = SUBEVENT_ADSR_AR;
		pSeqFile->SubEventMap[0x1b] = SUBEVENT_ADSR_DR;
		pSeqFile->SubEventMap[0x1c] = SUBEVENT_ADSR_SL;
		pSeqFile->SubEventMap[0x1d] = SUBEVENT_ADSR_SR;
		pSeqFile->SubEventMap[0x1e] = SUBEVENT_ADSR_RR;
	}
}

//  ***************
//  HudsonSnesTrack
//  ***************

HudsonSnesTrack::HudsonSnesTrack(HudsonSnesSeq* parentFile, long offset, long length)
	: SeqTrack(parentFile, offset, length)
{
	ResetVars();
	bDetermineTrackLengthEventByEvent = true;
	bWriteGenericEventAsTextEvent = false;
}

void HudsonSnesTrack::ResetVars(void)
{
	SeqTrack::ResetVars();
}


bool HudsonSnesTrack::ReadEvent(void)
{
	HudsonSnesSeq* parentSeq = (HudsonSnesSeq*)this->parentSeq;

	uint32_t beginOffset = curOffset;
	if (curOffset >= 0x10000) {
		return false;
	}

	uint8_t statusByte = GetByte(curOffset++);
	bool bContinue = true;

	std::wstringstream desc;

	HudsonSnesSeqEventType eventType = (HudsonSnesSeqEventType)0;
	std::map<uint8_t, HudsonSnesSeqEventType>::iterator pEventType = parentSeq->EventMap.find(statusByte);
	if (pEventType != parentSeq->EventMap.end()) {
		eventType = pEventType->second;
	}

	switch (eventType)
	{
	case EVENT_UNKNOWN0:
		desc << L"Event: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << (int)statusByte;
		AddUnknown(beginOffset, curOffset - beginOffset, L"Unknown Event", desc.str().c_str());
		break;

	case EVENT_UNKNOWN1:
	{
		uint8_t arg1 = GetByte(curOffset++);
		desc << L"Event: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << (int)statusByte
			<< std::dec << std::setfill(L' ') << std::setw(0)
			<< L"  Arg1: " << (int)arg1;
		AddUnknown(beginOffset, curOffset - beginOffset, L"Unknown Event", desc.str().c_str());
		break;
	}

	case EVENT_UNKNOWN2:
	{
		uint8_t arg1 = GetByte(curOffset++);
		uint8_t arg2 = GetByte(curOffset++);
		desc << L"Event: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << (int)statusByte
			<< std::dec << std::setfill(L' ') << std::setw(0)
			<< L"  Arg1: " << (int)arg1
			<< L"  Arg2: " << (int)arg2;
		AddUnknown(beginOffset, curOffset - beginOffset, L"Unknown Event", desc.str().c_str());
		break;
	}

	case EVENT_UNKNOWN3:
	{
		uint8_t arg1 = GetByte(curOffset++);
		uint8_t arg2 = GetByte(curOffset++);
		uint8_t arg3 = GetByte(curOffset++);
		desc << L"Event: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << (int)statusByte
			<< std::dec << std::setfill(L' ') << std::setw(0)
			<< L"  Arg1: " << (int)arg1
			<< L"  Arg2: " << (int)arg2
			<< L"  Arg3: " << (int)arg3;
		AddUnknown(beginOffset, curOffset - beginOffset, L"Unknown Event", desc.str().c_str());
		break;
	}

	case EVENT_UNKNOWN4:
	{
		uint8_t arg1 = GetByte(curOffset++);
		uint8_t arg2 = GetByte(curOffset++);
		uint8_t arg3 = GetByte(curOffset++);
		uint8_t arg4 = GetByte(curOffset++);
		desc << L"Event: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << (int)statusByte
			<< std::dec << std::setfill(L' ') << std::setw(0)
			<< L"  Arg1: " << (int)arg1
			<< L"  Arg2: " << (int)arg2
			<< L"  Arg3: " << (int)arg3
			<< L"  Arg4: " << (int)arg4;
		AddUnknown(beginOffset, curOffset - beginOffset, L"Unknown Event", desc.str().c_str());
		break;
	}

	default:
		desc << L"Event: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << (int)statusByte;
		AddUnknown(beginOffset, curOffset - beginOffset, L"Unknown Event", desc.str().c_str());
		pRoot->AddLogItem(new LogItem(std::wstring(L"Unknown Event - ") + desc.str(), LOG_LEVEL_ERR, std::wstring(L"HudsonSnesSeq")));
		bContinue = false;
		break;
	}

	//wostringstream ssTrace;
	//ssTrace << L"" << std::hex << std::setfill(L'0') << std::setw(8) << std::uppercase << beginOffset << L": " << std::setw(2) << (int)statusByte  << L" -> " << std::setw(8) << curOffset << std::endl;
	//OutputDebugString(ssTrace.str().c_str());

	return bContinue;
}
