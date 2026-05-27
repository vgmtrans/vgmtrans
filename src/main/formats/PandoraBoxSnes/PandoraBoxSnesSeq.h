#pragma once

#include "base/types.h"
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
                    u32 seqdata_offset,
                    std::string newName = "PandoraBox SNES Seq");
  virtual ~PandoraBoxSnesSeq();

  virtual bool parseHeader();
  virtual bool parseTrackPointers();
  virtual void resetVars();

  static const u8 VOLUME_TABLE[16];

  PandoraBoxSnesVersion version;
  std::map<u8, PandoraBoxSnesSeqEventType> EventMap;

  std::map<u8, u16> instrADSRHints;

 private:
  void loadEventMap();
};


class PandoraBoxSnesTrack
    : public SeqTrack {
 public:
  PandoraBoxSnesTrack(PandoraBoxSnesSeq *parentFile, u32 offset = 0, u32 length = 0);
  virtual void resetVars();
  virtual bool readEvent();

 private:
  u8 getVolume(u8 volumeIndex);

  s8 prevNoteKey;
  bool prevNoteSlurred;
  u8 spcNoteLength;
  u8 spcNoteQuantize;
  u8 spcVolumeIndex;
  u8 spcInstr;
  u16 spcADSR;
  u8 spcCallStack[PANDORABOXSNES_CALLSTACK_SIZE];
  u8 spcCallStackPtr;
};
