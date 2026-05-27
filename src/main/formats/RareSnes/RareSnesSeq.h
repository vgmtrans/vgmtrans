#pragma once

#include "base/Types.h"
#include "RareSnesFormat.h"
#include "SeqEvent.h"
#include "SeqTrack.h"
#include "VGMSeq.h"

#include <map>
#include <string>

#define RARESNES_RPTNESTMAX 8

enum RareSnesSeqEventType {
  //start enum at 1 because if map[] look up fails, it returns 0, and we don't want that to get confused with a legit event
  EVENT_UNKNOWN0 = 1,
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
    : public VGMSeq {
 public:
  RareSnesSeq(RawFile *file, RareSnesVersion ver, u32 seqdata_offset, std::string newName = "Rare SNES Seq");
  virtual ~RareSnesSeq();

  virtual bool parseHeader();
  virtual bool parseTrackPointers();
  virtual void resetVars();

  RareSnesVersion version;
  std::map<u8, RareSnesSeqEventType> EventMap;

  std::map<u8, s8> instrUnityKeyHints;
  std::map<u8, s16> instrPitchHints;
  std::map<u8, u16> instrADSRHints;

  static const u16 NOTE_PITCH_TABLE[128];  // note number to frequency table

  u8 initialTempo;                          // initial tempo value written in header
  u8 midiReverb;                            // MIDI reverb level for SPC700 echo
  u8 timerFreq;                             // SPC700 timer 0 frequency (tempo base)
  u8 tempo;                                 // song tempo
  s8 presetVolL[5];                           // volume preset L
  s8 presetVolR[5];                           // volume preset R
  u16 presetADSR[5];                       // ADSR preset

  double getTempoInBPM(u8 tempo, u8 timerFreq);

 private:
  void loadEventMap();
};


class RareSnesTrack
    : public SeqTrack {
 public:
  RareSnesTrack(RareSnesSeq *parentFile, u32 offset = 0, u32 length = 0);
  virtual void resetVars();
  virtual bool readEvent();
  virtual void onTickBegin();
  virtual void onTickEnd();

  void addVolLR(u32 offset,
                u32 length,
                s8 spcVolL,
                s8 spcVolR,
                const std::string &sEventName = "Volume L/R");
  void addVolLRNoItem(s8 spcVolL, s8 spcVolR);

 private:
  u8 rptNestLevel;                          // nest level for repeat-subroutine command
  u8 rptCount[RARESNES_RPTNESTMAX];         // repeat count for repeat-subroutine command
  u32 rptStart[RARESNES_RPTNESTMAX];        // loop start address for repeat-subroutine command
  u32 rptRetnAddr[RARESNES_RPTNESTMAX];     // return address for repeat-subroutine command
  u16 spcNotePitch;                        // SPC700 pitch register value (0000-3fff), converter will need it for pitch slide
  s8 spcTranspose;                            // transpose (compatible with actual engine)
  s8 spcTransposeAbs;                         // transpose (without relative change)
  s8 spcTuning;                               // tuning (compatible with actual engine)
  s8 spcVolL, spcVolR;                        // SPC700 left/right volume
  u8 spcInstr;                              // SPC700 instrument index (NOT SRCN value)
  u16 spcADSR;                             // SPC700 ADSR value
  u16 defNoteDur;                          // default duration for note (0:unused)
  bool useLongDur;                            // indicates duration length
  u8 altNoteByte1;                          // note number preset 1
  u8 altNoteByte2;                          // note number preset 2

  double getTuningInSemitones(s8 tuning);
  void calculateVolPanFromVolLR(s8 volL, s8 volR, u8 &midiVol, u8 &midiPan);
};
