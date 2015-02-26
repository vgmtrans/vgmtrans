#include "stdafx.h"
#include "IMaxSnesSeq.h"
#include "IMaxSnesFormat.h"
#include "ScaleConversion.h"

DECLARE_FORMAT(IMaxSnes);

//  ***********
//  IMaxSnesSeq
//  ***********
#define MAX_TRACKS  16
#define SEQ_PPQN    48

IMaxSnesSeq::IMaxSnesSeq(RawFile* file, IMaxSnesVersion ver, uint32_t seqdataOffset, std::wstring newName)
	: VGMSeq(IMaxSnesFormat::name, file, seqdataOffset, 0, newName), version(ver)
{
	bLoadTickByTick = true;
	bAllowDiscontinuousTrackData = true;
	bUseLinearAmplitudeScale = true;

	UseReverb();
	AlwaysWriteInitialReverb(0);

	LoadEventMap();
}

IMaxSnesSeq::~IMaxSnesSeq(void)
{
}

void IMaxSnesSeq::ResetVars(void)
{
	VGMSeq::ResetVars();

	conditionSwitch = false;
}

bool IMaxSnesSeq::GetHeaderInfo(void)
{
	SetPPQN(SEQ_PPQN);

	VGMHeader* header = AddHeader(dwOffset, 0);

	uint32_t curOffset = dwOffset;
	for (uint8_t trackIndex = 0; trackIndex < MAX_TRACKS + 1; trackIndex++) {
		if (curOffset + 1 > 0x10000) {
			return false;
		}

		uint8_t channel = GetByte(curOffset);
		if (channel >= 0x80) {
			header->AddSimpleItem(curOffset, 1, L"Header End");
			break;
		}
		if (trackIndex >= MAX_TRACKS) {
			return false;
		}

		std::wstringstream trackName;
		trackName << L"Track " << (trackIndex + 1);
		VGMHeader* trackHeader = header->AddHeader(curOffset, 4, trackName.str());

		trackHeader->AddSimpleItem(curOffset, 1, L"Channel");
		curOffset++;

		uint8_t a01 = GetByte(curOffset);
		trackHeader->AddUnknownItem(curOffset, 1);
		curOffset++;

		uint16_t addrTrackStart = GetShort(curOffset);
		trackHeader->AddSimpleItem(curOffset, 2, L"Track Pointer");
		curOffset += 2;

		IMaxSnesTrack* track = new IMaxSnesTrack(this, addrTrackStart);
		aTracks.push_back(track);
	}

	return true;
}

bool IMaxSnesSeq::GetTrackPointers(void)
{
	return true;
}

