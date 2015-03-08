#include "stdafx.h"
#include "NeverlandSnesSeq.h"
#include "NeverlandSnesFormat.h"
#include "ScaleConversion.h"

DECLARE_FORMAT(NeverlandSnes);

//  ****************
//  NeverlandSnesSeq
//  ****************
#define MAX_TRACKS  8
#define SEQ_PPQN    48

NeverlandSnesSeq::NeverlandSnesSeq(RawFile* file, NeverlandSnesVersion ver, uint32_t seqdataOffset)
	: VGMSeq(NeverlandSnesFormat::name, file, seqdataOffset), version(ver)
{
	bLoadTickByTick = true;
	bAllowDiscontinuousTrackData = true;
	bUseLinearAmplitudeScale = true;

	UseReverb();
	AlwaysWriteInitialReverb(0);

	LoadEventMap();
}

NeverlandSnesSeq::~NeverlandSnesSeq(void)
{
}

void NeverlandSnesSeq::ResetVars(void)
{
	VGMSeq::ResetVars();
}

bool NeverlandSnesSeq::GetHeaderInfo(void)
{
	SetPPQN(SEQ_PPQN);

	VGMHeader* header = AddHeader(dwOffset, 0);
	if (version == NEVERLANDSNES_SFC) {
		header->unLength = 0x40;
	}
	else if (version == NEVERLANDSNES_S2C) {
		header->unLength = 0x50;
	}

	if (dwOffset + header->unLength >= 0x10000) {
		return false;
	}

	header->AddSimpleItem(dwOffset, 3, L"Signature");
	header->AddUnknownItem(dwOffset + 3, 1);

	const size_t NAME_SIZE = 12;
	char rawName[NAME_SIZE + 1] = { 0 };
	GetBytes(dwOffset + 4, NAME_SIZE, rawName);
	header->AddSimpleItem(dwOffset + 4, 12, L"Song Name");

	// trim name text
	for (int i = NAME_SIZE - 1; i >= 0; i--) {
		if (rawName[i] != ' ') {
			break;
		}
		rawName[i] = '\0';
	}
	// set name to the sequence
	if (rawName[0] != ('\0')) {
		name = string2wstring(std::string(rawName));
	}
	else {
		name = L"NeverlandSnesSeq";
	}

	for (uint8_t trackIndex = 0; trackIndex < MAX_TRACKS; trackIndex++) {
		uint16_t trackSignPtr = dwOffset + 0x10 + trackIndex;
		uint8_t trackSign = GetByte(trackSignPtr);

		std::wstringstream trackSignName;
		trackSignName << L"Track " << (trackIndex + 1) << L" Entry";
		header->AddSimpleItem(trackSignPtr, 1, trackSignName.str());

		uint16_t sectionListOffsetPtr = dwOffset + 0x20 + (trackIndex * 2);
		if (trackSign != 0xff) {
			uint16_t sectionListAddress = GetShortAddress(sectionListOffsetPtr);

			std::wstringstream playlistName;
			playlistName << L"Track " << (trackIndex + 1) << L" Playlist Pointer";
			header->AddSimpleItem(sectionListOffsetPtr, 2, playlistName.str());

			NeverlandSnesTrack* track = new NeverlandSnesTrack(this, sectionListAddress);
			aTracks.push_back(track);
		}
		else {
			header->AddSimpleItem(sectionListOffsetPtr, 2, L"NULL");
		}
	}

	return true;
}

bool NeverlandSnesSeq::GetTrackPointers(void)
{
	return true;
}

void NeverlandSnesSeq::LoadEventMap()
{
	// TODO: NeverlandSnesSeq::LoadEventMap
}

uint16_t NeverlandSnesSeq::ConvertToAPUAddress(uint16_t offset)
{
	if (version == NEVERLANDSNES_S2C) {
		return dwOffset + offset;
	}
	else {
		return offset;
	}
}

uint16_t NeverlandSnesSeq::GetShortAddress(uint32_t offset)
{
	return ConvertToAPUAddress(GetShort(offset));
}

//  ******************
//  NeverlandSnesTrack
//  ******************

NeverlandSnesTrack::NeverlandSnesTrack(NeverlandSnesSeq* parentFile, long offset, long length)
: SeqTrack(parentFile, offset, length)
{
	ResetVars();
	bDetermineTrackLengthEventByEvent = true;
	bWriteGenericEventAsTextEvent = false;
}

void NeverlandSnesTrack::ResetVars(void)
{
	SeqTrack::ResetVars();
}

bool NeverlandSnesTrack::ReadEvent(void)
{
	NeverlandSnesSeq* parentSeq = (NeverlandSnesSeq*)this->parentSeq;

	uint32_t beginOffset = curOffset;
	if (curOffset >= 0x10000) {
		return false;
	}

	uint8_t statusByte = GetByte(curOffset++);
	bool bContinue = true;

	std::wstringstream desc;

	NeverlandSnesSeqEventType eventType = (NeverlandSnesSeqEventType)0;
	std::map<uint8_t, NeverlandSnesSeqEventType>::iterator pEventType = parentSeq->EventMap.find(statusByte);
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
		pRoot->AddLogItem(new LogItem(std::wstring(L"Unknown Event - ") + desc.str(), LOG_LEVEL_ERR, std::wstring(L"NeverlandSnesSeq")));
		bContinue = false;
		break;
	}

	//std::wostringstream ssTrace;
	//ssTrace << L"" << std::hex << std::setfill(L'0') << std::setw(8) << std::uppercase << beginOffset << L": " << std::setw(2) << (int)statusByte  << L" -> " << std::setw(8) << curOffset << std::endl;
	//OutputDebugString(ssTrace.str().c_str());

	return bContinue;
}

uint16_t NeverlandSnesTrack::ConvertToAPUAddress(uint16_t offset)
{
	NeverlandSnesSeq* parentSeq = (NeverlandSnesSeq*)this->parentSeq;
	return parentSeq->ConvertToAPUAddress(offset);
}

uint16_t NeverlandSnesTrack::GetShortAddress(uint32_t offset)
{
	NeverlandSnesSeq* parentSeq = (NeverlandSnesSeq*)this->parentSeq;
	return parentSeq->GetShortAddress(offset);
}
