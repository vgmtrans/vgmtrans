#pragma once
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "SeqEvent.h"
#include "KonamiSnesFormat.h"

enum KonamiSnesSeqEventType
{
	EVENT_UNKNOWN0 = 1, //start enum at 1 because if map[] look up fails, it returns 0, and we don't want that to get confused with a legit event
	EVENT_UNKNOWN1,
	EVENT_UNKNOWN2,
	EVENT_UNKNOWN3,
	EVENT_UNKNOWN4,
};

class KonamiSnesSeq
	: public VGMSeq
{
public:
	KonamiSnesSeq(RawFile* file, KonamiSnesVersion ver, uint32_t seqdataOffset, std::wstring newName = L"Konami SNES Seq");
	virtual ~KonamiSnesSeq(void);

	virtual bool GetHeaderInfo(void);
	virtual bool GetTrackPointers(void);
	virtual void ResetVars(void);

	uint8_t tempo;

	KonamiSnesVersion version;
	std::map<uint8_t, KonamiSnesSeqEventType> EventMap;

	double GetTempoInBPM ();
	double GetTempoInBPM (uint8_t tempo);

private:
	void LoadEventMap(KonamiSnesSeq *pSeqFile);
};


class KonamiSnesTrack
	: public SeqTrack
{
public:
	KonamiSnesTrack(KonamiSnesSeq* parentFile, long offset = 0, long length = 0);
	virtual bool LoadTrackInit(uint32_t trackNum);
	virtual void ResetVars(void);
	virtual bool ReadEvent(void);
	virtual void OnTickBegin(void);
	virtual void OnTickEnd(void);
};
