#pragma once
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "ChunSnesFormat.h"

#define CHUNSNES_CALLSTACK_SIZE 10

enum ChunSnesSeqEventType
{
	EVENT_UNKNOWN0 = 1, //start enum at 1 because if map[] look up fails, it returns 0, and we don't want that to get confused with a legit event
	EVENT_UNKNOWN1,
	EVENT_UNKNOWN2,
	EVENT_UNKNOWN3,
	EVENT_UNKNOWN4,
};

class ChunSnesSeq
	: public VGMSeq
{
public:
	ChunSnesSeq(RawFile* file, ChunSnesVersion ver, uint32_t seqdataOffset, std::wstring newName = L"Chun SNES Seq");
	virtual ~ChunSnesSeq(void);

	virtual bool GetHeaderInfo(void);
	virtual bool GetTrackPointers(void);
	virtual void ResetVars(void);

	double GetTempoInBPM(uint8_t tempo, bool fastTempo);

	ChunSnesVersion version;
	std::map<uint8_t, ChunSnesSeqEventType> EventMap;

private:
	void LoadEventMap(ChunSnesSeq *pSeqFile);
};


class ChunSnesTrack
	: public SeqTrack
{
public:
	ChunSnesTrack(ChunSnesSeq* parentFile, long offset = 0, long length = 0);
	virtual void ResetVars(void);
	virtual bool ReadEvent(void);
};
