/*
 * VGMTrans (c) 2002-2025
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "VGMInstrSet.h"
#include "VGMSampColl.h"
#include "VGMRgn.h"
#include "SegSatFormat.h"
#include <unordered_set>

//Envelope times in ms, taken from MAME's SCSP implementation
static const double ARTimes[64] = {100000,100000,8100.0,6900.0,6000.0,4800.0,4000.0,3400.0,3000.0,2400.0,2000.0,1700.0,1500.0,
          1200.0,1000.0,860.0,760.0,600.0,500.0,430.0,380.0,300.0,250.0,220.0,190.0,150.0,130.0,110.0,95.0,
          76.0,63.0,55.0,47.0,38.0,31.0,27.0,24.0,19.0,15.0,13.0,12.0,9.4,7.9,6.8,6.0,4.7,3.8,3.4,3.0,2.4,
          2.0,1.8,1.6,1.3,1.1,0.93,0.85,0.65,0.53,0.44,0.40,0.35,0.0,0.0};
static const double DRTimes[64] = {100000,100000,118200.0,101300.0,88600.0,70900.0,59100.0,50700.0,44300.0,35500.0,29600.0,25300.0,22200.0,17700.0,
          14800.0,12700.0,11100.0,8900.0,7400.0,6300.0,5500.0,4400.0,3700.0,3200.0,2800.0,2200.0,1800.0,1600.0,1400.0,1100.0,
          920.0,790.0,690.0,550.0,460.0,390.0,340.0,270.0,230.0,200.0,170.0,140.0,110.0,98.0,85.0,68.0,57.0,49.0,43.0,34.0,
          28.0,25.0,22.0,18.0,14.0,12.0,11.0,8.5,7.1,6.1,5.4,4.3,3.6,3.1};

constexpr double tlToDB(uint8_t tl) {
  // The hardware level method for calculating dB attenuation is to translate each TL bit to a dB
  // weight. Specifically, the weights for bit0..bit7 are:
  // { 0.4, 0.8, 1.5, 3.0, 6.0, 12.0, 24.0, 48.0 }
  // These weights aren't perfectly linear, but they're close. Since the final TL is the product of
  // many volume modifying values (velocity, volume, instrument settings), we can't practically
  // recalculate TL attenuation at each point during conversion. Instead, we treat the table as
  // linear: ∑(the table) / 0xFF == 0.37529. This gets us pretty close.
  return tl * 0.37529;
}

// Velocity Level Table, defines a curve to transform note velocity
struct SegSatVLTable {
  uint8_t rate0, point0, level0;
  uint8_t rate1, point1, level1;
  uint8_t rate2, point2, level2;
  uint8_t rate3;
};

struct SegSatMixerTable {
  uint8_t data[18];
  [[nodiscard]] uint8_t effLevel(int idx) const {
    return data[idx] >> 5;
  }
  [[nodiscard]] uint8_t effPan(int idx) const {
    return data[idx] & 0x1F;
  }
};

struct SegSatPlfoTable {
  uint8_t delay;
  uint8_t amp;
  uint8_t frequency;
  uint8_t fadeTime;
};

// **************
// SegSatInstrSet
// **************

enum class SegSatDriverVer : uint8_t;

class SegSatInstrSet:
    public VGMInstrSet {
public:
  SegSatInstrSet(RawFile* file, uint32_t offset, int numInstrs, SegSatDriverVer ver, const std::string& name = "SegSatInstrSet");
  ~SegSatInstrSet() = default;

  virtual bool parseHeader();
  virtual bool parseInstrPointers();
  void assignBankNumber(uint8_t bankNum);
  [[nodiscard]] SegSatDriverVer driverVer() const { return m_driverVer; }

  std::vector<SegSatMixerTable> mixerTables() { return m_mixerTables; }
  std::vector<SegSatVLTable> vlTables() { return  m_vlTables; }
  std::vector<SegSatPlfoTable> plfoTables() { return  m_plfoTables; }

  std::unordered_set<uint32_t> sampleOffsets;


private:
  int m_numInstrs;
  SegSatDriverVer m_driverVer;
  std::vector<SegSatMixerTable> m_mixerTables;
  std::vector<SegSatVLTable> m_vlTables;
  std::vector<SegSatPlfoTable> m_plfoTables;
};

// ***********
// SegSatInstr
// ***********

class SegSatInstr
    : public VGMInstr {
public:
  SegSatInstr(SegSatInstrSet* set, size_t offset, size_t length, uint32_t bank, uint32_t number, const std::string &name = "SegSatInstr");
  ~SegSatInstr() = default;

  virtual bool loadInstr();

  uint8_t pitchBendRange() { return m_pitchBendRange; }
  int8_t volBias()        { return m_volBias; }
  uint8_t portamento()     { return m_portamento; }

private:
  uint8_t m_pitchBendRange;
  int8_t m_volBias;
  uint8_t m_portamento;
};

// *********
// SegSatRgn
// *********
class SegSatRgn : public VGMRgn {
public:
  enum class LoopType : uint8_t {
    Off = 0,
    Forward = 1,
    Reverse = 2,
    PingPong = 3,
  };

  enum class LFOWave : uint8_t {
    SawTooth = 0,
    Square = 1,
    Triangle = 2,
    Noise = 3,
  };

  enum class SampleType : uint8_t { PCM16 = 0, PCM8 = 1 };

  SegSatRgn(SegSatInstr* instr, uint32_t offset, const std::string& name);
  ~SegSatRgn() = default;

  bool isRegionValid();
  uint32_t sampleLoopStart() const { return m_sampLoopStart; }
  uint32_t sampleLoopEnd() const { return m_sampLoopEnd; }
  uint32_t sampleLoopLength() const { return m_sampLoopEnd - m_sampLoopStart; }
  SampleType sampleType() const { return m_sampleType; }
  LoopType loopType() const { return m_loopType; }
  uint8_t vlTableIndex() const { return m_vlTableIndex; }
  uint8_t totalLevel() const { return m_totalLevel; }

private:
  uint8_t m_attackRate;
  uint8_t m_decayRate1;
  uint8_t m_decayRate2;
  uint8_t m_decayLevel;
  uint8_t m_releaseRate;
  uint8_t m_keyRateScale;
  SampleType m_sampleType;
  uint32_t m_sampLoopStart;
  uint32_t m_sampLoopEnd;
  LoopType m_loopType;
  LFOWave m_pitchLfoWave;
  LFOWave m_ampLfoWave;
  bool m_lfoReset;
  uint8_t m_lfoFreq;
  uint8_t m_pitchLfoDepth;
  uint8_t m_ampLfoDepth;
  uint8_t m_directLevel;
  uint8_t m_directPan;

  bool m_enableLfoModulation;
  bool m_enablePeg;
  bool m_enablePegModulation;
  bool m_enablePlfo;
  bool m_enablePlfoModulation;
  bool m_soundDirectEnable;

  uint8_t m_totalLevel;
  bool m_enableTotalLevelModulation;

  uint8_t m_vlTableIndex;
  uint8_t m_PegIndex;
  uint8_t m_plfoIndex;
};
