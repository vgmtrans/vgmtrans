#pragma once
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "SuzukiSnesFormat.h"

extern const uint8_t durtbl[14];

enum SuzukiSnesSeqEventType
{
	EVENT_UNKNOWN0 = 1, //start enum at 1 because if map[] look up fails, it returns 0, and we don't want that to get confused with a legit event
	EVENT_UNKNOWN1,
	EVENT_UNKNOWN2,
	EVENT_UNKNOWN3,
	EVENT_UNKNOWN4,
	EVENT_NOTE,
	EVENT_OCTAVE_UP,
	EVENT_OCTAVE_DOWN,
	EVENT_OCTAVE,
	EVENT_NOP,
	EVENT_NOISE_FREQ,
	EVENT_NOISE_ON,
	EVENT_NOISE_OFF,
	EVENT_PITCH_MOD_ON,
	EVENT_PITCH_MOD_OFF,
	EVENT_JUMP_TO_SFX_LO,
	EVENT_JUMP_TO_SFX_HI,
	EVENT_TUNING,
	EVENT_END,
	EVENT_TEMPO,
	EVENT_TIMER1_FREQ,
	EVENT_TIMER1_FREQ_REL,
	EVENT_LOOP_START,
	EVENT_LOOP_END,
	EVENT_LOOP_BREAK,
	EVENT_LOOP_POINT,
	EVENT_ADSR_DEFAULT,
	EVENT_ADSR_AR,
	EVENT_ADSR_DR,
	EVENT_ADSR_SL,
	EVENT_ADSR_SR,
	EVENT_DURATION_RATE,
	EVENT_PROGCHANGE,
	EVENT_NOISE_FREQ_REL,
	EVENT_VOLUME,
	EVENT_VOLUME_REL,
	EVENT_VOLUME_FADE,
	EVENT_PORTAMENTO,
	EVENT_PORTAMENTO_TOGGLE,
	EVENT_PAN,
	EVENT_PAN_FADE,
	EVENT_PAN_LFO_ON,
	EVENT_PAN_LFO_RESTART,
	EVENT_PAN_LFO_OFF,
	EVENT_TRANSPOSE_ABS,
	EVENT_TRANSPOSE_REL,
	EVENT_PERC_ON,
	EVENT_PERC_OFF,
	EVENT_VIBRATO_ON,
	EVENT_VIBRATO_ON_WITH_DELAY,
	EVENT_TEMPO_REL,
	EVENT_VIBRATO_OFF,
	EVENT_TREMOLO_ON,
	EVENT_TREMOLO_ON_WITH_DELAY,
	EVENT_TREMOLO_OFF,
	EVENT_SLUR_ON,
	EVENT_SLUR_OFF,
	EVENT_ECHO_ON,
	EVENT_ECHO_OFF,
	EVENT_CALL_SFX_LO,
	EVENT_CALL_SFX_HI,
};

class SuzukiSnesSeq
	: public VGMSeq
{
public:
	SuzukiSnesSeq(RawFile* file, SuzukiSnesVersion ver, uint32_t seqdataOffset, std::wstring newName = L"Square SUZUKI SNES Seq");
	virtual ~SuzukiSnesSeq(void);

	virtual bool GetHeaderInfo(void);
	virtual bool GetTrackPointers(void);
	virtual void ResetVars(void);

	SuzukiSnesVersion version;
	std::map<uint8_t, SuzukiSnesSeqEventType> EventMap;

private:
	void LoadEventMap(SuzukiSnesSeq *pSeqFile);
};


class SuzukiSnesTrack
	: public SeqTrack
{
public:
	SuzukiSnesTrack(SuzukiSnesSeq* parentFile, long offset = 0, long length = 0);
	virtual void ResetVars(void);
	virtual bool ReadEvent(void);

private:
	uint8_t rpt;
	uint8_t rptcnt[10];
	uint32_t rptpos[10];
	uint8_t rptcur[10];
	uint8_t rptoct[10];
};