void IMaxSnesSeq::LoadEventMap()
{
	int statusByte;

	for (statusByte = 0x00; statusByte <= 0x7f; statusByte++) {
		//EventMap[statusByte] = EVENT_XXX;
	}

	for (statusByte = 0x80; statusByte <= 0x9f; statusByte++) {
		//EventMap[statusByte] = EVENT_XXX;
	}

	EventMap[0xc0] = EVENT_UNKNOWN1;
	EventMap[0xc1] = EVENT_UNKNOWN1;
	EventMap[0xc2] = EVENT_UNKNOWN1;
	EventMap[0xc3] = EVENT_UNKNOWN1;
	EventMap[0xc4] = EVENT_UNKNOWN1;
	EventMap[0xc5] = EVENT_CONDITIONAL_JUMP;
	EventMap[0xc6] = EVENT_CONDITION;
	EventMap[0xc7] = EVENT_UNKNOWN1;
	EventMap[0xc8] = EVENT_UNKNOWN2;
	EventMap[0xc9] = EVENT_UNKNOWN2;
	EventMap[0xca] = EVENT_UNKNOWN0;
	EventMap[0xcb] = EVENT_UNKNOWN0;
	EventMap[0xcc] = EVENT_UNKNOWN1;
	EventMap[0xcd] = EVENT_UNKNOWN0;
	EventMap[0xce] = EVENT_UNKNOWN0;
	EventMap[0xcf] = EVENT_UNKNOWN2;
	EventMap[0xd0] = EVENT_UNKNOWN0;
	EventMap[0xd1] = EVENT_UNKNOWN0;
	EventMap[0xd2] = EVENT_UNKNOWN0;
	EventMap[0xd3] = EVENT_UNKNOWN0;
	EventMap[0xd4] = EVENT_UNKNOWN0;
	EventMap[0xd5] = EVENT_UNKNOWN1;
	EventMap[0xd6] = EVENT_UNKNOWN1;
	EventMap[0xd7] = EVENT_UNKNOWN1;
	EventMap[0xd8] = EVENT_UNKNOWN1;
	EventMap[0xd9] = EVENT_UNKNOWN3;
	EventMap[0xda] = EVENT_UNKNOWN2;
	EventMap[0xdb] = EVENT_NOP2;
	EventMap[0xdc] = EVENT_UNKNOWN0;
	EventMap[0xdd] = EVENT_UNKNOWN1;
	EventMap[0xde] = EVENT_LOOP_UNTIL;
	EventMap[0xdf] = EVENT_LOOP_UNTIL_ALT;
	EventMap[0xe0] = EVENT_RET;
	EventMap[0xe1] = EVENT_CALL;
	EventMap[0xe2] = EVENT_GOTO;
	EventMap[0xe3] = EVENT_UNKNOWN1;
	EventMap[0xe4] = EVENT_UNKNOWN1;
	EventMap[0xe5] = EVENT_UNKNOWN1;
	EventMap[0xe6] = EVENT_UNKNOWN0;
	EventMap[0xe7] = EVENT_UNKNOWN3;
	EventMap[0xe8] = EVENT_UNKNOWN1;
	EventMap[0xe9] = EVENT_UNKNOWN2;
	EventMap[0xea] = EVENT_UNKNOWN1;
	EventMap[0xeb] = EVENT_UNKNOWN1;
	EventMap[0xec] = EVENT_UNKNOWN1;
	EventMap[0xed] = EVENT_UNKNOWN3;
	//EventMap[0xee] = EVENT_UNKNOWN_VARIABLE;
	EventMap[0xef] = EVENT_UNKNOWN2;
	EventMap[0xf0] = EVENT_UNKNOWN2;
	EventMap[0xf1] = EVENT_UNKNOWN0;
	EventMap[0xf2] = EVENT_UNKNOWN0;
	EventMap[0xf3] = EVENT_UNKNOWN1;
	EventMap[0xf4] = EVENT_UNKNOWN0;
	EventMap[0xf5] = EVENT_UNKNOWN0;
	EventMap[0xf6] = EVENT_UNKNOWN2;
	EventMap[0xf7] = EVENT_UNKNOWN2;
	EventMap[0xf8] = EVENT_UNKNOWN3;
	EventMap[0xf9] = EVENT_UNKNOWN0;
	EventMap[0xfa] = EVENT_UNKNOWN0;
	EventMap[0xfb] = EVENT_UNKNOWN4;
	//EventMap[0xfc] = EVENT_UNKNOWN_VARIABLE;
	EventMap[0xfd] = EVENT_UNKNOWN2;
	EventMap[0xfe] = EVENT_UNKNOWN1;
	EventMap[0xff] = EVENT_UNKNOWN0;
}


//  *************
//  IMaxSnesTrack
//  *************

IMaxSnesTrack::IMaxSnesTrack(IMaxSnesSeq* parentFile, long offset, long length)
: SeqTrack(parentFile, offset, length)
{
	ResetVars();
	bDetermineTrackLengthEventByEvent = true;
	bWriteGenericEventAsTextEvent = false;
}

void IMaxSnesTrack::ResetVars(void)
{
	SeqTrack::ResetVars();

	loopCount = 0;
	loopCountAlt = 0;
	subReturnAddr = 0;
}

