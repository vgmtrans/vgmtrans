#pragma once
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "HudsonSnesFormat.h"

enum HudsonSnesSeqEventType
{
	EVENT_UNKNOWN0 = 1, //start enum at 1 because if map[] look up fails, it returns 0, and we don't want that to get confused with a legit event
	EVENT_UNKNOWN1,
	EVENT_UNKNOWN2,
	EVENT_UNKNOWN3,
	EVENT_UNKNOWN4,
};

class HudsonSnesSeq
	: public VGMSeq
{
public:
	HudsonSnesSeq(RawFile* file, HudsonSnesVersion ver, uint32_t seqdataOffset, std::wstring newName = L"Hudson SNES Seq");
	virtual ~HudsonSnesSeq(void);

	virtual bool GetHeaderInfo(void);
	virtual bool GetTrackPointers(void);
	virtual void ResetVars(void);

	HudsonSnesVersion version;
	std::map<uint8_t, HudsonSnesSeqEventType> EventMap;

private:
	void LoadEventMap(HudsonSnesSeq *pSeqFile);
};


class HudsonSnesTrack
	: public SeqTrack
{
public:
	HudsonSnesTrack(HudsonSnesSeq* parentFile, long offset = 0, long length = 0);
	virtual void ResetVars(void);
	virtual bool ReadEvent(void);
};
