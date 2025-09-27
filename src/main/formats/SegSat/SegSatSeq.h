#pragma once
#include "VGMSeqNoTrks.h"

class SegSatRgn;
class SegSatInstr;
class SegSatInstrSet;
struct SegSatVLTable;

class SegSatSeq:
    public VGMSeqNoTrks {
 public:
  SegSatSeq(RawFile *file, uint32_t offset, std::string name);
  ~SegSatSeq() override = default;

  void resetVars() override;
  void useColl(const VGMColl* coll) override;
  bool parseHeader() override;
  bool readEvent() override;

 private:
  const SegSatRgn* resolveRegion(u8 bank, u8 progNum, u8 noteNum);
  constexpr double tlDB(uint8_t tl);
  u8 resolveVelocity(u8 vel, const SegSatRgn& rgn, s8 volBias, u8 ch);

  struct CollContext {
    std::vector<SegSatVLTable> m_vlTables;
    std::vector<SegSatInstr> instrs;
  };
  CollContext m_collContext;
  u32 m_normalTrackOffset;
  int m_remainingNotesInLoop = 0;
  u32 m_loopEndPos = -1;
  u32 m_foreverLoopStart = -1;
  u32 m_durationAccumulator = 0;
  u8 m_vol[16];
  u8 m_bank[16];
  u8 m_progNum[16];
};