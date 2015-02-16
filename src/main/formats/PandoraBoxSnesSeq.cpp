#include "stdafx.h"
#include "PandoraBoxSnesSeq.h"
#include "PandoraBoxSnesFormat.h"
#include "ScaleConversion.h"

DECLARE_FORMAT(PandoraBoxSnes);

//  *****************
//  PandoraBoxSnesSeq
//  *****************
#define MAX_TRACKS  8

PandoraBoxSnesSeq::PandoraBoxSnesSeq(RawFile* file, PandoraBoxSnesVersion ver, uint32_t seqdataOffset, std::wstring newName)
	: VGMSeq(PandoraBoxSnesFormat::name, file, seqdataOffset, 0, newName), version(ver)
{
	bLoadTickByTick = true;
	bAllowDiscontinuousTrackData = true;
	bUseLinearAmplitudeScale = true;

	UseReverb();
	AlwaysWriteInitialReverb(0);

	LoadEventMap();
}

PandoraBoxSnesSeq::~PandoraBoxSnesSeq(void)
{
}

void PandoraBoxSnesSeq::ResetVars(void)
{
	VGMSeq::ResetVars();
}

bool PandoraBoxSnesSeq::GetHeaderInfo(void)
{
	uint32_t curOffset;

	VGMHeader* header = AddHeader(dwOffset, 0);
	if (dwOffset + 0x20 > 0x10000) {
		return false;
	}

	AddSimpleItem(dwOffset + 6, 1, L"Tempo");
	AddSimpleItem(dwOffset + 7, 1, L"Timebase");

	bWriteInitialTempo = true;
	tempoBPM = GetByte(dwOffset + 6);
	uint8_t timebase = GetByte(dwOffset + 7);
	assert((timebase % 4) == 0);
	SetPPQN(timebase / 4);

	curOffset = dwOffset + 0x10;
	for (uint8_t trackIndex = 0; trackIndex < MAX_TRACKS; trackIndex++) {
		uint16_t ofsTrackStart = GetShort(curOffset);
		if (ofsTrackStart != 0xffff) {
			std::wstringstream trackName;
			trackName << L"Track Pointer " << (trackIndex + 1);
			header->AddSimpleItem(curOffset, 2, trackName.str());
		}
		else {
			header->AddSimpleItem(curOffset, 2, L"NULL");
		}
		curOffset += 2;
	}

	return true;
}

bool PandoraBoxSnesSeq::GetTrackPointers(void)
{
	uint32_t curOffset = dwOffset + 0x10;
	for (uint8_t trackIndex = 0; trackIndex < MAX_TRACKS; trackIndex++) {
		uint16_t ofsTrackStart = GetShort(curOffset); curOffset += 2;
		if (ofsTrackStart != 0xffff) {
			uint16_t addrTrackStart = dwOffset + ofsTrackStart;
			PandoraBoxSnesTrack* track = new PandoraBoxSnesTrack(this, addrTrackStart);
			aTracks.push_back(track);
		}
	}
	return true;
}

void PandoraBoxSnesSeq::LoadEventMap()
{
	// TODO: PandoraBoxSnesSeq::LoadEventMap
}


//  *****************
//  PandoraBoxSnesTrack
//  *****************

PandoraBoxSnesTrack::PandoraBoxSnesTrack(PandoraBoxSnesSeq* parentFile, long offset, long length)
: SeqTrack(parentFile, offset, length)
{
	ResetVars();
	bDetermineTrackLengthEventByEvent = true;
	bWriteGenericEventAsTextEvent = false;
}

void PandoraBoxSnesTrack::ResetVars(void)
{
	SeqTrack::ResetVars();
}

bool PandoraBoxSnesTrack::ReadEvent(void)
{
	PandoraBoxSnesSeq* parentSeq = (PandoraBoxSnesSeq*)this->parentSeq;

	uint32_t beginOffset = curOffset;
	if (curOffset >= 0x10000) {
		return false;
	}

	uint8_t statusByte = GetByte(curOffset++);
	bool bContinue = true;

	std::wstringstream desc;

	PandoraBoxSnesSeqEventType eventType = (PandoraBoxSnesSeqEventType)0;
	std::map<uint8_t, PandoraBoxSnesSeqEventType>::iterator pEventType = parentSeq->EventMap.find(statusByte);
	if (pEventType != parentSeq->EventMap.end()) {
		eventType = pEventType->second;
	}

	switch (eventType)
	{
	case EVENT_UNKNOWN0:
		desc << L"Event: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << (int)statusByte;
		AddUnknown(beginOffset, curOffset-beginOffset, L"Unknown Event", desc.str());
		break;

	case EVENT_UNKNOWN1:
	{
		uint8_t arg1 = GetByte(curOffset++);
		desc << L"Event: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << (int)statusByte
			<< std::dec << std::setfill(L' ') << std::setw(0)
			<< L"  Arg1: " << (int)arg1;
		AddUnknown(beginOffset, curOffset-beginOffset, L"Unknown Event", desc.str());
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
		AddUnknown(beginOffset, curOffset-beginOffset, L"Unknown Event", desc.str());
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
		AddUnknown(beginOffset, curOffset-beginOffset, L"Unknown Event", desc.str());
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
		AddUnknown(beginOffset, curOffset-beginOffset, L"Unknown Event", desc.str());
		break;
	}

	default:
		desc << L"Event: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << (int)statusByte;
		AddUnknown(beginOffset, curOffset-beginOffset, L"Unknown Event", desc.str());
		pRoot->AddLogItem(new LogItem(std::wstring(L"Unknown Event - ") + desc.str(), LOG_LEVEL_ERR, std::wstring(L"PandoraBoxSnesSeq")));
		bContinue = false;
		break;
	}

	//std::wostringstream ssTrace;
	//ssTrace << L"" << std::hex << std::setfill(L'0') << std::setw(8) << std::uppercase << beginOffset << L": " << std::setw(2) << (int)statusByte  << L" -> " << std::setw(8) << curOffset << std::endl;
	//OutputDebugString(ssTrace.str().c_str());

	return bContinue;
}
