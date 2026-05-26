#pragma once

#include "util/types.h"
#include "VGMInstrSet.h"
#include "VGMRgn.h"
#include "HeartBeatSnesFormat.h"

// *********************
// HeartBeatSnesInstrSet
// *********************

class HeartBeatSnesInstrSet:
    public VGMInstrSet {
 public:
  HeartBeatSnesInstrSet(RawFile *file,
                        HeartBeatSnesVersion ver,
                        u32 offset,
                        u32 length,
                        u16 addrSRCNTable,
                        u8 songIndex,
                        u32 spcDirAddr,
                        const std::string &name = "HeartBeatSnesInstrSet");
  ~HeartBeatSnesInstrSet() override;

  bool parseHeader() override;
  bool parseInstrPointers() override;

  HeartBeatSnesVersion version;

 protected:
  u16 addrSRCNTable;
  u8 songIndex;
  u32 spcDirAddr;
  std::vector<u8> usedSRCNs;
};

// ******************
// HeartBeatSnesInstr
// ******************

class HeartBeatSnesInstr
    : public VGMInstr {
 public:
  HeartBeatSnesInstr(VGMInstrSet *instrSet,
                     HeartBeatSnesVersion ver,
                     u32 offset,
                     u32 theBank,
                     u32 theInstrNum,
                     u16 addrSRCNTable,
                     u8 songIndex,
                     u32 spcDirAddr,
                     const std::string &name = "HeartBeatSnesInstr");
  ~HeartBeatSnesInstr() override;

  bool loadInstr() override;

  HeartBeatSnesVersion version;

 protected:
  u16 addrSRCNTable;
  u8 songIndex;
  u32 spcDirAddr;
};

// ****************
// HeartBeatSnesRgn
// ****************

class HeartBeatSnesRgn
    : public VGMRgn {
 public:
  HeartBeatSnesRgn(HeartBeatSnesInstr *instr, HeartBeatSnesVersion ver, u32 offset);
  ~HeartBeatSnesRgn() override;

  bool loadRgn() override;

  HeartBeatSnesVersion version;
};
