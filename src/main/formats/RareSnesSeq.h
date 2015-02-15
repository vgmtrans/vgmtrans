#pragma once
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "SeqEvent.h"
#include "RareSnesFormat.h"

#define RARESNES_RPTNESTMAX 8

enum RareSnesSeqEventType
{
	EVENT_UNKNOWN0 = 1, //start enum at 1 because if map[] look up fails, it returns 0, and we don't want that to get confused with a legit event
	EVENT_UNKNOWN1,
	EVENT_UNKNOWN2,
	EVENT_UNKNOWN3,
	EVENT_UNKNOWN4,
	//EVENT_NOTE,
	//EVENT_REST,
	EVENT_END,
	EVENT_PROGCHANGE,
	EVENT_PROGCHANGEVOL,
	EVENT_VOLLR,
	EVENT_VOLCENTER,
	EVENT_GOTO,
	EVENT_CALLNTIMES,
	EVENT_CALLONCE,
	EVENT_RET,
	EVENT_DEFDURON,
	EVENT_DEFDUROFF,
	EVENT_PITCHSLIDEUP,
	EVENT_PITCHSLIDEDOWN,
	EVENT_PITCHSLIDEOFF,
	EVENT_TEMPO,
	EVENT_TEMPOADD,
	EVENT_VIBRATOSHORT,
	EVENT_VIBRATOOFF,
	EVENT_VIBRATO,
	EVENT_TREMOLOOFF,
	EVENT_TREMOLO,
	EVENT_ADSR,
	EVENT_MASTVOL,
	EVENT_MASTVOLLR,
	EVENT_TUNING,
	EVENT_TRANSPABS,
	EVENT_TRANSPREL,
	EVENT_ECHOPARAM,
	EVENT_ECHOON,
	EVENT_ECHOOFF,
	EVENT_ECHOFIR,
	EVENT_NOISECLK,
	EVENT_NOISEON,
	EVENT_NOISEOFF,
	EVENT_SETALTNOTE1,
	EVENT_SETALTNOTE2,
	EVENT_PITCHSLIDEDOWNSHORT,
	EVENT_PITCHSLIDEUPSHORT,
	EVENT_LONGDURON,
	EVENT_LONGDUROFF,
	EVENT_SETVOLADSRPRESET1,
	EVENT_SETVOLADSRPRESET2,
	EVENT_SETVOLADSRPRESET3,
	EVENT_SETVOLADSRPRESET4,
	EVENT_SETVOLADSRPRESET5,
	EVENT_GETVOLADSRPRESET1,
	EVENT_GETVOLADSRPRESET2,
	EVENT_GETVOLADSRPRESET3,
	EVENT_GETVOLADSRPRESET4,
	EVENT_GETVOLADSRPRESET5,
	EVENT_TIMERFREQ,
	EVENT_CONDJUMP,
	EVENT_SETCONDJUMPPARAM,
	EVENT_RESETADSR,
	EVENT_RESETADSRSOFT,
	EVENT_VOICEPARAMSHORT,
	EVENT_VOICEPARAM,
	EVENT_ECHODELAY,
	EVENT_SETVOLPRESETS,
	EVENT_GETVOLPRESET1,
	EVENT_GETVOLPRESET2,
	EVENT_LFOOFF,
};

class RareSnesSeq
	: public VGMSeq
{
public:
	RareSnesSeq(RawFile* file, RareSnesVersion ver, uint32_t seqdata_offset, std::wstring newName = L"Rare SNES Seq");
	virtual ~RareSnesSeq(void);

	virtual bool GetHeaderInfo(void);
	virtual bool GetTrackPointers(void);
	virtual void ResetVars(void);

	RareSnesVersion version;
	std::map<uint8_t, RareSnesSeqEventType> EventMap;

	std::map<uint8_t, int8_t> instrUnityKeyHints;
	std::map<uint8_t, int16_t> instrPitchHints;
	std::map<uint8_t, uint16_t> instrADSRHints;

	static const uint16_t NOTE_PITCH_TABLE[128];  // note number to frequency table

	uint8_t initialTempo;                          // initial tempo value written in header
	uint8_t midiReverb;                            // MIDI reverb level for SPC700 echo
	uint8_t timerFreq;                             // SPC700 timer 0 frequency (tempo base)
	uint8_t tempo;                                 // song tempo
	int8_t presetVolL[5];                           // volume preset L
	int8_t presetVolR[5];                           // volume preset R
	uint16_t presetADSR[5];                       // ADSR preset

	double GetTempoInBPM ();
	double GetTempoInBPM (uint8_t tempo);
	double GetTempoInBPM (uint8_t tempo, uint8_t timerFreq);

private:
	void LoadEventMap(void);
};


class RareSnesTrack
	: public SeqTrack
{
public:
	RareSnesTrack(RareSnesSeq* parentFile, long offset = 0, long length = 0);
	virtual void ResetVars(void);
	virtual bool ReadEvent(void);
	virtual void OnTickBegin(void);
	virtual void OnTickEnd(void);

	void AddVolLR(uint32_t offset, uint32_t length, int8_t spcVolL, int8_t spcVolR, const wchar_t* sEventName = L"Volume L/R");
	void AddVolLRNoItem(int8_t spcVolL, int8_t spcVolR);

private:
	uint8_t rptNestLevel;                          // nest level for repeat-subroutine command
	uint8_t rptCount[RARESNES_RPTNESTMAX];         // repeat count for repeat-subroutine command
	uint32_t rptStart[RARESNES_RPTNESTMAX];        // loop start address for repeat-subroutine command
	uint32_t rptRetnAddr[RARESNES_RPTNESTMAX];     // return address for repeat-subroutine command
	uint16_t spcNotePitch;                        // SPC700 pitch register value (0000-3fff), converter will need it for pitch slide
	int8_t spcTranspose;                            // transpose (compatible with actual engine)
	int8_t spcTransposeAbs;                         // transpose (without relative change)
	int8_t spcTuning;                               // tuning (compatible with actual engine)
	int8_t spcVolL, spcVolR;                        // SPC700 left/right volume
	uint8_t spcInstr;                              // SPC700 instrument index (NOT SRCN value)
	uint16_t spcADSR;                             // SPC700 ADSR value
	uint16_t defNoteDur;                          // default duration for note (0:unused)
	bool useLongDur;                            // indicates duration length
	uint8_t altNoteByte1;                          // note number preset 1
	uint8_t altNoteByte2;                          // note number preset 2

	double GetTuningInSemitones(int8_t tuning);
	void CalcVolPanFromVolLR(int8_t volLByte, int8_t volRByte, uint8_t& midiVol, uint8_t& midiPan);
};
