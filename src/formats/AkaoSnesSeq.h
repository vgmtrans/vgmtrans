#pragma once
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "AkaoSnesFormat.h"

enum AkaoSnesSeqEventType
{
	EVENT_UNKNOWN0 = 1, //start enum at 1 because if map[] look up fails, it returns 0, and we don't want that to get confused with a legit event
	EVENT_UNKNOWN1,
	EVENT_UNKNOWN2,
	EVENT_UNKNOWN3,
	EVENT_UNKNOWN4,
	EVENT_VOLUME,
	EVENT_VOLUME_FADE,
	EVENT_PAN,
	EVENT_PAN_FADE,
	EVENT_PITCH_SLIDE,
	EVENT_VIBRATO_ON,
	EVENT_VIBRATO_OFF,
	EVENT_TREMOLO_ON,
	EVENT_TREMOLO_OFF,
	EVENT_PAN_LFO_ON,
	EVENT_PAN_LFO_OFF,
	EVENT_NOISE_FREQ,
	EVENT_NOISE_ON,
	EVENT_NOISE_OFF,
	EVENT_PITCHMOD_ON,
	EVENT_PITCHMOD_OFF,
	EVENT_ECHO_ON,
	EVENT_ECHO_OFF,
	EVENT_OCTAVE,
	EVENT_OCTAVE_UP,
	EVENT_OCTAVE_DOWN,
	EVENT_TRANSPOSE_ABS,
	EVENT_TRANSPOSE_REL,
	EVENT_DETUNE,
	EVENT_PROGCHANGE,
	EVENT_ADSR_AR,
	EVENT_ADSR_DR,
	EVENT_ADSR_SL,
	EVENT_ADSR_SR,
	EVENT_ADSR_DEFAULT,
	EVENT_LOOP_START,
	EVENT_LOOP_END,
};

class AkaoSnesSeq
	: public VGMSeq
{
public:
	AkaoSnesSeq(RawFile* file, AkaoSnesVersion ver, AkaoSnesMinorVersion minorVer, uint32_t seqdataOffset, uint32_t addrAPURelocBase, std::wstring newName = L"Square SNES Seq");
	virtual ~AkaoSnesSeq(void);

	virtual bool GetHeaderInfo(void);
	virtual bool GetTrackPointers(void);
	virtual void ResetVars(void);

	uint16_t ROMAddressToAPUAddress(uint16_t romAddress);
	uint16_t GetShortAddress(uint32_t offset);

	AkaoSnesVersion version;
	AkaoSnesMinorVersion minorVersion;
	std::map<uint8_t, AkaoSnesSeqEventType> EventMap;
	uint32_t addrAPURelocBase;
	uint32_t addrROMRelocBase;
	uint32_t addrSequenceEnd;

private:
	void LoadEventMap(AkaoSnesSeq *pSeqFile);
};


class AkaoSnesTrack
	: public SeqTrack
{
public:
	AkaoSnesTrack(AkaoSnesSeq* parentFile, long offset = 0, long length = 0);
	virtual void ResetVars(void);
	virtual bool ReadEvent(void);

	uint16_t ROMAddressToAPUAddress(uint16_t romAddress);
	uint16_t GetShortAddress(uint32_t offset);
};
