#include "stdafx.h"
#include "AkaoSnesSeq.h"
#include "AkaoSnesFormat.h"
#include "ScaleConversion.h"

DECLARE_FORMAT(AkaoSnes);

//  **********
//  AkaoSnesSeq
//  **********
#define MAX_TRACKS  8
#define SEQ_PPQN    48

AkaoSnesSeq::AkaoSnesSeq(RawFile* file, AkaoSnesVersion ver, AkaoSnesMinorVersion minorVer, uint32_t seqdataOffset, uint32_t addrAPURelocBase, std::wstring newName)
	: VGMSeq(AkaoSnesFormat::name, file, seqdataOffset, 0, newName),
	version(ver),
	minorVersion(minorVer),
	addrAPURelocBase(addrAPURelocBase),
	addrROMRelocBase(addrAPURelocBase),
	addrSequenceEnd(0)
{
	bLoadTickByTick = true;
	bAllowDiscontinuousTrackData = true;

	UseReverb();
	AlwaysWriteInitialReverb(0);

	LoadEventMap(this);
}

AkaoSnesSeq::~AkaoSnesSeq(void)
{
}

void AkaoSnesSeq::ResetVars(void)
{
	VGMSeq::ResetVars();
}

bool AkaoSnesSeq::GetHeaderInfo(void)
{
	SetPPQN(SEQ_PPQN);

	VGMHeader* header = AddHeader(dwOffset, 0);
	uint32_t curOffset = dwOffset;
	if (version == AKAOSNES_V1 || version == AKAOSNES_V2) {
		// Earlier versions are not relocatable
		// All addresses are plain APU addresses
		addrROMRelocBase = addrAPURelocBase;
		addrSequenceEnd = ROMAddressToAPUAddress(0);
	}
	else {
		// Later versions are relocatable
		if (version == AKAOSNES_V3) {
			header->AddSimpleItem(curOffset, 2, L"ROM Address Base");
			addrROMRelocBase = GetShort(curOffset);
			if (minorVersion != AKAOSNES_V3_FFMQ) {
				curOffset += 2;
			}

			header->AddSimpleItem(curOffset + MAX_TRACKS * 2, 2, L"End Address");
			addrSequenceEnd = GetShortAddress(curOffset + MAX_TRACKS * 2);
		}
		else if (version == AKAOSNES_V4) {
			header->AddSimpleItem(curOffset, 2, L"ROM Address Base");
			addrROMRelocBase = GetShort(curOffset); curOffset += 2;

			header->AddSimpleItem(curOffset, 2, L"End Address");
			addrSequenceEnd = GetShortAddress(curOffset); curOffset += 2;
		}

		// calculate sequence length
		if (addrSequenceEnd < dwOffset) {
			return false;
		}
		unLength = addrSequenceEnd - dwOffset;
	}

	for (uint8_t trackIndex = 0; trackIndex < MAX_TRACKS; trackIndex++) {
		uint16_t addrTrackStart = GetShortAddress(curOffset);
		if (addrTrackStart != addrSequenceEnd) {
			std::wstringstream trackName;
			trackName << L"Track Pointer " << (trackIndex + 1);
			header->AddSimpleItem(curOffset, 2, trackName.str().c_str());
		}
		else {
			header->AddSimpleItem(curOffset, 2, L"NULL");
		}
		curOffset += 2;
	}

	header->SetGuessedLength();

	return true;		//successful
}


bool AkaoSnesSeq::GetTrackPointers(void)
{
	uint32_t curOffset = dwOffset;
	if (version == AKAOSNES_V3) {
		if (minorVersion != AKAOSNES_V3_FFMQ) {
			curOffset += 2;
		}
	}
	else if (version == AKAOSNES_V4) {
		curOffset += 4;
	}

	for (uint8_t trackIndex = 0; trackIndex < MAX_TRACKS; trackIndex++) {
		uint16_t addrTrackStart = GetShortAddress(curOffset);
		if (addrTrackStart != addrSequenceEnd) {
			AkaoSnesTrack* track = new AkaoSnesTrack(this, addrTrackStart);
			aTracks.push_back(track);
		}
		curOffset += 2;
	}

	return true;
}

void AkaoSnesSeq::LoadEventMap(AkaoSnesSeq *pSeqFile)
{
	// TODO: AkaoSnesSeq::LoadEventMap
}

uint16_t AkaoSnesSeq::ROMAddressToAPUAddress(uint16_t romAddress)
{
	return (romAddress - addrROMRelocBase) + addrAPURelocBase;
}

uint16_t AkaoSnesSeq::GetShortAddress(uint32_t offset)
{
	return ROMAddressToAPUAddress(GetShort(offset));
}

//  ************
//  AkaoSnesTrack
//  ************

AkaoSnesTrack::AkaoSnesTrack(AkaoSnesSeq* parentFile, long offset, long length)
	: SeqTrack(parentFile, offset, length)
{
	ResetVars();
	bDetermineTrackLengthEventByEvent = true;
	bWriteGenericEventAsTextEvent = false;
}

void AkaoSnesTrack::ResetVars(void)
{
	SeqTrack::ResetVars();
}


bool AkaoSnesTrack::ReadEvent(void)
{
	AkaoSnesSeq* parentSeq = (AkaoSnesSeq*)this->parentSeq;

	uint32_t beginOffset = curOffset;
	if (curOffset >= 0x10000) {
		return false;
	}

	uint8_t statusByte = GetByte(curOffset++);
	bool bContinue = true;

	std::wstringstream desc;

	AkaoSnesSeqEventType eventType = (AkaoSnesSeqEventType)0;
	std::map<uint8_t, AkaoSnesSeqEventType>::iterator pEventType = parentSeq->EventMap.find(statusByte);
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
		pRoot->AddLogItem(new LogItem(std::wstring(L"Unknown Event - ") + desc.str(), LOG_LEVEL_ERR, std::wstring(L"AkaoSnesSeq")));
		bContinue = false;
		break;
	}

	//wostringstream ssTrace;
	//ssTrace << L"" << std::hex << std::setfill(L'0') << std::setw(8) << std::uppercase << beginOffset << L": " << std::setw(2) << (int)statusByte  << L" -> " << std::setw(8) << curOffset << std::endl;
	//OutputDebugString(ssTrace.str().c_str());

	return bContinue;
}

uint16_t AkaoSnesTrack::ROMAddressToAPUAddress(uint16_t romAddress)
{
	AkaoSnesSeq* parentSeq = (AkaoSnesSeq*)this->parentSeq;
	return parentSeq->ROMAddressToAPUAddress(romAddress);
}

uint16_t AkaoSnesTrack::GetShortAddress(uint32_t offset)
{
	return ROMAddressToAPUAddress(GetShort(offset));
}
