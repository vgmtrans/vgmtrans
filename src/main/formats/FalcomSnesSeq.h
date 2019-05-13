/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
 #pragma once
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "SeqEvent.h"
#include "FalcomSnesFormat.h"

enum FalcomSnesSeqEventType {
  //start enum at 1 because if map[] look up fails, it returns 0, and we don't want that to get confused with a legit event
  EVENT_UNKNOWN0 = 1,
  EVENT_UNKNOWN1,
  EVENT_UNKNOWN2,
  EVENT_UNKNOWN3,
  EVENT_UNKNOWN4,
  EVENT_NOP1,
  EVENT_NOP3,
  EVENT_NOTE,
  EVENT_OCTAVE,
  EVENT_TEMPO,
  EVENT_PROGCHANGE,
  EVENT_VIBRATO,
  EVENT_VIBRATO_ON_OFF,
  EVENT_QUANTIZE,
  EVENT_VOLUME,
  EVENT_VOLUME_DEC,
  EVENT_VOLUME_INC,
  EVENT_PAN,
  EVENT_PAN_DEC,
  EVENT_PAN_INC,
  EVENT_PAN_LFO,
  EVENT_PAN_LFO_ON_OFF,
  EVENT_TUNING,
  EVENT_LOOP_START,
  EVENT_LOOP_BREAK,
  EVENT_LOOP_END,
  EVENT_PITCH_ENVELOPE,
  EVENT_PITCH_ENVELOPE_ON_OFF,
  EVENT_ADSR,
  EVENT_GAIN,
  EVENT_NOISE_FREQ,
  EVENT_PITCHMOD,
  EVENT_ECHO,
  EVENT_ECHO_PARAM,
  EVENT_ECHO_VOLUME_ON_OFF,
  EVENT_ECHO_VOLUME,
  EVENT_ECHO_FIR_OVERWRITE,
  EVENT_GOTO,
};

class FalcomSnesSeq
    : public VGMSeq {
 public:
  FalcomSnesSeq(RawFile *file,
                    FalcomSnesVersion ver,
                    uint32_t seqdata_offset,
                    std::wstring newName = L"Falcom SNES Seq");
  virtual ~FalcomSnesSeq(void);

  virtual bool GetHeaderInfo(void);
  virtual bool GetTrackPointers(void);
  virtual void ResetVars(void);

  static const uint8_t VOLUME_TABLE[129];

  FalcomSnesVersion version;
  std::map<uint8_t, FalcomSnesSeqEventType> EventMap;

  std::vector<uint8_t> NoteDurTable;

  std::map<uint16_t, uint8_t> repeatCountMap;
  std::map<uint8_t, uint16_t> instrADSRHints;

  double GetTempoInBPM(uint8_t tempo);

 private:
  void LoadEventMap(void);
};


class FalcomSnesTrack
    : public SeqTrack {
 public:
  FalcomSnesTrack(FalcomSnesSeq *parentFile, long offset = 0, long length = 0);
  virtual void ResetVars(void);
  virtual bool ReadEvent(void);

  int8_t CalcPanValue(uint8_t pan, double &volumeScale);

 private:
  int8_t prevNoteKey;
  bool prevNoteSlurred;
  uint8_t spcNoteQuantize;
  uint8_t spcInstr;
  uint16_t spcADSR;
  uint8_t spcVolume;
  uint8_t spcPan;
};
