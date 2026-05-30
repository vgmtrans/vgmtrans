#pragma once

#include "base/Types.h"
#include "KonamiSnesFormat.h"
#include "VGMInstrSet.h"
#include "VGMRgn.h"
#include "VGMSampColl.h"

#include <string>
#include <vector>

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
  ~KonamiSnesInstrSet() override;

  bool parseHeader() override;
  bool parseInstrPointers() override;
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
  ~KonamiSnesInstr() override;

  bool loadInstr() override;

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
  ~KonamiSnesRgn() override;

  bool loadRgn() override;
};
