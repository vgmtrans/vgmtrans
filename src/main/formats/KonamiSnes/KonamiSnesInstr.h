#pragma once

#include "Types.h"
#include "VGMInstrSet.h"
#include "VGMSampColl.h"
#include "VGMRgn.h"
#include "KonamiSnesFormat.h"

class VGMColl;

// ******************
// KonamiSnesInstrSet
// ******************

class KonamiSnesInstrSet:
    public VGMInstrSet {
 public:
  static constexpr u32 DRUMKIT_PROGRAM = (0x7F << 7);

  KonamiSnesInstrSet(RawFile *file,
                     KonamiSnesVersion ver,
                     u32 offset,
                     u32 bankedInstrOffset,
                     u8 firstBankedInstr,
                     u32 percInstrOffset,
                     u32 spcDirAddr,
                     const std::string &name = "KonamiSnesInstrSet");
  virtual ~KonamiSnesInstrSet();

  virtual bool parseHeader();
  virtual bool parseInstrPointers();
  void useColl(const VGMColl* coll) override;
  void unuseColl() override;

  KonamiSnesVersion version;

 protected:
  u32 bankedInstrOffset;
  u8 firstBankedInstr;
  u32 percInstrOffset;
  u32 spcDirAddr;
  std::vector<u8> usedSRCNs;
};

// ***************
// KonamiSnesInstr
// ***************

class KonamiSnesInstr
    : public VGMInstr {
 public:
  KonamiSnesInstr(VGMInstrSet *instrSet,
                  KonamiSnesVersion ver,
                  u32 offset,
                  u32 theBank,
                  u32 theInstrNum,
                  u32 spcDirAddr,
                  bool percussion,
                  const std::string &name = "KonamiSnesInstr");
  virtual ~KonamiSnesInstr();

  virtual bool loadInstr();

  static bool isValidHeader
      (RawFile *file, KonamiSnesVersion version, u32 addrInstrHeader, u32 spcDirAddr, bool validateSample);
  static u32 expectedSize(KonamiSnesVersion version);

  KonamiSnesVersion version;

 protected:
  u32 spcDirAddr;
  bool percussion;
};

// *************
// KonamiSnesRgn
// *************

class KonamiSnesRgn
    : public VGMRgn {
 public:
  KonamiSnesRgn(KonamiSnesInstr *instr,
                KonamiSnesVersion ver,
                u32 offset,
                bool percussion,
                u8 percussionNote = 0);
  virtual ~KonamiSnesRgn();

  virtual bool loadRgn();
};
