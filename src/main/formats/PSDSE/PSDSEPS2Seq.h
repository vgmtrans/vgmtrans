#pragma once

#include "PSDSEPS2Header.h"
#include "SeqTrack.h"
#include "VGMSeq.h"

#include <cstdint>

class PSDSEPS2Seq : public VGMSeq {
public:
  PSDSEPS2Seq(RawFile* file, const PSDSEPS2::SequenceHeader& header);

  bool parseHeader() override;
  bool parseTrackPointers() override;
  void resetVars() override;

  void observeEffectVelocity();
  [[nodiscard]] uint8_t normalizedEffectVelocity() const;

  PSDSEPS2::SequenceHeader m_header;

private:
  uint32_t m_peakEffectiveVelocity = 0;
};

class PSDSEPS2Track : public SeqTrack {
public:
  PSDSEPS2Track(PSDSEPS2Seq* sequence, const PSDSEPS2::SequenceTrackRecord& record);

  void resetVars() override;
  bool readEvent() override;

protected:
  void setChannelAndGroupFromTrkNum(int trackNum) override;

private:
  void addDelta(uint32_t offset, uint32_t length, uint32_t ticks, const std::string& name);
  uint16_t readEventU16LE();
  uint32_t readEventU24LE();
  std::string readOperands(uint8_t count);

  uint16_t m_version;
  uint16_t m_defaultBankId;
  uint16_t m_bankId;
  bool m_initialBankPending;
  uint8_t m_channel;
  uint8_t m_outputGroup;
  int8_t m_octave;
  uint32_t m_afterDelta;
  uint32_t m_gateDelta;
  uint32_t m_lastNoteDuration;
  uint8_t m_gateTime;
  uint32_t m_repeatOffset;
};
