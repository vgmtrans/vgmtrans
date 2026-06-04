#pragma once

#include "base/Types.h"
#include "VGMSeqNoTrks.h"

#include <string>
#include <vector>

class SegSatRgn;
class SegSatInstr;
class SegSatInstrSet;
struct SegSatVLTable;

class SegSatSeq:
    public VGMSeqNoTrks {
 public:
  SegSatSeq(RawFile *file, u32 offset, std::string name);
  ~SegSatSeq() override = default;

  void resetVars() override;
  void useColl(const VGMColl* coll) override;
  bool parseHeader() override;
  bool readEvent() override;

 private:
  static constexpr u32 kInvalidOffset = static_cast<u32>(-1);

  const SegSatRgn* resolveRegion(u8 bank, u8 progNum, u8 noteNum);
  constexpr double tlDB(u8 tl);
  u8 resolveVelocity(u8 vel, const SegSatRgn& rgn, s8 volBias, u8 ch);

  struct CollContext {
    std::vector<SegSatVLTable> m_vlTables;
    std::vector<const SegSatInstr*> instrs;
  };
  CollContext m_collContext;
  u32 m_normalTrackOffset;
  int m_remainingNotesInLoop = 0;
  u32 m_loopEndPos = kInvalidOffset;
  u32 m_foreverLoopStart = kInvalidOffset;
  u32 m_durationAccumulator = 0;
  u8 m_vol[16];
  u8 m_bank[16];
  u8 m_progNum[16];
};
