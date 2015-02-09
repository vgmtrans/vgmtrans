#pragma once
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "ChunSnesFormat.h"

#define CHUNSNES_SUBLEVEL_MAX   3

enum ChunSnesSeqEventType
{
	EVENT_UNKNOWN0 = 1, //start enum at 1 because if map[] look up fails, it returns 0, and we don't want that to get confused with a legit event
	EVENT_UNKNOWN1,
	EVENT_UNKNOWN2,
	EVENT_UNKNOWN3,
	EVENT_UNKNOWN4,
	EVENT_NOP,
	EVENT_NOTE,
	EVENT_DURATION_FROM_TABLE,
	EVENT_LOOP_BREAK_ALT,
	EVENT_LOOP_AGAIN_ALT,
	EVENT_ADSR_RR,
	EVENT_ADSR_AND_RR,
	EVENT_CPU_CONTROLED_JUMP,
	EVENT_EXPRESSION_FADE,
	EVENT_PAN_FADE,
	EVENT_GOTO,
	EVENT_TEMPO,
	EVENT_DURATION_RATE,
	EVENT_VOLUME,
	EVENT_PAN,
	EVENT_ADSR,
	EVENT_PROGCHANGE,
	EVENT_SYNC_NOTE_LEN_ON,
	EVENT_SYNC_NOTE_LEN_OFF,
	EVENT_LOOP_AGAIN,
	EVENT_LOOP_UNTIL,
	EVENT_EXPRESSION,
	EVENT_CALL,
	EVENT_RET,
	EVENT_TRANSPOSE,
	EVENT_PITCH_SLIDE,
	EVENT_END,
};

class ChunSnesSeq
	: public VGMSeq
{
public:
	ChunSnesSeq(RawFile* file, ChunSnesVersion ver, ChunSnesMinorVersion minorVer, uint32_t seqdataOffset, std::wstring newName = L"Chun SNES Seq");
	virtual ~ChunSnesSeq(void);

	virtual bool GetHeaderInfo(void);
	virtual bool GetTrackPointers(void);
	virtual void ResetVars(void);

	double GetTempoInBPM(uint8_t tempo);

	ChunSnesVersion version;
	ChunSnesMinorVersion minorVersion;
	std::map<uint8_t, ChunSnesSeqEventType> EventMap;

	uint8_t initialTempo;

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

	void SyncNoteLengthWithPriorTrack(void);

	uint8_t index;

	uint8_t noteLength;
	uint8_t noteDurationRate;
	bool syncNoteLen;

	uint8_t loopCount;
	uint8_t loopCountAlt;
	uint8_t subNestLevel;
	uint16_t subReturnAddr[CHUNSNES_SUBLEVEL_MAX];
};
