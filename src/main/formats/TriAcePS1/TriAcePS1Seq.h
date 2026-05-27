#pragma once

#include "Types.h"
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "TriAcePS1Format.h"

class TriAcePS1ScorePattern;

class TriAcePS1Seq:
    public VGMSeq {
 public:
  typedef struct _TrkInfo {
    u16 unknown1;
    u16 unknown2;
    u16 trkOffset;
  } TrkInfo;


  TriAcePS1Seq(RawFile *file, u32 offset, const std::string &name = std::string("TriAce Seq"));

  bool parseHeader() override;
  bool parseTrackPointers() override;
  void resetVars() override;

  bool postLoad() override;

  VGMHeader *header;
  TrkInfo TrkInfos[32];
  std::vector<TriAcePS1ScorePattern *> aScorePatterns;
  TriAcePS1ScorePattern *curScorePattern;
  std::map<u32, TriAcePS1ScorePattern *> patternMap;
  u8 initialTempoBPM;
};

class TriAcePS1ScorePattern
    : public VGMItem {
 public:
  TriAcePS1ScorePattern(TriAcePS1Seq *parentSeq, u32 offset)
      : VGMItem(parentSeq, offset, 0, "Score Pattern") { }
};


class TriAcePS1Track
    : public SeqTrack {
 public:
  TriAcePS1Track(TriAcePS1Seq *parentSeq, u32 offset = 0, u32 length = 0);

  virtual void loadTrackMainLoop(u32 stopOffset, s32 stopTime);
  u32 readScorePattern(u32 offset);
  virtual bool isOffsetUsed(u32 offset);
  virtual SeqEvent* addEvent(SeqEvent *pSeqEvent);
  virtual bool readEvent();

  u8 impliedNoteDur;
  u8 impliedVelocity;
};
