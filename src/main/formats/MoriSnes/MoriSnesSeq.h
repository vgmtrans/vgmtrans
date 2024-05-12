#pragma once
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
  MoriSnesSeq(RawFile *file, MoriSnesVersion ver, uint32_t seqdataOffset, std::string name = "Mint SNES Seq");
  virtual ~MoriSnesSeq();

  virtual bool GetHeaderInfo();
  virtual bool GetTrackPointers();
  virtual void ResetVars();

  double GetTempoInBPM(uint8_t tempo, bool fastTempo);

  MoriSnesVersion version;
  std::map<uint8_t, MoriSnesSeqEventType> EventMap;

  uint16_t TrackStartAddress[10];
  std::vector<uint16_t> InstrumentAddresses;
  std::map<uint16_t, MoriSnesInstrHintDir> InstrumentHints;

  uint8_t spcTempo;
  bool fastTempo;

 private:
  void LoadEventMap();
};

class MoriSnesTrack
    : public SeqTrack {
 public:
  MoriSnesTrack(MoriSnesSeq *parentFile, long offset = 0, long length = 0);
  virtual void ResetVars();
  virtual bool ReadEvent();

  void ParseInstrument(uint16_t instrAddress, uint8_t instrNum);
  void ParseInstrumentEvents(uint16_t offset, uint8_t instrNum, bool percussion = false, uint8_t percNoteKey = 0);

 private:
  std::list<int8_t> tiedNoteKeys;

  uint8_t spcDeltaTime;
  int8_t spcNoteNumberBase;
  uint8_t spcNoteDuration;
  uint8_t spcNoteVelocity;
  uint8_t spcVolume;
  int8_t spcTranspose;
  uint8_t spcTuning;

  uint8_t spcCallStack[MORISNES_CALLSTACK_SIZE]; // shared by multiple commands
  uint8_t spcCallStackPtr;
};
