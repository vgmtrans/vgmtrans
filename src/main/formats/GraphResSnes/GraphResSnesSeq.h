#pragma once

#include "base/Types.h"
#include "GraphResSnesFormat.h"
#include "SeqEvent.h"
#include "SeqTrack.h"
#include "VGMSeq.h"

#include <map>
#include <string>

#define GRAPHRESSNES_CALLSTACK_SIZE 2
#define GRAPHRESSNES_LOOP_LEVEL_MAX 4

enum GraphResSnesSeqEventType {
  //start enum at 1 because if map[] look up fails, it returns 0, and we don't want that to get confused with a legit event
  EVENT_UNKNOWN0 = 1,
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
  EVENT_SLUR,
  EVENT_END,
};

class GraphResSnesSeq
    : public VGMSeq {
 public:
  GraphResSnesSeq
      (RawFile *file, GraphResSnesVersion ver, u32 seqdata_offset, std::string name = "GraphRes SNES Seq");
  ~GraphResSnesSeq() override;

  bool parseHeader() override;
  bool parseTrackPointers() override;
  void resetVars() override;

  GraphResSnesVersion version;
  std::map<u8, GraphResSnesSeqEventType> EventMap;

  std::map<u8, u16> instrADSRHints;

 private:
  void loadEventMap();
};


class GraphResSnesTrack
    : public SeqTrack {
 public:
  GraphResSnesTrack(GraphResSnesSeq *parentFile, u32 offset = 0, u32 length = 0);
  void resetVars() override;
  bool readEvent() override;

 private:
  s8 prevNoteKey;
  bool prevNoteSlurred;
  u8 durationRate;
  u8 defaultNoteLength;
  s8 spcPan;
  u8 spcInstr;
  u16 spcADSR;
  u16 callStack[GRAPHRESSNES_CALLSTACK_SIZE];
  u8 callStackPtr;

  u8 loopStackPtr;
  s8 loopCount[GRAPHRESSNES_LOOP_LEVEL_MAX];
};
