#pragma once

#include "VGMInstrSet.h"
#include "CPS1Scanner.h"
#include "VGMSampColl.h"

// ******************
// CPS1SampleInstrSet
// ******************

class CPS1SampleInstrSet
    : public VGMInstrSet {
public:
  CPS1SampleInstrSet(RawFile *file,
                     CPSFormatVer fmt_version,
                     uint32_t offset,
                     std::string &name);
  virtual ~CPS1SampleInstrSet(void);

  virtual bool GetInstrPointers();

public:
  CPSFormatVer fmt_version;
};

// **************
// CPS1SampColl
// **************

class CPS1SampColl
    : public VGMSampColl {
public:
  CPS1SampColl(RawFile *file, CPS1SampleInstrSet *instrset, uint32_t offset,
               uint32_t length = 0, std::string name = std::string("CPS1 Sample Collection"));
  virtual bool GetHeaderInfo();
  virtual bool GetSampleInfo();

private:
  vector<VGMItem*> samplePointers;
  CPS1SampleInstrSet *instrset;
};


// ***************
// CPS1OPMInstrSet
// ***************

class CPS1OPMInstrSet
    : public VGMInstrSet {
public:
  CPS1OPMInstrSet(RawFile *file,
                 CPSFormatVer fmt_version,
                 uint32_t offset,
                 std::string &name);
  virtual ~CPS1OPMInstrSet(void);

  virtual bool GetInstrPointers();
  bool SaveAsOPMFile(const std::string &filepath);

public:
  CPSFormatVer fmt_version;

private:
  std::string generateOPMFile();
};

// ************
// CPS1OPMInstr
// ************

struct OPMData {
  struct LFO {
    uint8_t LFRQ;
    uint8_t AMD;
    uint8_t PMD;
    uint8_t WF;
    uint8_t NFRQ;
  };

  struct CH {
    uint8_t PAN;
    uint8_t FL;
    uint8_t CON;
    uint8_t AMS;
    uint8_t PMS;
    uint8_t SLOT_MASK;
    uint8_t NE;
  };

  struct OP {
    uint8_t AR;
    uint8_t D1R;
    uint8_t D2R;
    uint8_t RR;
    uint8_t D1L;
    uint8_t TL;
    uint8_t KS;
    uint8_t MUL;
    uint8_t DT1;
    uint8_t DT2;
    uint8_t AMS_EN;
  };

  std::string name;
  LFO lfo;
  CH ch;
  OP op[4];

  std::string toOPMString(int num) const {
    std::ostringstream ss;
    ss << "@:" << num << " " << name << "\n";
    ss << "LFO:" << +lfo.LFRQ << "   " << +lfo.AMD << "   " << +lfo.PMD << "   " << +lfo.WF << "   " << +lfo.NFRQ << "\n";
    ss << "CH: " << +ch.PAN << "   " << +ch.FL << "   " << +ch.CON << "   " << +ch.AMS << "   " << +ch.PMS << " " << +ch.SLOT_MASK << "   " << +ch.NE << "\n";

    const int opIndex[4] = { 0, 2, 1, 3 };
    const char* opNames[4] = {"M1", "C1", "M2", "C2"};
    for (int i = 0; i < 4; ++i) {
      const OP& op = this->op[opIndex[i]];
      ss << opNames[i] << ": " << +op.AR << "   " << +op.D1R << "   " << +op.D2R << "   " << +op.RR << "  " << +op.D1L << "  "
         << +op.TL << "   " << +op.KS << "   " << +op.MUL << "   " << +op.DT1 << "   " << +op.DT2 << " " << +op.AMS_EN << "\n";
    }

    return ss.str();
  }
};

struct CPS1OPMInstrData {
  int8_t transpose;
  uint8_t LFO_ENABLE_AND_WF;
  uint8_t LFRQ;
  uint8_t PMD;
  uint8_t AMD;
  uint8_t FL_CON;
  uint8_t PMS_AMS;
  uint8_t SLOT_MASK;
  uint8_t unknown[12];
  uint8_t DT1_MUL[4];
  uint8_t KS_AR[4];
  uint8_t AMSEN_D1R[4];
  uint8_t DT2_D2R[4];
  uint8_t D1L_RR[4];

  OPMData convertToOPMData(std::string& name) const {
    // LFO
    OPMData::LFO lfo{};
    lfo.WF = (LFO_ENABLE_AND_WF >> 5) & 0b11;
    lfo.NFRQ = 0;  // the driver doesn't define noise frequency

    // CH
    OPMData::CH ch{};
    ch.PAN = 0b11000000; // the driver always sets R/L, ie PAN, to 0xC0 (sf2ce 0xDC0)
    ch.FL = (FL_CON >> 3) & 0b111;
    ch.CON = FL_CON & 0b111;
    ch.AMS = PMS_AMS & 0b11;
    ch.PMS = (PMS_AMS >> 4) & 0b1111;
    ch.SLOT_MASK = SLOT_MASK;
    ch.NE = 0;

    // OP
    OPMData::OP op[4];
    for (int i = 0; i < 4; i ++) {
      auto& opx = op[i];
      opx.AR = KS_AR[i] & 0b11111;
      opx.D1R = AMSEN_D1R[i] & 0b11111;
      opx.D2R = DT2_D2R[i] & 0b11111;
      opx.RR = D1L_RR[i] & 0b1111;
      opx.D1L = D1L_RR[i] >> 4;
      opx.TL = 25; // the driver dynamically calculates TL each note. set to a default for now
      opx.KS = KS_AR[i] >> 6;
      opx.MUL = DT1_MUL[i] & 0b1111;
      opx.DT1 = (DT1_MUL[i] >> 4) & 0b111;
      opx.DT2 = DT2_D2R[i] >> 6;
      opx.AMS_EN = AMSEN_D1R[i] & 0b10000000;
    }

    return {name, lfo, ch, { op[0], op[1], op[2], op[3] }};
  }
};

class CPS1OPMInstr : public VGMInstr {
public:
  CPS1OPMInstr(VGMInstrSet *instrSet,
               uint32_t offset,
               uint32_t length,
               uint32_t theBank,
               uint32_t theInstrNum,
               std::string &name);
  virtual ~CPS1OPMInstr(void);
  virtual bool LoadInstr();
//protected:
//  CPSFormatVer GetFormatVer() { return ((CPS1OPMInstr *) parInstrSet)->fmt_version; }

  std::string toOPMString(int num);

protected:
  CPS1OPMInstrData opmData;

  int info_ptr;        //pointer to start of instrument set block
  int nNumRegions;
};