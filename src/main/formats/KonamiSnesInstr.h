/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
 #pragma once
#include "VGMInstrSet.h"
#include "VGMSampColl.h"
#include "VGMRgn.h"
#include "KonamiSnesFormat.h"

// ******************
// KonamiSnesInstrSet
// ******************

class KonamiSnesInstrSet:
    public VGMInstrSet {
 public:
  KonamiSnesInstrSet(RawFile *file,
                     KonamiSnesVersion ver,
                     uint32_t offset,
                     uint32_t bankedInstrOffset,
                     uint8_t firstBankedInstr,
                     uint32_t percInstrOffset,
                     uint32_t spcDirAddr,
                     const std::wstring &name = L"KonamiSnesInstrSet");
  virtual ~KonamiSnesInstrSet(void);

  virtual bool GetHeaderInfo();
  virtual bool GetInstrPointers();

  KonamiSnesVersion version;

 protected:
  uint32_t bankedInstrOffset;
  uint8_t firstBankedInstr;
  uint32_t percInstrOffset;
  uint32_t spcDirAddr;
  std::vector<uint8_t> usedSRCNs;
};

// ***************
// KonamiSnesInstr
// ***************

class KonamiSnesInstr
    : public VGMInstr {
 public:
  KonamiSnesInstr(VGMInstrSet *instrSet,
                  KonamiSnesVersion ver,
                  uint32_t offset,
                  uint32_t theBank,
                  uint32_t theInstrNum,
                  uint32_t spcDirAddr,
                  bool percussion,
                  const std::wstring &name = L"KonamiSnesInstr");
  virtual ~KonamiSnesInstr(void);

  virtual bool LoadInstr();

  static bool IsValidHeader
      (RawFile *file, KonamiSnesVersion version, uint32_t addrInstrHeader, uint32_t spcDirAddr, bool validateSample);
  static uint32_t ExpectedSize(KonamiSnesVersion version);

  KonamiSnesVersion version;

 protected:
  uint32_t spcDirAddr;
  bool percussion;
};

// *************
// KonamiSnesRgn
// *************

class KonamiSnesRgn
    : public VGMRgn {
 public:
  KonamiSnesRgn(KonamiSnesInstr *instr, KonamiSnesVersion ver, uint32_t offset, bool percussion);
  virtual ~KonamiSnesRgn(void);

  virtual bool LoadRgn();

  KonamiSnesVersion version;
};
