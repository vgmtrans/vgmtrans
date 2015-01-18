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

AkaoSnesSeq::AkaoSnesSeq(RawFile* file, AkaoSnesVersion ver, AkaoSnesMinorVersion minorVer, uint32_t seqdataOffset, std::wstring newName)
	: VGMSeq(AkaoSnesFormat::name, file, seqdataOffset, 0, newName), version(ver), minorVersion(minorVer)
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

	// TODO: AkaoSnesSeq::GetHeaderInfo

	return true;		//successful
}


bool AkaoSnesSeq::GetTrackPointers(void)
{
	// TODO: AkaoSnesSeq::GetTrackPointers
	return true;
}

void AkaoSnesSeq::LoadEventMap(AkaoSnesSeq *pSeqFile)
{
	// TODO: AkaoSnesSeq::LoadEventMap
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
