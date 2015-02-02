#pragma once
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "HudsonSnesFormat.h"

#define HUDSONSNES_CALLSTACK_SIZE   0x10
#define HUDSONSNES_USERRAM_SIZE     0x08

enum HudsonSnesSeqEventType
{
	EVENT_UNKNOWN0 = 1, //start enum at 1 because if map[] look up fails, it returns 0, and we don't want that to get confused with a legit event
	EVENT_UNKNOWN1,
	EVENT_UNKNOWN2,
	EVENT_UNKNOWN3,
	EVENT_UNKNOWN4,
	EVENT_NOP,
	EVENT_NOP1,
	EVENT_NOTE,
	EVENT_TEMPO,
	EVENT_OCTAVE,
	EVENT_OCTAVE_UP,
	EVENT_OCTAVE_DOWN,
	EVENT_QUANTIZE,
	EVENT_PROGCHANGE,
	EVENT_VOLUME,
	EVENT_PAN,
	EVENT_REVERSE_PHASE,
	EVENT_VOLUME_REL,
	EVENT_LOOP_START,
	EVENT_LOOP_END,
	EVENT_SUBROUTINE,
	EVENT_GOTO,
	EVENT_TUNING,
	EVENT_VIBRATO,
	EVENT_VIBRATO_DELAY,
	EVENT_ECHO_VOLUME,
	EVENT_ECHO_PARAM,
	EVENT_ECHO_ON,
	EVENT_TRANSPOSE_ABS,
	EVENT_TRANSPOSE_REL,
	EVENT_PITCH_ATTACK_ENV_ON,
	EVENT_PITCH_ATTACK_ENV_OFF,
	EVENT_LOOP_POINT,
	EVENT_JUMP_TO_LOOP_POINT,
	EVENT_LOOP_POINT_ONCE,
	EVENT_VOLUME_FROM_TABLE,
	EVENT_PORTAMENTO,
	EVENT_SUBEVENT,
	EVENT_END,
};

enum HudsonSnesSeqSubEventType
{
	SUBEVENT_UNKNOWN0 = 1, //start enum at 1 because if map[] look up fails, it returns 0, and we don't want that to get confused with a legit event
	SUBEVENT_UNKNOWN1,
	SUBEVENT_UNKNOWN2,
	SUBEVENT_UNKNOWN3,
	SUBEVENT_UNKNOWN4,
	SUBEVENT_NOP,
	SUBEVENT_END,
	SUBEVENT_ECHO_OFF,
	SUBEVENT_PERC_ON,
	SUBEVENT_PERC_OFF,
	SUBEVENT_VIBRATO_TYPE,
	SUBEVENT_MOV_IMM,
	SUBEVENT_MOV,
	SUBEVENT_CMP_IMM,
	SUBEVENT_CMP,
	SUBEVENT_BNE,
	SUBEVENT_BEQ,
	SUBEVENT_BCS,
	SUBEVENT_BCC,
	SUBEVENT_BMI,
	SUBEVENT_BPL,
	SUBEVENT_ADSR_AR,
	SUBEVENT_ADSR_DR,
	SUBEVENT_ADSR_SL,
	SUBEVENT_ADSR_SR,
	SUBEVENT_ADSR_RR,
};

enum HudsonSnesSeqHeaderEventType
{
	HEADER_EVENT_END = 1,
	HEADER_EVENT_TIMEBASE,
	HEADER_EVENT_TRACKS,
	HEADER_EVENT_INSTRUMENTS_V1,
	HEADER_EVENT_PERCUSSIONS_V1,
	HEADER_EVENT_INSTRUMENTS_V2,
	HEADER_EVENT_PERCUSSIONS_V2,
	HEADER_EVENT_05,
	HEADER_EVENT_06,
	HEADER_EVENT_ECHO_PARAM,
	HEADER_EVENT_08,
	HEADER_EVENT_09,
};

class HudsonSnesSeq
	: public VGMSeq
{
public:
	HudsonSnesSeq(RawFile* file, HudsonSnesVersion ver, uint32_t seqdataOffset, std::wstring newName = L"Hudson SNES Seq");
	virtual ~HudsonSnesSeq(void);

	virtual bool GetHeaderInfo(void);
	bool GetTrackPointersInHeaderInfo(VGMHeader* header, uint32_t & offset);
	virtual bool GetTrackPointers(void);
	virtual void ResetVars(void);

	HudsonSnesVersion version;
	std::map<uint8_t, HudsonSnesSeqEventType> EventMap;
	std::map<uint8_t, HudsonSnesSeqSubEventType> SubEventMap;
	std::map<uint8_t, HudsonSnesSeqHeaderEventType> HeaderEventMap;

	uint8_t TimebaseShift;
	uint8_t TrackAvailableBits;
	uint16_t TrackAddresses[8];
	uint16_t InstrumentTableAddress;
	uint8_t InstrumentTableSize;
	uint16_t PercussionTableAddress;
	uint8_t PercussionTableSize;

	uint8_t UserRAM[HUDSONSNES_USERRAM_SIZE];
	uint8_t UserCmpReg;
	bool UserCarry;

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

private:
	uint16_t infiniteLoopPoint;
	bool loopPointOnceProcessed;
	uint8_t spcNoteQuantize;
	uint8_t spcVolume;
	uint8_t spcCallStack[HUDSONSNES_CALLSTACK_SIZE]; // shared by multiple commands
	uint8_t spcCallStackPtr;
};
