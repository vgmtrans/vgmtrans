#pragma once

#include "VGMInstrSet.h"
#include "CPS1Scanner.h"
#include "VGMSampColl.h"
#include "YM2151.h"
#include "YM2151InstrSet.h"
#include <sstream>

// ******************
// CPS1SampleInstrSet
// ******************

class CPS1SampleInstrSet
    : public VGMInstrSet {
public:
  CPS1SampleInstrSet(RawFile *file,
                     CPS1FormatVer fmt_version,
                     uint32_t offset,
                     std::string name);
  ~CPS1SampleInstrSet() override = default;

  bool parseInstrPointers() override;

public:
  CPS1FormatVer fmt_version;
};

// **************
// CPS1SampColl
// **************

class CPS1SampColl
    : public VGMSampColl {
public:
  CPS1SampColl(RawFile *file, CPS1SampleInstrSet *instrset, uint32_t offset,
               uint32_t length = 0, std::string name = std::string("CPS1 Sample Collection"));
  bool parseHeader() override;
  bool parseSampleInfo() override;

private:
  std::vector<VGMItem*> samplePointers;
  CPS1SampleInstrSet *instrset;
};


// ***************
// CPS1OPMInstrSet
// ***************

class CPS1OPMInstrSet
    : public YM2151InstrSet {
public:
  CPS1OPMInstrSet(RawFile *file,
                 CPS1FormatVer fmt_version,
                 u8 masterVol,
                 uint32_t offset,
                 uint32_t length,
                 const std::string& name);
  ~CPS1OPMInstrSet() override = default;

  bool parseInstrPointers() override;
public:
  CPS1FormatVer fmt_version;
  u8 masterVol;
};

// ************
// CPS1OPMInstr
// ************

struct CPS1OPMInstrDataV2_00 {
  uint8_t SLOT_MASK;
  uint8_t useLFO;
  uint8_t LFO_WAVEFORM = 0;
  uint8_t LFRQ = 0;
  uint8_t PMD = 0;
  uint8_t AMD = 0;
  uint8_t PMS_AMS = 0;
  uint8_t FL_CON;
  uint8_t DT1_MUL[4];
  uint8_t op_vol[4];
  uint8_t KS_AR[4];
  uint8_t AMSEN_D1R[4];
  uint8_t DT2_D2R[4];
  uint8_t D1L_RR[4];

  u8 volToAttenuation(u8 instrVol) const {
    u8 m = instrVol >> 4;
    u8 attenuation = 17 + (instrVol & 0x0F) - 2 * m - (m == 0 ? 1 : 0);
    return attenuation > 0x7F ? 0x7F : attenuation;
  }

  OPMData convertToOPMData(u8 masterVol, const std::string& name) const {
    // LFO
    OPMData::LFO lfo{};
    if (useLFO) {
      lfo.LFRQ = LFRQ;
      lfo.AMD = AMD;
      lfo.PMD = PMD;
      lfo.WF = LFO_WAVEFORM & 0b11;
      lfo.NFRQ = 0;
    }

    // CH
    OPMData::CH ch{};
    ch.PAN = 0b11000000;
    ch.FL = (FL_CON >> 3) & 0b111;
    ch.CON = FL_CON & 0b111;
    ch.AMS = useLFO ? PMS_AMS & 0b11 : 0;
    ch.PMS = useLFO ? (PMS_AMS >> 4) & 0b1111 : 0;
    ch.SLOT_MASK = SLOT_MASK;
    ch.NE = 0;

    // OP
    uint8_t CON_limits[4] = { 7, 5, 4, 0 };
    OPMData::OP op[4];
    u8 masterVolumeAtten = 0x7F - masterVol;
    for (int i = 0; i < 4; i ++) {
      auto conLimit = CON_limits[i];
      auto& opx = op[i];
      opx.AR = KS_AR[i] & 0b11111;
      opx.D1R = AMSEN_D1R[i] & 0b11111;
      opx.D2R = DT2_D2R[i] & 0b11111;
      opx.RR = D1L_RR[i] & 0b1111;
      opx.D1L = D1L_RR[i] >> 4;
      opx.TL = masterVolumeAtten + (ch.CON < conLimit ? op_vol[i] & 0x7F : 0);
      opx.KS = KS_AR[i] >> 6;
      opx.MUL = DT1_MUL[i] & 0b1111;
      opx.DT1 = (DT1_MUL[i] >> 4) & 0b111;
      opx.DT2 = DT2_D2R[i] >> 6;
      opx.AMS_EN = AMSEN_D1R[i] & 0b10000000;
    }

    return {name, lfo, ch, { op[0], op[1], op[2], op[3] }};
  }
};

struct CPS1OPMVolData4_25 {
  uint8_t extra_atten;
  uint8_t key_scale;
  uint8_t vol;
};

