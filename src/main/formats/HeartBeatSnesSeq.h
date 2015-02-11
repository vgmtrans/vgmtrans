#pragma once
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "HeartBeatSnesFormat.h"

#define HEARTBEATSNES_SUBLEVEL_MAX   3

enum HeartBeatSnesSeqEventType
{
	EVENT_UNKNOWN0 = 1, //start enum at 1 because if map[] look up fails, it returns 0, and we don't want that to get confused with a legit event
	EVENT_UNKNOWN1,
	EVENT_UNKNOWN2,
	EVENT_UNKNOWN3,
	EVENT_UNKNOWN4,
};

class HeartBeatSnesSeq
	: public VGMSeq
{
public:
	HeartBeatSnesSeq(RawFile* file, HeartBeatSnesVersion ver, uint32_t seqdataOffset, std::wstring newName = L"HeartBeat SNES Seq");
	virtual ~HeartBeatSnesSeq(void);

	virtual bool GetHeaderInfo(void);
	virtual bool GetTrackPointers(void);
	virtual void ResetVars(void);

	double GetTempoInBPM(uint8_t tempo);

	HeartBeatSnesVersion version;
	std::map<uint8_t, HeartBeatSnesSeqEventType> EventMap;

private:
	void LoadEventMap(HeartBeatSnesSeq *pSeqFile);
};


class HeartBeatSnesTrack
	: public SeqTrack
{
public:
	HeartBeatSnesTrack(HeartBeatSnesSeq* parentFile, long offset = 0, long length = 0);
	virtual void ResetVars(void);
	virtual bool ReadEvent(void);
};
