#pragma once

#include "base/Types.h"
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "MoriSnesFormat.h"
#include "MoriSnesInstr.h"

#define MORISNES_CALLSTACK_SIZE 10

enum MoriSnesSeqEventType {
  //start enum at 1 because if map[] look up fails, it returns 0, and we don't want that to get confused with a legit event
  EVENT_UNKNOWN0 = 1,
  EVENT_UNKNOWN1,
  EVENT_UNKNOWN2,
  EVENT_UNKNOWN3,
  EVENT_UNKNOWN4,
  EVENT_NOTE_PARAM,
  EVENT_NOTE,
  EVENT_NOTE_WITH_PARAM,
  EVENT_PROGCHANGE,
  EVENT_PAN,
  EVENT_TEMPO,
  EVENT_VOLUME,
  EVENT_PRIORITY,
  EVENT_TUNING,
  EVENT_ECHO_ON,
  EVENT_ECHO_OFF,
  EVENT_ECHO_PARAM,
  EVENT_GOTO,
  EVENT_CALL,
  EVENT_RET,
  EVENT_LOOP_START,
  EVENT_LOOP_END,
  EVENT_END,
  EVENT_NOTE_NUMBER,
  EVENT_OCTAVE_UP,
  EVENT_OCTAVE_DOWN,
  EVENT_WAIT,
  EVENT_PITCHBENDRANGE,
  EVENT_TRANSPOSE,
  EVENT_TRANSPOSE_REL,
  EVENT_TUNING_REL,
  EVENT_KEY_ON,
  EVENT_KEY_OFF,
  EVENT_VOLUME_REL,
  EVENT_PITCHBEND,
  EVENT_INSTR,
  EVENT_TIMEBASE,
};

class MoriSnesSeq : public VGMSeq {
 public:
  MoriSnesSeq(RawFile *file, MoriSnesVersion ver, u32 seqdataOffset, std::string name = "Mint SNES Seq");
  virtual ~MoriSnesSeq();

  virtual bool parseHeader();
  virtual bool parseTrackPointers();
  virtual void resetVars();

  double getTempoInBPM(u8 tempo, bool fastTempo);

  MoriSnesVersion version;
  std::map<u8, MoriSnesSeqEventType> EventMap;

  u16 TrackStartAddress[10];
  std::vector<u16> InstrumentAddresses;
  std::map<u16, MoriSnesInstrHintDir> InstrumentHints;

  u8 spcTempo;
  bool fastTempo;

 private:
  void loadEventMap();
};

class MoriSnesTrack
    : public SeqTrack {
 public:
  MoriSnesTrack(MoriSnesSeq *parentFile, u32 offset = 0, u32 length = 0);
  virtual void resetVars();
  virtual bool readEvent();

  void parseInstrument(u16 instrAddress, u8 instrNum);
  void parseInstrumentEvents(u16 offset, u8 instrNum, bool percussion = false, u8 percNoteKey = 0);

 private:
  std::list<s8> tiedNoteKeys;

  u8 spcDeltaTime;
  s8 spcNoteNumberBase;
  u8 spcNoteDuration;
  u8 spcNoteVelocity;
  u8 spcVolume;
  s8 spcTranspose;
  u8 spcTuning;

  u8 spcCallStack[MORISNES_CALLSTACK_SIZE]; // shared by multiple commands
  u8 spcCallStackPtr;
};
