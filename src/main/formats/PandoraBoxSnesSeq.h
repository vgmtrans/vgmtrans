#pragma once
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "SeqEvent.h"
#include "PandoraBoxSnesFormat.h"

#define PANDORABOXSNES_CALLSTACK_SIZE 2
#define PANDORABOXSNES_LOOP_LEVEL_MAX 4

enum PandoraBoxSnesSeqEventType
{
	EVENT_UNKNOWN0 = 1, //start enum at 1 because if map[] look up fails, it returns 0, and we don't want that to get confused with a legit event
	EVENT_UNKNOWN1,
	EVENT_UNKNOWN2,
	EVENT_UNKNOWN3,
	EVENT_UNKNOWN4,
	EVENT_NOTE,
	EVENT_INSTANT_VOLUME,
	EVENT_INSTANT_OCTAVE,
	EVENT_TRANSPOSE,
	EVENT_MASTER_VOLUME,
	EVENT_ECHO_VOLUME,
	EVENT_DEC_OCTAVE,
	EVENT_INC_OCTAVE,
	EVENT_LOOP_BREAK,
	EVENT_LOOP_START,
	EVENT_LOOP_END,
	EVENT_DURATION_RATE,
	EVENT_DSP_WRITE,
	EVENT_LOOP_AGAIN_NO_NEST,
	EVENT_NOISE_TOGGLE,
	EVENT_VOLUME,
	EVENT_MASTER_VOLUME_FADE,
	EVENT_PAN,
	EVENT_ADSR,
	EVENT_RET,
	EVENT_CALL,
	EVENT_GOTO,
	EVENT_PROGCHANGE,
	EVENT_DEFAULT_LENGTH,
	EVENT_END,
};

class PandoraBoxSnesSeq
	: public VGMSeq
{
public:
	PandoraBoxSnesSeq(RawFile* file, PandoraBoxSnesVersion ver, uint32_t seqdata_offset, std::wstring newName = L"PandoraBox SNES Seq");
	virtual ~PandoraBoxSnesSeq(void);

	virtual bool GetHeaderInfo(void);
	virtual bool GetTrackPointers(void);
	virtual void ResetVars(void);

	PandoraBoxSnesVersion version;
	std::map<uint8_t, PandoraBoxSnesSeqEventType> EventMap;

private:
	void LoadEventMap(void);
};


class PandoraBoxSnesTrack
	: public SeqTrack
{
public:
	PandoraBoxSnesTrack(PandoraBoxSnesSeq* parentFile, long offset = 0, long length = 0);
	virtual void ResetVars(void);
	virtual bool ReadEvent(void);
};
