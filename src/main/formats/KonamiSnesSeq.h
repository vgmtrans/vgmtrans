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
	EVENT_UNKNOWN5,
	EVENT_NOTE,
	EVENT_PERCUSSION_ON,
	EVENT_PERCUSSION_OFF,
	EVENT_GAIN,
	EVENT_REST,
	EVENT_REST_WITH_DURATION,
	EVENT_PAN,
	EVENT_VIBRATO,
	EVENT_RANDOM_PITCH,
	EVENT_PROGCHANGE,
	EVENT_LOOP_START,
	EVENT_LOOP_END,
	EVENT_LOOP_START_2,
	EVENT_LOOP_END_2,
	EVENT_TEMPO,
	EVENT_TEMPO_FADE,
	EVENT_TRANSPABS,
	EVENT_ADSR1,
	EVENT_ADSR2,
	EVENT_VOLUME,
	EVENT_VOLUME_FADE,
	EVENT_PORTAMENTO,
	EVENT_PITCH_ENVELOPE,
	EVENT_TUNING,
	EVENT_PITCH_SLIDE,
	EVENT_ECHO,
	EVENT_ECHO_PARAM,
	EVENT_LOOP_WITH_VOLTA_START,
	EVENT_LOOP_WITH_VOLTA_END,
	EVENT_PAN_FADE,
	EVENT_VIBRATO_FADE,
	EVENT_ADSR_GAIN,
	EVENT_PROGCHANGEVOL,
	EVENT_GOTO,
	EVENT_CALL,
	EVENT_END,
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

	static const uint8_t panTable[];

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
	virtual void ResetVars(void);
	virtual bool ReadEvent(void);
	virtual void OnTickBegin(void);
	virtual void OnTickEnd(void);

	uint8_t noteLength;
	uint8_t noteDurationRate;
	bool inSubroutine;
	uint16_t subReturnAddr;
	uint16_t loopReturnAddr;
	uint8_t loopCount;
	int16_t loopVolumeDelta;
	int16_t loopPitchDelta;
	uint16_t loopReturnAddr2;
	uint8_t loopCount2;
	int16_t loopVolumeDelta2;
	int16_t loopPitchDelta2;
	uint16_t voltaLoopStart;
	uint16_t voltaLoopEnd;
	bool voltaEndMeansPlayFromStart;
	bool voltaEndMeansPlayNextVolta;
	bool percussion;
	uint8_t instrument;

private:
	double GetTuningInSemitones(int8_t tuning);
	uint8_t ConvertGAINAmountToGAIN(uint8_t gainAmount);
};
