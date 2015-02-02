#pragma once
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "MintSnesFormat.h"

enum MintSnesSeqEventType
{
	EVENT_UNKNOWN0 = 1, //start enum at 1 because if map[] look up fails, it returns 0, and we don't want that to get confused with a legit event
	EVENT_UNKNOWN1,
	EVENT_UNKNOWN2,
	EVENT_UNKNOWN3,
	EVENT_UNKNOWN4,
};

class MintSnesSeq
	: public VGMSeq
{
public:
	MintSnesSeq(RawFile* file, MintSnesVersion ver, uint32_t seqdataOffset, std::wstring newName = L"Mint SNES Seq");
	virtual ~MintSnesSeq(void);

	virtual bool GetHeaderInfo(void);
	virtual bool GetTrackPointers(void);
	virtual void ResetVars(void);

	MintSnesVersion version;
	std::map<uint8_t, MintSnesSeqEventType> EventMap;

	uint16_t TrackStartAddress[10];

private:
	void LoadEventMap(MintSnesSeq *pSeqFile);
};


class MintSnesTrack
	: public SeqTrack
{
public:
	MintSnesTrack(MintSnesSeq* parentFile, long offset = 0, long length = 0);
	virtual void ResetVars(void);
	virtual bool ReadEvent(void);
};
