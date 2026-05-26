#pragma once

#include "util/types.h"
#include "VGMSeqNoTrks.h"
#include "SeqTrack.h"
#include "NamcoSnesFormat.h"

enum NamcoSnesSeqEventType {
  //start enum at 1 because if map[] look up fails, it returns 0, and we don't want that to get confused with a legit event
  EVENT_UNKNOWN0 = 1,
  EVENT_UNKNOWN1,
  EVENT_UNKNOWN2,
  EVENT_UNKNOWN3,
  EVENT_DELTA_TIME,
  EVENT_OPEN_TRACKS,
  EVENT_CALL,
  EVENT_END,
  EVENT_DELTA_MULTIPLIER,
  EVENT_MASTER_VOLUME,
  EVENT_LOOP_AGAIN,
  EVENT_LOOP_BREAK,
  EVENT_GOTO,
  EVENT_NOTE,
  EVENT_ECHO_DELAY,
  EVENT_UNKNOWN_0B,
  EVENT_ECHO,
  EVENT_WAIT,
  EVENT_LOOP_AGAIN_ALT,
  EVENT_LOOP_BREAK_ALT,
  EVENT_ECHO_FEEDBACK,
  EVENT_ECHO_FIR,
  EVENT_ECHO_VOLUME,
  EVENT_ECHO_ADDRESS,
  EVENT_CONTROL_CHANGES,
};

enum NamcoSnesSeqControlType {
  //start enum at 1 because if map[] look up fails, it returns 0, and we don't want that to get confused with a legit event
  CONTROL_PROGCHANGE = 1,
  CONTROL_VOLUME,
  CONTROL_PAN,
  CONTROL_ADSR,
};

enum NamcoSnesSeqNoteType {
  NOTE_MELODY = 1,
  NOTE_NOISE,
  NOTE_PERCUSSION,
};

class NamcoSnesSeq
    : public VGMSeqNoTrks {
 public:
  NamcoSnesSeq(RawFile *file, NamcoSnesVersion ver, u32 seqdataOffset, std::string newName = "Namco SNES Seq");
  virtual ~NamcoSnesSeq();

  virtual bool parseHeader();
  virtual void resetVars();
  virtual bool readEvent();
  virtual bool postLoad();

  NamcoSnesVersion version;
  std::map<u8, NamcoSnesSeqEventType> EventMap;
  std::map<u8, NamcoSnesSeqControlType> ControlChangeMap;
  std::map<NamcoSnesSeqControlType, std::string> ControlChangeNames;

 private:
  void loadEventMap();
  void keyOffAllNotes();

  u8 NOTE_NUMBER_REST;
  u8 NOTE_NUMBER_NOISE_MIN;
  u8 NOTE_NUMBER_PERCUSSION_MIN;

  s8 prevNoteKey[8];
  NamcoSnesSeqNoteType prevNoteType[8];
  s8 instrNum[8];

  u8 spcDeltaTime;
  u8 spcDeltaTimeScale;
  u16 subReturnAddress;
  u8 loopCount;
  u8 loopCountAlt;
};