bool IMaxSnesTrack::ReadEvent(void)
{
	IMaxSnesSeq* parentSeq = (IMaxSnesSeq*)this->parentSeq;

	uint32_t beginOffset = curOffset;
	if (curOffset >= 0x10000) {
		return false;
	}

	uint8_t statusByte = GetByte(curOffset++);
	bool bContinue = true;

	std::wstringstream desc;

	IMaxSnesSeqEventType eventType = (IMaxSnesSeqEventType)0;
	std::map<uint8_t, IMaxSnesSeqEventType>::iterator pEventType = parentSeq->EventMap.find(statusByte);
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

	case EVENT_NOP2:
	{
		curOffset += 2;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"NOP", desc.str(), CLR_MISC, ICON_BINARY);
		break;
	}

	case EVENT_CONDITIONAL_JUMP:
	{
		uint16_t dest = GetShort(curOffset); curOffset += 2;
		desc << L"Destination: $" << std::hex << std::setfill(L'0') << std::setw(4) << std::uppercase << (int)dest;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Conditional Jump", desc.str(), CLR_LOOP);

		if (parentSeq->conditionSwitch) {
			curOffset = dest;
		}
		break;
	}

	case EVENT_CONDITION:
	{
		parentSeq->conditionSwitch = true;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Condition On", desc.str(), CLR_CHANGESTATE);
		break;
	}

	case EVENT_LOOP_UNTIL:
	{
		uint8_t count = GetByte(curOffset);
		uint16_t dest = GetShort(curOffset); curOffset += 2;
		desc << L"Loop Count: " << count << L"  Destination: $" << std::hex << std::setfill(L'0') << std::setw(4) << std::uppercase << (int)dest;
		AddGenericEvent(beginOffset, 2, L"Loop Until", desc.str(), CLR_LOOP, ICON_ENDREP);

		bool doJump;
		if (loopCount == 0) {
			loopCount = count;
			doJump = true;
		}
		else {
			loopCount--;
			if (loopCount != 0) {
				doJump = true;
			}
			else {
				doJump = false;
			}
		}

		if (doJump) {
			curOffset = dest;
		}

		break;
	}

	case EVENT_LOOP_UNTIL_ALT:
	{
		uint8_t count = GetByte(curOffset);
		uint16_t dest = GetShort(curOffset); curOffset += 2;
		desc << L"Loop Count: " << count << L"  Destination: $" << std::hex << std::setfill(L'0') << std::setw(4) << std::uppercase << (int)dest;
		AddGenericEvent(beginOffset, 2, L"Loop Until (Alt)", desc.str(), CLR_LOOP, ICON_ENDREP);

		bool doJump;
		if (loopCountAlt == 0) {
			loopCountAlt = count;
			doJump = true;
		}
		else {
			loopCountAlt--;
			if (loopCountAlt != 0) {
				doJump = true;
			}
			else {
				doJump = false;
			}
		}

		if (doJump) {
			curOffset = dest;
		}

		break;
	}

	case EVENT_RET:
	{
		AddGenericEvent(beginOffset, 2, L"Pattern End", desc.str(), CLR_LOOP, ICON_ENDREP);
		curOffset = subReturnAddr;
		break;
	}

	case EVENT_CALL:
	{
		uint16_t dest = GetShort(curOffset); curOffset += 2;
		desc << L"Destination: $" << std::hex << std::setfill(L'0') << std::setw(4) << std::uppercase << (int)dest;
		AddGenericEvent(beginOffset, 2, L"Pattern Play", desc.str(), CLR_LOOP, ICON_STARTREP);

		subReturnAddr = curOffset;
		curOffset = dest;

		break;
	}

	case EVENT_GOTO:
	{
		uint16_t dest = GetShort(curOffset); curOffset += 2;
		desc << L"Destination: $" << std::hex << std::setfill(L'0') << std::setw(4) << std::uppercase << (int)dest;
		uint32_t length = curOffset - beginOffset;

		curOffset = dest;
		if (!IsOffsetUsed(dest)) {
			AddGenericEvent(beginOffset, length, L"Jump", desc.str(), CLR_LOOPFOREVER);
		}
		else {
			bContinue = AddLoopForever(beginOffset, length, L"Jump");
		}
		break;
	}

	default:
		desc << L"Event: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << (int)statusByte;
		AddUnknown(beginOffset, curOffset-beginOffset, L"Unknown Event", desc.str());
		pRoot->AddLogItem(new LogItem(std::wstring(L"Unknown Event - ") + desc.str(), LOG_LEVEL_ERR, std::wstring(L"IMaxSnesSeq")));
		bContinue = false;
		break;
	}

	//std::wostringstream ssTrace;
	//ssTrace << L"" << std::hex << std::setfill(L'0') << std::setw(8) << std::uppercase << beginOffset << L": " << std::setw(2) << (int)statusByte  << L" -> " << std::setw(8) << curOffset << std::endl;
	//OutputDebugString(ssTrace.str().c_str());

	return bContinue;
}
