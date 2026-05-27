#pragma once

#include "base/Types.h"
#include "NinSnesFormat.h"
#include "NinSnesScanResult.h"
#include "VGMInstrSet.h"
#include "VGMRgn.h"
#include "VGMSampColl.h"

#include <string>
#include <vector>

class NinSnesSeq;

// ****************
// NinSnesInstrSet
// ****************

class NinSnesInstrSet:
    public VGMInstrSet {
 public:
  NinSnesInstrSet(RawFile *file,
                  NinSnesProfileId profile,
                  u32 offset,
                  u32 spcDirAddr,
                  const std::string &name = "NinSnesInstrSet");
  NinSnesInstrSet(RawFile *file, const NinSnesScanResult& scanResult);
  virtual ~NinSnesInstrSet();

  bool parseHeader() override;
  bool parseInstrPointers() override;
  void useColl(const VGMColl* coll) override;
  void unuseColl() override;

  NinSnesSignatureId signature;
  NinSnesProfileId profileId;

  u16 konamiTuningTableAddress;
  u8 konamiTuningTableSize;

 protected:
  u32 spcDirAddr;
  std::vector<u8> usedSRCNs;

 private:
  void addStandardPercussionDrumKit(const NinSnesSeq& seq);
  void addIntelliOverrideInstrs(const NinSnesSeq& seq);
  void addIntelliDrumKits(const NinSnesSeq& seq);
};

// *************
// NinSnesInstr
// *************

class NinSnesInstr
    : public VGMInstr {
 public:
  NinSnesInstr(VGMInstrSet *instrSet,
               NinSnesProfileId profile,
               u32 offset,
               u32 theBank,
               u32 theInstrNum,
               u32 spcDirAddr,
               const std::string &name = "NinSnesInstr");
  virtual ~NinSnesInstr();

  virtual bool loadInstr();

  static bool isValidHeader
      (RawFile *file, NinSnesProfileId profileId, u32 addrInstrHeader, u32 spcDirAddr, bool validateSample);
  static u32 expectedSize(NinSnesProfileId profileId);

  NinSnesProfileId profileId;

  u16 konamiTuningTableAddress;
  u8 konamiTuningTableSize;

 protected:
  u32 spcDirAddr;
};

// ***********
// NinSnesRgn
// ***********

class NinSnesRgn
    : public VGMRgn {
 public:
  NinSnesRgn(NinSnesInstr *instr,
             NinSnesProfileId profileId,
             u32 offset,
             u16 konamiTuningTableAddress = 0,
             u8 konamiTuningTableSize = 0);
  virtual ~NinSnesRgn();

  virtual bool loadRgn();
};
