#pragma once
#include "VGMInstrSet.h"
#include "VGMSampColl.h"
#include "VGMMiscFile.h"
#include "NamcoS2Seq.h"

class NamcoS2SampColl;
class NamcoS2ArticTable;

static const uint16_t envRateTable[128] = {
    0, 1, 2, 3, 4, 5, 7, 9, 0xB, 0xD, 0xF, 0x11, 0x13, 0x15,
    0x17, 0x19, 0x1B, 0x1D, 0x1F, 0x22, 0x24, 0x27, 0x2A, 0x2D, 0x30,
    0x33, 0x37, 0x3B, 0x40, 0x44, 0x49, 0x4F, 0x54, 0x5A, 0x61, 0x68,
    0x70, 0x78, 0x81, 0x8A, 0x94, 0x9F, 0xAA, 0xB7, 0xC4, 0xD2, 0xE1,
    0xF2, 0x103, 0x116, 0x12A, 0x140, 0x157, 0x170, 0x18B, 0x1AB,
    0x1C7, 0x1E8, 0x20B, 0x231, 0x25A, 0x285, 0x2B4, 0x2E6, 0x31C,
    0x356, 0x394, 0x3D6, 0x41D, 0x46A, 0x4BC, 0x514, 0x572, 0x5D7,
    0x644, 0x6B8, 0x735, 0x7BA, 0x84A, 0x8E4, 0x989, 0xA3A, 0xAF8,
    0xBC3, 0xC9E, 0xD88, 0xE83, 0xF91, 0x10B2, 0x11E8, 0x1334,
    0x1498, 0x1617, 0x17B1, 0x1969, 0x1B40, 0x1D3A, 0x1F59, 0x219F,
    0x240F, 0x26AC, 0x297A, 0x2C7C, 0x2FB6, 0x332B, 0x36E1, 0x3ADC,
    0x3F20, 0x43B4, 0x489D, 0x4DE1, 0x5386, 0x5995, 0x6014, 0x670B,
    0x6E84, 0x7687, 0x7F1F, 0x8857, 0x923A, 0x9CD4, 0xA833, 0xB465,
    0xC17A, 0xCF81, 0xDE8D, 0xEEB0, 0xFFFF
};

#define C140_ARTIC_SIZE 10
#define C140_SAMPINFO_SIZE 10

struct C140ArticSection {
  uint8_t rate;
  uint8_t level;
};

class C140Artic {
 public:
  vector<C140ArticSection> upwardSegments;
  vector<C140ArticSection> downwardSegments;

  //double attenuation()
  double attackTime();      // in seconds
  double decayTime(uint8_t hold);       // in seconds
  double sustainLevel();    // as percentage
  double sustainTime();     // in seconds
  double releaseTime();     // in seconds

  void Load(RawFile *file, uint32_t offset);
};

//struct C140Artic {
//  uint8_t unknown1;
//  uint8_t unknown2;
//  uint8_t unknown3;
//  uint8_t unknown4;
//  uint8_t unknown5;
//  uint8_t unknown6;
//  uint8_t unknown7;
//  uint8_t unknown8;
//  uint8_t unknown9;
//  uint8_t unknown10;
//};
//struct Artic {
//  ArtiSection upwardSections[]
//};



struct C140SampleInfo {
  uint8_t bank;
  uint8_t mode;
  uint16_t start_addr;
  uint32_t end_addr;
  uint32_t loop_addr;
  uint16_t freq;

 public:
  bool loops();
  uint32_t realStartAddr();
  uint32_t realEndAddr();
  uint32_t realLoopAddr();
 private:
  uint32_t getAddr(uint32_t adrs, uint32_t bank);

};

// ***************
// NamcoS2InstrSet
// ***************

class NamcoS2C140InstrSet:
    public VGMInstrSet {
 public:
  NamcoS2C140InstrSet(
      RawFile *file,
      uint32_t offset,
      NamcoS2SampColl* sampColl,
      NamcoS2ArticTable* articTable,
      shared_ptr<InstrMap> instrMap,
      const std::wstring &name = L"Namco S2 C140 InstrSet"
  );
  virtual ~NamcoS2C140InstrSet(void);

  virtual bool GetHeaderInfo();
  virtual bool GetInstrPointers();

 private:
  NamcoS2SampColl* sampColl;
  NamcoS2ArticTable* articTable;
  shared_ptr<InstrMap> instrMap;
};


// ****************
// NamcoS2C140Instr
// ****************

class NamcoS2C140Instr
    : public VGMInstr {
 public:
  NamcoS2C140Instr(VGMInstrSet *instrSet,
           uint32_t offset,
           uint32_t length,
           uint32_t theBank,
           uint32_t theInstrNum,
           const std::wstring &name);
  virtual ~NamcoS2C140Instr(void);
  virtual bool LoadInstr();
// protected:
//  CPSFormatVer GetFormatVer() { return ((CPSInstrSet *) parInstrSet)->fmt_version; }
//
// protected:
//  uint8_t attack_rate;
//  uint8_t decay_rate;
//  uint8_t sustain_level;
//  uint8_t sustain_rate;
//  uint8_t release_rate;
//
//  int info_ptr;        //pointer to start of instrument set block
//  int nNumRegions;
};


// *****************
// NamcoS2ArticTable
// *****************

class NamcoS2ArticTable
    : public VGMMiscFile {
 public:
  NamcoS2ArticTable(RawFile *file, uint32_t offset, uint32_t length, uint32_t bankOffset);
  virtual ~NamcoS2ArticTable(void);

  virtual bool LoadMain();

 public:
  vector<C140Artic> artics;

 private:
  void LoadArtic(uint32_t offset);

  uint32_t bankOffset;
};

// **********************
// NamcoS2SampleInfoTable
// **********************

class NamcoS2SampleInfoTable
    : public VGMMiscFile {
 public:
  NamcoS2SampleInfoTable(RawFile *file, uint32_t offset, uint32_t samplesFileLenth);
  ~NamcoS2SampleInfoTable(void);

 public:
  virtual bool LoadMain();

  vector<C140SampleInfo> infos;

 private:
  uint32_t samplesFileLength;
};

// ***************
// NamcoS2SampColl
// ***************

class NamcoS2SampColl
    : public VGMSampColl {
 public:
  NamcoS2SampColl(RawFile *file, NamcoS2SampleInfoTable *sampinfotable, uint32_t offset,
              uint32_t length = 0, std::wstring name = std::wstring(L"Namco S2 Sample Collection"));
  virtual bool GetHeaderInfo();
  virtual bool GetSampleInfo();

 private:
  uint32_t FindSample(uint32_t adrs, uint32_t bank, uint8_t voice);

  NamcoS2SampleInfoTable *sampInfoTable;
};
