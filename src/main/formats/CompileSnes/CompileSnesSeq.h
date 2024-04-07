#pragma once
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
      (RawFile *file, CompileSnesVersion ver, uint32_t seqdataOffset, std::string newName = "Compile SNES Seq");
  virtual ~CompileSnesSeq(void);

  virtual bool GetHeaderInfo(void);
  virtual bool GetTrackPointers(void);
  virtual void ResetVars(void);

  CompileSnesVersion version;
  std::map<uint8_t, CompileSnesSeqEventType> EventMap;

  uint8_t STATUS_PERCUSSION_NOTE_MIN;
  uint8_t STATUS_PERCUSSION_NOTE_MAX;
  uint8_t STATUS_DURATION_DIRECT;
  uint8_t STATUS_DURATION_MIN;
  uint8_t STATUS_DURATION_MAX;

  static const uint8_t noteDurTable[];

  double GetTempoInBPM(uint8_t tempo);

 private:
  void LoadEventMap(void);
};


class CompileSnesTrack
    : public SeqTrack {
 public:
  CompileSnesTrack(CompileSnesSeq *parentFile, long offset = 0, long length = 0);
  virtual void ResetVars(void);
  virtual void AddInitialMidiEvents(int trackNum);
  virtual bool ReadEvent(void);

  uint8_t spcNoteDuration;
  uint8_t spcFlags;
  uint8_t spcVolume;
  int8_t spcTranspose;
  uint8_t spcTempo;
  uint8_t spcSRCN;
  int8_t spcPan;

  uint8_t spcInitialFlags;
  uint8_t spcInitialVolume;
  int8_t spcInitialTranspose;
  uint8_t spcInitialTempo;
  uint8_t spcInitialSRCN;
  int8_t spcInitialPan;

  uint16_t subReturnAddress;
  uint8_t repeatCount[256];

 private:
  bool ReadDurationBytes(uint32_t &offset, uint8_t &duration);
};