struct CPS1OPMInstrDataV4_25 {
  int8_t transpose;
  uint8_t LFO_ENABLE_AND_WF;  // bit 7: enable LFO, bit 5-6: WF, bit 1: reset LFO
  uint8_t LFRQ;
  uint8_t PMD;
  uint8_t AMD;
  uint8_t FL_CON;
  uint8_t PMS_AMS;
  uint8_t SLOT_MASK;
  CPS1OPMVolData4_25 volData[4];
  uint8_t DT1_MUL[4];
  uint8_t KS_AR[4];
  uint8_t AMSEN_D1R[4];
  uint8_t DT2_D2R[4];
  uint8_t D1L_RR[4];

  u8 volToAttenuation(u8 instrVol) const {
    u16 uVar4 = (((u16)instrVol << 8) | (instrVol >> 4)) & 0xF07;

    u8 key_scale_atten = (char)(uVar4 >> 8);

    u8 bVar3 = 0x7f << 1;
    u8 bVar2 = bVar3;
    bVar2 = (((bVar2 << 1 | bVar2 >> 7) & 0xf0) >> 4) * (((s8)uVar4 << 1) & 0x0f);
    bVar2 = bVar2 >> 4;
    if ((bVar3 & 0x80) != 0) {
      bVar2 = -bVar2;
    }
    s8 cVar1 = bVar2 + 0x10 + key_scale_atten;
    return cVar1 > 0x7f ? 0x7F : cVar1;
  }

  // Simplified implementation, but might have to revisit the first when we add key scale
  // u8 volToAttenuation(u8 instrVol) const {
  //   u8 m = instrVol >> 4;
  //   u8 attenuation = 17 + (instrVol & 0x0F) - 2 * m - (m == 0 ? 1 : 0);
  //   return attenuation > 0x7F ? 0x7F : attenuation;
  // }

  OPMData convertToOPMData(u8 masterVol, const std::string& name) const {
    bool enableLFO = (LFO_ENABLE_AND_WF & 0x80) != 0;
    // LFO
    OPMData::LFO lfo{};
    if (enableLFO) {
      lfo.LFRQ = LFRQ;
      lfo.AMD = AMD;
      lfo.PMD = PMD;
      lfo.WF = (LFO_ENABLE_AND_WF >> 5) & 0b11;
      lfo.NFRQ = 0;  // the driver doesn't define noise frequency
    }

    // CH
    OPMData::CH ch{};
    ch.PAN = 0b11000000; // the driver always sets R/L, ie PAN, to 0xC0 (sf2ce 0xDC0)
    ch.FL = (FL_CON >> 3) & 0b111;
    ch.CON = FL_CON & 0b111;
    ch.AMS = enableLFO ? PMS_AMS & 0b11 : 0;
    ch.PMS = enableLFO ? (PMS_AMS >> 4) & 0b1111 : 0;
    ch.SLOT_MASK = SLOT_MASK;
    ch.NE = 0;

    // OP
    uint8_t CON_limits[4] = { 7, 5, 4, 0 };
    OPMData::OP op[4];
    for (int i = 0; i < 4; i ++) {
      auto conLimit = CON_limits[i];
      auto& opx = op[i];
      opx.AR = KS_AR[i] & 0b11111;
      opx.D1R = AMSEN_D1R[i] & 0b11111;
      opx.D2R = DT2_D2R[i] & 0b11111;
      opx.RR = D1L_RR[i] & 0b1111;
      opx.D1L = D1L_RR[i] >> 4;
      if (ch.CON < conLimit) {
        u8 atten = volToAttenuation(volData[i].vol);
        opx.TL = (atten + volData[i].extra_atten) & 0x7F;
      } else {
        u8 masterVolumeAtten = 0x7F - masterVol;
        u8 atten = volToAttenuation(volData[i].vol);
        u32 finalAtten = (atten + masterVolumeAtten) + volData[i].extra_atten;
        opx.TL = std::min(finalAtten, 0x7FU);
      }
      opx.KS = KS_AR[i] >> 6;
      opx.MUL = DT1_MUL[i] & 0b1111;
      opx.DT1 = (DT1_MUL[i] >> 4) & 0b111;
      opx.DT2 = DT2_D2R[i] >> 6;
      opx.AMS_EN = AMSEN_D1R[i] & 0b10000000;
    }

    return {name, lfo, ch, { op[0], op[1], op[2], op[3] }};
  }
};

