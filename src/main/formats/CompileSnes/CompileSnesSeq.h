#pragma once

#include "util/types.h"
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "SeqEvent.h"
#include "CompileSnesFormat.h"

enum CompileSnesSeqEventType {
  EVENT_UNKNOWN0 =
  1, //start enum at 1 because if map[] look up fails, it returns 0, and we don't want that to get confused with a legit event
  EVENT_UNKNOWN1,
  EVENT_UNKNOWN2,
  EVENT_UNKNOWN3,
  EVENT_UNKNOWN4,
  EVENT_UNKNOWN5,
  EVENT_NOTE,
  EVENT_GOTO,
  EVENT_LOOP_END,
  EVENT_END,
  EVENT_VIBRATO,
  EVENT_PORTAMENTO_TIME,
  EVENT_VOLUME,
  EVENT_VOLUME_ENVELOPE,
  EVENT_TRANSPOSE,
  EVENT_VOLUME_REL,
  EVENT_LOOP_COUNT,
  EVENT_FLAGS,
  EVENT_TEMPO,
  EVENT_TUNING,
  EVENT_CALL,
  EVENT_RET,
  EVENT_ADSR,
  EVENT_PROGCHANGE,
  EVENT_PORTAMENTO_ON,
  EVENT_PORTAMENTO_OFF,
  EVENT_PANPOT_ENVELOPE,
  EVENT_PAN,
  EVENT_LOOP_BREAK,
  EVENT_PERCUSSION_NOTE,
  EVENT_DURATION_DIRECT,
  EVENT_DURATION,
};

class CompileSnesSeq
    : public VGMSeq {
 public:
  CompileSnesSeq
      (RawFile *file, CompileSnesVersion ver, u32 seqdataOffset, std::string name = "Compile SNES Seq");
  ~CompileSnesSeq() override;

  bool parseHeader() override;
  bool parseTrackPointers() override;
  void resetVars() override;

  CompileSnesVersion version;
  std::map<u8, CompileSnesSeqEventType> EventMap;

  u8 STATUS_PERCUSSION_NOTE_MIN;
  u8 STATUS_PERCUSSION_NOTE_MAX;
  u8 STATUS_DURATION_DIRECT;
  u8 STATUS_DURATION_MIN;
  u8 STATUS_DURATION_MAX;

  static const u8 noteDurTable[];

  static double getTempoInBPM(u8 tempo);

 private:
  void loadEventMap();
};


class CompileSnesTrack
    : public SeqTrack {
 public:
  CompileSnesTrack(CompileSnesSeq *parentFile, u32 offset = 0, u32 length = 0);
  void resetVars() override;
  void addInitialMidiEvents(int trackNum) override;
  bool readEvent() override;

  u8 spcNoteDuration{};
  u8 spcFlags{};
  u8 spcVolume{};
  s8 spcTranspose{};
  u8 spcTempo{};
  u8 spcSRCN{};
  s8 spcPan{};

  u8 spcInitialFlags{};
  u8 spcInitialVolume{};
  s8 spcInitialTranspose{};
  u8 spcInitialTempo{};
  u8 spcInitialSRCN{};
  s8 spcInitialPan{};

  u16 subReturnAddress{};
  u8 repeatCount[256]{};

 private:
  bool readDurationBytes(u32& offset, u8& duration) const;
};
