#include "stdafx.h"
#include "HeartBeatSnesSeq.h"
#include "HeartBeatSnesFormat.h"
#include "ScaleConversion.h"

DECLARE_FORMAT(HeartBeatSnes);

//  ****************
//  HeartBeatSnesSeq
//  ****************
#define MAX_TRACKS  8
#define SEQ_PPQN    24

HeartBeatSnesSeq::HeartBeatSnesSeq(RawFile* file, HeartBeatSnesVersion ver, uint32_t seqdataOffset, std::wstring newName)
	: VGMSeq(HeartBeatSnesFormat::name, file, seqdataOffset, 0, newName),
	version(ver)
{
	bLoadTickByTick = true;
	bAllowDiscontinuousTrackData = true;
	bUseLinearAmplitudeScale = true;

	UseReverb();

	LoadEventMap(this);
}

HeartBeatSnesSeq::~HeartBeatSnesSeq(void)
{
}

void HeartBeatSnesSeq::ResetVars(void)
{
	VGMSeq::ResetVars();
}

bool HeartBeatSnesSeq::GetHeaderInfo(void)
{
	SetPPQN(SEQ_PPQN);

	VGMHeader* header = AddHeader(dwOffset, 0);
	uint32_t curOffset = dwOffset;
	if (curOffset + 2 > 0x10000) {
		return false;
	}

	header->AddUnknownItem(curOffset, 1);
	curOffset++;

	header->AddUnknownItem(curOffset, 1);
	curOffset++;

	for (uint8_t trackIndex = 0; trackIndex < MAX_TRACKS; trackIndex++) {
		uint16_t ofsTrackStart = GetShort(curOffset);
		if (ofsTrackStart != 0) {
			std::wstringstream trackName;
			trackName << L"Track Pointer " << (trackIndex + 1);
			header->AddSimpleItem(curOffset, 2, trackName.str().c_str());
		}
		else {
			header->AddSimpleItem(curOffset, 2, L"NULL");
		}
		curOffset += 2;
	}

	return true;
}

bool HeartBeatSnesSeq::GetTrackPointers(void)
{
	uint32_t curOffset = dwOffset;

	curOffset += 2;

	for (uint8_t trackIndex = 0; trackIndex < MAX_TRACKS; trackIndex++) {
		uint16_t ofsTrackStart = GetShort(curOffset);
		if (ofsTrackStart != 0) {
			uint16_t addrTrackStart = dwOffset + ofsTrackStart;
			HeartBeatSnesTrack* track = new HeartBeatSnesTrack(this, addrTrackStart);
			aTracks.push_back(track);
		}
		curOffset += 2;
	}

	return true;
}

void HeartBeatSnesSeq::LoadEventMap(HeartBeatSnesSeq *pSeqFile)
{
	// TODO: HeartBeatSnesSeq::LoadEventMap
}

double HeartBeatSnesSeq::GetTempoInBPM(uint8_t tempo)
{
	if (tempo != 0)
	{
		return 60000000.0 / (SEQ_PPQN * (125 * 0x10)) * (tempo / 256.0);
	}
	else
	{
		return 1.0; // since tempo 0 cannot be expressed, this function returns a very small value.
	}
}

//  ******************
//  HeartBeatSnesTrack
//  ******************

HeartBeatSnesTrack::HeartBeatSnesTrack(HeartBeatSnesSeq* parentFile, long offset, long length)
	: SeqTrack(parentFile, offset, length)
{
	ResetVars();
	bDetermineTrackLengthEventByEvent = true;
	bWriteGenericEventAsTextEvent = false;
}

void HeartBeatSnesTrack::ResetVars(void)
{
	SeqTrack::ResetVars();
}


bool HeartBeatSnesTrack::ReadEvent(void)
{
	HeartBeatSnesSeq* parentSeq = (HeartBeatSnesSeq*)this->parentSeq;

	uint32_t beginOffset = curOffset;
	if (curOffset >= 0x10000) {
		return false;
	}

	uint8_t statusByte = GetByte(curOffset++);
	bool bContinue = true;

	std::wstringstream desc;

	HeartBeatSnesSeqEventType eventType = (HeartBeatSnesSeqEventType)0;
	std::map<uint8_t, HeartBeatSnesSeqEventType>::iterator pEventType = parentSeq->EventMap.find(statusByte);
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
		pRoot->AddLogItem(new LogItem(std::wstring(L"Unknown Event - ") + desc.str(), LOG_LEVEL_ERR, std::wstring(L"HeartBeatSnesSeq")));
		bContinue = false;
		break;
	}

	//wostringstream ssTrace;
	//ssTrace << L"" << std::hex << std::setfill(L'0') << std::setw(8) << std::uppercase << beginOffset << L": " << std::setw(2) << (int)statusByte  << L" -> " << std::setw(8) << curOffset << std::endl;
	//OutputDebugString(ssTrace.str().c_str());

	return bContinue;
}
