#pragma once

#include "base/Types.h"
#include "SeqTrack.h"
#include "TriAcePS1Format.h"
#include "VGMSeq.h"

#include <map>
#include <memory>
#include <span>
#include <string>
#include <vector>

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
  [[nodiscard]] std::span<TriAcePS1ScorePattern* const> scorePatterns() const {
    return m_scorePatterns;
  }

  VGMHeader *header;
  TrkInfo TrkInfos[32];
  u8 initialTempoBPM;

 private:
  friend class TriAcePS1Track;
  TriAcePS1ScorePattern* sinkScorePattern(std::unique_ptr<TriAcePS1ScorePattern>&& pattern);

  TriAcePS1ScorePattern *curScorePattern = nullptr;
  std::map<u32, TriAcePS1ScorePattern *> patternMap;
  std::vector<std::unique_ptr<TriAcePS1ScorePattern>> m_ownedScorePatterns;
  std::vector<TriAcePS1ScorePattern *> m_scorePatterns;
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

  void loadTrackMainLoop(u32 stopOffset, s32 stopTime) override;
  u32 readScorePattern(u32 offset);
  bool isOffsetUsed(u32 offset) override;
  SeqEvent* sinkEvent(std::unique_ptr<SeqEvent>&& seqEvent) override;
  bool readEvent() override;

  u8 impliedNoteDur;
  u8 impliedVelocity;
};
