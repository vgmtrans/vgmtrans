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

	// TODO: HudsonSnesSeq::LoadEventMap
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
