#include "stdafx.h"
#include "MintSnesSeq.h"
#include "MintSnesFormat.h"
#include "ScaleConversion.h"

DECLARE_FORMAT(MintSnes);

//  ***********
//  MintSnesSeq
//  ***********
#define MAX_TRACKS  10
#define SEQ_PPQN    48

MintSnesSeq::MintSnesSeq(RawFile* file, MintSnesVersion ver, uint32_t seqdataOffset, std::wstring newName)
	: VGMSeq(MintSnesFormat::name, file, seqdataOffset, 0, newName),
	version(ver)
{
	bLoadTickByTick = true;
	bAllowDiscontinuousTrackData = true;

	UseReverb();
	AlwaysWriteInitialReverb(0);

	LoadEventMap(this);
}

MintSnesSeq::~MintSnesSeq(void)
{
}

void MintSnesSeq::ResetVars(void)
{
	VGMSeq::ResetVars();
}

bool MintSnesSeq::GetHeaderInfo(void)
{
	SetPPQN(SEQ_PPQN);

	uint32_t curOffset = dwOffset;
	VGMHeader* header = AddHeader(dwOffset, 0);

	// reset track start addresses
	for (uint8_t trackIndex = 0; trackIndex < MAX_TRACKS; trackIndex++) {
		TrackStartAddress[trackIndex] = 0;
	}

	// parse header events
	while (true) {
		uint32_t beginOffset = curOffset;
		uint8_t statusByte = GetByte(curOffset++);
		if (statusByte == 0xff) {
			header->AddSimpleItem(beginOffset, curOffset - beginOffset, L"Header End");
			break;
		}

		if (statusByte <= 0x7f) {
			uint8_t trackIndex = statusByte;
			if (trackIndex > MAX_TRACKS) {
				// out of range
				return false;
			}

			if (curOffset + 2 > 0x10000) {
				return false;
			}

			uint16_t ofsTrackStart = GetShort(curOffset); curOffset += 2;
			TrackStartAddress[trackIndex] = curOffset + ofsTrackStart;

			std::wstringstream trackName;
			trackName << L"Track " << (trackIndex + 1) << L" Offset";
			header->AddSimpleItem(beginOffset, curOffset - beginOffset, trackName.str().c_str());
		}
		else {
			header->AddUnknownItem(beginOffset, curOffset - beginOffset);
			break;
		}

		if (curOffset + 1 > 0x10000) {
			return false;
		}
	}

	header->SetGuessedLength();
	return true;
}

bool MintSnesSeq::GetTrackPointers(void)
{
	for (uint8_t trackIndex = 0; trackIndex < MAX_TRACKS; trackIndex++) {
		if (TrackStartAddress[trackIndex] != 0) {
			MintSnesTrack* track = new MintSnesTrack(this, TrackStartAddress[trackIndex]);
			aTracks.push_back(track);
		}
	}
	return true;
}

void MintSnesSeq::LoadEventMap(MintSnesSeq *pSeqFile)
{
	// TODO: MintSnesSeq::LoadEventMap
}

//  ***************
//  MintSnesTrack
//  ***************

MintSnesTrack::MintSnesTrack(MintSnesSeq* parentFile, long offset, long length)
	: SeqTrack(parentFile, offset, length)
{
	ResetVars();
	bDetermineTrackLengthEventByEvent = true;
	bWriteGenericEventAsTextEvent = false;
}

void MintSnesTrack::ResetVars(void)
{
	SeqTrack::ResetVars();
}


bool MintSnesTrack::ReadEvent(void)
{
	MintSnesSeq* parentSeq = (MintSnesSeq*)this->parentSeq;

	uint32_t beginOffset = curOffset;
	if (curOffset >= 0x10000) {
		return false;
	}

	uint8_t statusByte = GetByte(curOffset++);
	bool bContinue = true;

	std::wstringstream desc;

	MintSnesSeqEventType eventType = (MintSnesSeqEventType)0;
	std::map<uint8_t, MintSnesSeqEventType>::iterator pEventType = parentSeq->EventMap.find(statusByte);
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
		pRoot->AddLogItem(new LogItem(std::wstring(L"Unknown Event - ") + desc.str(), LOG_LEVEL_ERR, std::wstring(L"MintSnesSeq")));
		bContinue = false;
		break;
	}

	//wostringstream ssTrace;
	//ssTrace << L"" << std::hex << std::setfill(L'0') << std::setw(8) << std::uppercase << beginOffset << L": " << std::setw(2) << (int)statusByte  << L" -> " << std::setw(8) << curOffset << std::endl;
	//OutputDebugString(ssTrace.str().c_str());

	return bContinue;
}
