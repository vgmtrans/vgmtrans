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
  const SegSatRgn* resolveRegion(uint8_t bank, uint8_t progNum, uint8_t noteNum);
  constexpr double tlDB(uint8_t tl);
  uint8_t resolveVelocity(uint8_t vel, const SegSatRgn& rgn, int8_t volBias, uint8_t ch);

  struct CollContext {
    std::vector<SegSatVLTable> m_vlTables;
    std::vector<SegSatInstr> instrs;
  };
  CollContext m_collContext;
  uint32_t m_normalTrackOffset;
  int m_remainingNotesInLoop = 0;
  uint32_t m_loopEndPos = -1;
  uint32_t m_foreverLoopStart = -1;
  uint32_t m_durationAccumulator = 0;
  uint8_t m_vol[16];
  uint8_t m_bank[16];
  uint8_t m_progNum[16];
};