#pragma once

#include "base/types.h"
#include "VGMInstrSet.h"
#include "VGMRgn.h"
#include "GraphResSnesFormat.h"

// ********************
// GraphResSnesInstrSet
// ********************

class GraphResSnesInstrSet : public VGMInstrSet {
 public:
  GraphResSnesInstrSet(RawFile *file,
                       GraphResSnesVersion ver,
                       u32 spcDirAddr,
                       const std::map<u8, u16> &instrADSRHints = std::map<u8, u16>(),
                       const std::string &name = "GraphResSnesInstrSet");
  ~GraphResSnesInstrSet() override;

  bool parseHeader() override;
  bool parseInstrPointers() override;

  GraphResSnesVersion version;

 protected:
  u32 spcDirAddr;
  std::map<u8, u16> instrADSRHints;
  std::vector<u8> usedSRCNs;
};

// *****************
// GraphResSnesInstr
// *****************

class GraphResSnesInstr : public VGMInstr {
 public:
  GraphResSnesInstr(VGMInstrSet *instrSet,
                    GraphResSnesVersion ver,
                    u8 srcn,
                    u32 spcDirAddr,
                    u16 adsr = 0x8fe0,
                    const std::string &name = "GraphResSnesInstr");
  ~GraphResSnesInstr() override;

  bool loadInstr() override;

  GraphResSnesVersion version;

 protected:
  u32 spcDirAddr;
  u16 adsr;
};

// ***************
// GraphResSnesRgn
// ***************

class GraphResSnesRgn : public VGMRgn {
 public:
  GraphResSnesRgn
      (GraphResSnesInstr *instr, GraphResSnesVersion ver, u8 srcn, u32 spcDirAddr, u16 adsr = 0x8fe0);
  ~GraphResSnesRgn() override;

  bool loadRgn() override;

  GraphResSnesVersion version;
};
