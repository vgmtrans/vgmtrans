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

constexpr double tlToDB(u8 tl) {
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
  u8 rate0, point0, level0;
  u8 rate1, point1, level1;
  u8 rate2, point2, level2;
  u8 rate3;
};

struct SegSatMixerTable {
  u8 data[18];
  [[nodiscard]] u8 effLevel(int idx) const {
    return data[idx] >> 5;
  }
  [[nodiscard]] u8 effPan(int idx) const {
    return data[idx] & 0x1F;
  }
};

struct SegSatPlfoTable {
  u8 delay;
  u8 amp;
  u8 frequency;
  u8 fadeTime;
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
  void assignBankNumber(u8 bankNum);
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
  SegSatInstr(SegSatInstrSet* set, size_t offset, size_t length, u32 bank, u32 number, const std::string &name = "SegSatInstr");
  ~SegSatInstr() = default;

  virtual bool loadInstr();

  u8 pitchBendRange() { return m_pitchBendRange; }
  s8 volBias()        { return m_volBias; }
  u8 portamento()     { return m_portamento; }

private:
  u8 m_pitchBendRange;
  s8 m_volBias;
  u8 m_portamento;
};

// *********
// SegSatRgn
// *********
class SegSatRgn : public VGMRgn {
public:
  enum class LoopType : u8 {
    Off = 0,
    Forward = 1,
    Reverse = 2,
    PingPong = 3,
  };

  enum class LFOWave : u8 {
    SawTooth = 0,
    Square = 1,
    Triangle = 2,
    Noise = 3,
  };

  enum class SampleType : u8 { PCM16 = 0, PCM8 = 1 };

  SegSatRgn(SegSatInstr* instr, uint32_t offset, const std::string& name);
  ~SegSatRgn() = default;

  bool isRegionValid();
  u32 sampleLoopStart() const { return m_sampLoopStart; }
  u32 sampleLoopEnd() const { return m_sampLoopEnd; }
  u32 sampleLoopLength() const { return m_sampLoopEnd - m_sampLoopStart; }
  SampleType sampleType() const { return m_sampleType; }
  LoopType loopType() const { return m_loopType; }
  u8 vlTableIndex() const { return m_vlTableIndex; }
  u8 totalLevel() const { return m_totalLevel; }

private:
  u8 m_attackRate;
  u8 m_decayRate1;
  u8 m_decayRate2;
  u8 m_decayLevel;
  u8 m_releaseRate;
  u8 m_keyRateScale;
  SampleType m_sampleType;
  u32 m_sampLoopStart;
  u32 m_sampLoopEnd;
  LoopType m_loopType;
  LFOWave m_pitchLfoWave;
  LFOWave m_ampLfoWave;
  bool m_lfoReset;
  u8 m_lfoFreq;
  u8 m_pitchLfoDepth;
  u8 m_ampLfoDepth;
  u8 m_directLevel;
  u8 m_directPan;

  bool m_enableLfoModulation;
  bool m_enablePeg;
  bool m_enablePegModulation;
  bool m_enablePlfo;
  bool m_enablePlfoModulation;
  bool m_soundDirectEnable;

  u8 m_totalLevel;
  bool m_enableTotalLevelModulation;

  u8 m_vlTableIndex;
  u8 m_PegIndex;
  u8 m_plfoIndex;
};
