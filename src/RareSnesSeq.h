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
	RareSnesSeq(RawFile* file, RareSnesVersion ver, ULONG seqdata_offset, wstring newName = L"Rare SNES Seq");
	virtual ~RareSnesSeq(void);

	virtual int GetHeaderInfo(void);
	virtual int GetTrackPointers(void);
	virtual void ResetVars(void);

	RareSnesVersion version;
	map<BYTE, RareSnesSeqEventType> EventMap;

	static const USHORT NOTE_PITCH_TABLE[128];  // note number to frequency table

	BYTE initialTempo;                          // initial tempo value written in header
	BYTE midiReverb;                            // MIDI reverb level for SPC700 echo
	BYTE timerFreq;                             // SPC700 timer 0 frequency (tempo base)
	BYTE tempo;                                 // song tempo
	S8 presetVolL[5];                         // volume preset L
	S8 presetVolR[5];                         // volume preset R
	USHORT presetADSR[5];                       // ADSR preset

	double GetTempoInBPM ();
	double GetTempoInBPM (BYTE tempo);
	double GetTempoInBPM (BYTE tempo, BYTE timerFreq);

private:
	void LoadEventMap(RareSnesSeq *pSeqFile);
};


class RareSnesTrack
	: public SeqTrack
{
public:
	RareSnesTrack(RareSnesSeq* parentFile, long offset = 0, long length = 0);
	virtual int LoadTrackInit(int trackNum);
	virtual void ResetVars(void);
	virtual int ReadEvent(void);

	void AddVolLR(ULONG offset, ULONG length, S8 spcVolL, S8 spcVolR, const wchar_t* sEventName = L"Volume L/R");
	void AddVolLRNoItem(S8 spcVolL, S8 spcVolR);

private:
	BYTE rptNestLevel;                          // nest level for repeat-subroutine command
	BYTE rptCount[RARESNES_RPTNESTMAX];         // repeat count for repeat-subroutine command
	ULONG rptStart[RARESNES_RPTNESTMAX];        // loop start address for repeat-subroutine command
	ULONG rptRetnAddr[RARESNES_RPTNESTMAX];     // return address for repeat-subroutine command
	USHORT spcNotePitch;                        // SPC700 pitch register value (0000-3fff), converter will need it for pitch slide
	S8 spcTranspose;                            // transpose (compatible with actual engine)
	S8 spcTransposeAbs;                         // transpose (without relative change)
	S8 spcTuning;                               // tuning (compatible with actual engine)
	S8 spcVolL, spcVolR;                      // SPC700 left/right volume
	USHORT defNoteDur;                          // default duration for note (0:unused)
	bool useLongDur;                            // indicates duration length
	BYTE altNoteByte1;                          // note number preset 1
	BYTE altNoteByte2;                          // note number preset 2

	void CalcVolPanFromVolLR(S8 volLByte, S8 volRByte, BYTE& midiVol, BYTE& midiPan);
};
