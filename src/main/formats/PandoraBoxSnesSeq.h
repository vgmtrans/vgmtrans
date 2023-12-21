#pragma once
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "SeqEvent.h"
#include "PandoraBoxSnesFormat.h"

#define PANDORABOXSNES_CALLSTACK_SIZE   40

enum PandoraBoxSnesSeqEventType {
  //start enum at 1 because if map[] look up fails, it returns 0, and we don't want that to get confused with a legit event
  EVENT_UNKNOWN0 = 1,
  EVENT_UNKNOWN1,
  EVENT_UNKNOWN2,
  EVENT_UNKNOWN3,
  EVENT_UNKNOWN4,
  EVENT_NOP,
  EVENT_NOTE,
  EVENT_OCTAVE,
  EVENT_QUANTIZE,
  EVENT_VOLUME_FROM_TABLE,
  EVENT_PROGCHANGE,
  EVENT_TEMPO,
  EVENT_TUNING,
  EVENT_TRANSPOSE,
  EVENT_PAN,
  EVENT_INC_OCTAVE,
  EVENT_DEC_OCTAVE,
  EVENT_INC_VOLUME,
  EVENT_DEC_VOLUME,
  EVENT_VIBRATO_PARAM,
  EVENT_VIBRATO,
  EVENT_ECHO_OFF,
  EVENT_ECHO_ON,
  EVENT_LOOP_START,
  EVENT_LOOP_END,
  EVENT_LOOP_BREAK,
  EVENT_DSP_WRITE,
  EVENT_NOISE_PARAM,
  EVENT_ADSR,
  EVENT_END,
  EVENT_VOLUME,
};

class PandoraBoxSnesSeq
    : public VGMSeq {
 public:
  PandoraBoxSnesSeq(RawFile *file,
                    PandoraBoxSnesVersion ver,
                    uint32_t seqdata_offset,
                    std::string newName = "PandoraBox SNES Seq");
  virtual ~PandoraBoxSnesSeq(void);

  virtual bool GetHeaderInfo(void);
  virtual bool GetTrackPointers(void);
  virtual void ResetVars(void);

  static const uint8_t VOLUME_TABLE[16];

  PandoraBoxSnesVersion version;
  std::map<uint8_t, PandoraBoxSnesSeqEventType> EventMap;

  std::map<uint8_t, uint16_t> instrADSRHints;

 private:
  void LoadEventMap(void);
};


class PandoraBoxSnesTrack
    : public SeqTrack {
 public:
  PandoraBoxSnesTrack(PandoraBoxSnesSeq *parentFile, long offset = 0, long length = 0);
  virtual void ResetVars(void);
  virtual bool ReadEvent(void);

 private:
  uint8_t GetVolume(uint8_t volumeIndex);

  int8_t prevNoteKey;
  bool prevNoteSlurred;
  uint8_t spcNoteLength;
  uint8_t spcNoteQuantize;
  uint8_t spcVolumeIndex;
  uint8_t spcInstr;
  uint16_t spcADSR;
  uint8_t spcCallStack[PANDORABOXSNES_CALLSTACK_SIZE];
  uint8_t spcCallStackPtr;
};
