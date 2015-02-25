#pragma once
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "SeqEvent.h"
#include "IMaxSnesFormat.h"

enum IMaxSnesSeqEventType
{
	EVENT_UNKNOWN0 = 1, //start enum at 1 because if map[] look up fails, it returns 0, and we don't want that to get confused with a legit event
	EVENT_UNKNOWN1,
	EVENT_UNKNOWN2,
	EVENT_UNKNOWN3,
	EVENT_UNKNOWN4,
	EVENT_NOP2,
	EVENT_CONDITIONAL_JUMP,
	EVENT_CONDITION,
	EVENT_LOOP_UNTIL,
	EVENT_LOOP_UNTIL_ALT,
	EVENT_RET,
	EVENT_CALL,
	EVENT_GOTO,
};

class IMaxSnesSeq
	: public VGMSeq
{
public:
	IMaxSnesSeq(RawFile* file, IMaxSnesVersion ver, uint32_t seqdataOffset, std::wstring newName = L"I'Max SNES Seq");
	virtual ~IMaxSnesSeq(void);

	virtual bool GetHeaderInfo(void);
	virtual bool GetTrackPointers(void);
	virtual void ResetVars(void);

	IMaxSnesVersion version;
	std::map<uint8_t, IMaxSnesSeqEventType> EventMap;

	bool conditionSwitch;

private:
	void LoadEventMap(void);
};


class IMaxSnesTrack
	: public SeqTrack
{
public:
	IMaxSnesTrack(IMaxSnesSeq* parentFile, long offset = 0, long length = 0);
	virtual void ResetVars(void);
	virtual bool ReadEvent(void);

private:
	uint8_t loopCount;
	uint8_t loopCountAlt;
	uint16_t subReturnAddr;
};
