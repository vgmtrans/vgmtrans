#pragma once
#include "VGMInstrSet.h"
#include "VGMSampColl.h"
#include "VGMRgn.h"
#include "AkaoFormat.h"
#include "Matcher.h"

// ************
// AkaoInstrSet
// ************

class AkaoInstrSet: public VGMInstrSet {
 public:
  AkaoInstrSet(RawFile *file,
               uint32_t length,
               uint32_t instrOff,
               uint32_t dkitOff,
               uint32_t id,
               std::wstring name = L"Akao Instrument Bank"/*, VGMSampColl* sampColl = NULL*/);
  virtual bool GetInstrPointers();
 public:
  bool bMelInstrs, bDrumKit;
  uint32_t drumkitOff;
};

// *********
// AkaoInstr
// *********


class AkaoInstr: public VGMInstr {
 public:
  AkaoInstr(AkaoInstrSet *instrSet,
            uint32_t offset,
            uint32_t length,
            uint32_t bank,
            uint32_t instrNum,
            const std::wstring &name = L"Instrument");
  virtual bool LoadInstr();

 public:
  uint8_t instrType;
  bool bDrumKit;
};

// ***********
// AkaoDrumKit
// ***********


class AkaoDrumKit: public AkaoInstr {
 public:
  AkaoDrumKit(AkaoInstrSet *instrSet, uint32_t offset, uint32_t length, uint32_t bank, uint32_t instrNum);
  virtual bool LoadInstr();
};


// *******
// AkaoRgn
// *******

class AkaoRgn:
    public VGMRgn {
 public:
  AkaoRgn(VGMInstr *instr, uint32_t offset, uint32_t length = 0, const std::wstring &name = L"Region");
  AkaoRgn(VGMInstr *instr, uint32_t offset, uint32_t length, uint8_t keyLow, uint8_t keyHigh,
          uint8_t artIDNum, const std::wstring &name = L"Region");

  virtual bool LoadRgn();

 public:
  unsigned short ADSR1;                //raw psx ADSR1 value (articulation data)
  unsigned short ADSR2;                //raw psx ADSR2 value (articulation data)
  uint8_t artNum;
  uint8_t drumRelUnityKey;
};


// ***********
// AkaoSampColl
// ***********

typedef struct _AkaoArt {
  uint8_t unityKey;
  short fineTune;
  uint32_t sample_offset;
  uint32_t loop_point;
  uint16_t ADSR1;
  uint16_t ADSR2;
  uint32_t artID;
  int sample_num;
} AkaoArt;


class AkaoSampColl:
    public VGMSampColl {
 public:
  AkaoSampColl(RawFile *file, uint32_t offset, uint32_t length, std::wstring name = L"Akao Sample Collection");
  virtual ~AkaoSampColl();

  virtual bool GetHeaderInfo();
  virtual bool GetSampleInfo();

 public:
  std::vector<AkaoArt> akArts;
  uint32_t starting_art_id;
  uint16_t sample_set_id;

 private:
  uint32_t sample_section_size;
  uint32_t nNumArts;
  uint32_t arts_offset;
  uint32_t sample_section_offset;
};