struct CPS1OPMInstrDataV5_02 {
  uint8_t SLOT_MASK;  // bit 7: enable LFO, bit 5-6: WF, bit 1: reset LFO
  uint8_t LFO_ENABLE_AND_WF;  // bit 7: enable LFO, bit 5-6: WF, bit 1: reset LFO
  uint8_t LFRQ;
  uint8_t PMD;
  uint8_t AMD;
  uint8_t unknown;
  uint8_t PMS_AMS;
  uint8_t FL_CON;
  uint8_t DT1_MUL[4];
  uint8_t KS_AR[4];
  uint8_t AMSEN_D1R[4];
  uint8_t DT2_D2R[4];
  uint8_t D1L_RR[4];
  uint8_t op_vol[4];

  u8 volToAttenuation(u8 instrVol) const {
    u16 uVar4 = (((u16)instrVol << 8) | (instrVol >> 4)) & 0xF07;

    u8 key_scale_atten = (char)(uVar4 >> 8);

    u8 bVar3 = 0x7f << 1;
    u8 bVar2 = bVar3;
    bVar2 = (((bVar2 << 1 | bVar2 >> 7) & 0xf0) >> 4) * (((s8)uVar4 << 1) & 0x0f);
    bVar2 = bVar2 >> 4;
    if ((bVar3 & 0x80) != 0) {
      bVar2 = -bVar2;
    }
    s8 cVar1 = bVar2 + 0x10 + key_scale_atten;
    return cVar1 > 0x7f ? 0x7F : cVar1;
  }

  // Simplified implementation, but might have to revisit the first when we add key scale
  u8 volToAttenuation2(u8 instrVol) const {
    u8 m = instrVol >> 4;
    u8 attenuation = 17 + (instrVol & 0x0F) - 2 * m - (m == 0 ? 1 : 0);
    return attenuation > 0x7F ? 0x7F : attenuation;
  }

  OPMData convertToOPMData(u8 masterVol, const std::string& name) const {
    bool enableLFO = (LFO_ENABLE_AND_WF & 0x80) != 0;
    // LFO
    OPMData::LFO lfo{};
    if (enableLFO) {
      lfo.LFRQ = LFRQ;
      lfo.AMD = AMD;
      lfo.PMD = PMD;
      lfo.WF = (LFO_ENABLE_AND_WF >> 5) & 0b11;
      lfo.NFRQ = 0;  // the driver doesn't define noise frequency
    }

    // CH
    OPMData::CH ch{};
    ch.PAN = 0b11000000; // the driver always sets R/L, ie PAN, to 0xC0 (sf2ce 0xDC0)
    ch.FL = (FL_CON >> 3) & 0b111;
    ch.CON = FL_CON & 0b111;
    ch.AMS = enableLFO ? PMS_AMS & 0b11 : 0;
    ch.PMS = enableLFO ? (PMS_AMS >> 4) & 0b1111 : 0;
    ch.SLOT_MASK = SLOT_MASK;
    ch.NE = 0;

    // OP
    uint8_t CON_limits[4] = { 7, 5, 4, 0 };
    OPMData::OP op[4];
    u8 masterVolumeAtten = 0x7F - masterVol;
    for (int i = 0; i < 4; i ++) {
      auto conLimit = CON_limits[i];
      auto& opx = op[i];
      opx.AR = KS_AR[i] & 0b11111;
      opx.D1R = AMSEN_D1R[i] & 0b11111;
      opx.D2R = DT2_D2R[i] & 0b11111;
      opx.RR = D1L_RR[i] & 0b1111;
      opx.D1L = D1L_RR[i] >> 4;
      // This is wrong. The attenuation is calculated differently for 5.00 and 5.02
      opx.TL = masterVolumeAtten + (ch.CON < conLimit ? op_vol[i] & 0x7F : 0);
      // opx.TL = masterVolumeAtten + op_vol[i] & 0x7F;;
      opx.KS = KS_AR[i] >> 6;
      opx.MUL = DT1_MUL[i] & 0b1111;
      opx.DT1 = (DT1_MUL[i] >> 4) & 0b111;
      opx.DT2 = DT2_D2R[i] >> 6;
      opx.AMS_EN = AMSEN_D1R[i] & 0b10000000;
    }

    return {name, lfo, ch, { op[0], op[1], op[2], op[3] }};
  }
};

template <class OPMType>
class CPS1OPMInstr : public VGMInstr {
public:
  CPS1OPMInstr(VGMInstrSet *instrSet,
               u8 masterVol,
               uint32_t offset,
               uint32_t length,
               uint32_t theBank,
               uint32_t theInstrNum,
               const std::string& name)
    : VGMInstr(instrSet, offset, length, theBank, theInstrNum, name), masterVol(masterVol) {}
  ~CPS1OPMInstr() override = default;

  bool loadInstr() override {
    this->readBytes(offset(), sizeof(OPMType), &opmData);
    return true;
  }

  s8 getTranspose() const { return opmData.transpose; }

private:
  OPMType opmData;
  int info_ptr;        //pointer to start of instrument set block
  int nNumRegions;
  u8 masterVol;
};