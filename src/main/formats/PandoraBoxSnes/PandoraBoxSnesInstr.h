#pragma once

#include "util/types.h"
#include "VGMInstrSet.h"
#include "VGMSampColl.h"
#include "VGMRgn.h"
#include "PandoraBoxSnesFormat.h"

// **********************
// PandoraBoxSnesInstrSet
// **********************

class PandoraBoxSnesInstrSet:
    public VGMInstrSet {
 public:
  PandoraBoxSnesInstrSet(RawFile *file,
                         PandoraBoxSnesVersion ver,
                         u32 spcDirAddr,
                         u16 addrLocalInstrTable,
                         u16 addrGlobalInstrTable,
                         u8 globalInstrumentCount,
                         const std::map<u8, u16> &instrADSRHints = std::map<u8, u16>(),
                         const std::string &name = "PandoraBoxSnesInstrSet");
  virtual ~PandoraBoxSnesInstrSet();

  virtual bool parseHeader();
  virtual bool parseInstrPointers();

  PandoraBoxSnesVersion version;

 protected:
  u32 spcDirAddr;
  u16 addrLocalInstrTable;
  u16 addrGlobalInstrTable;
  u8 globalInstrumentCount;
  std::vector<u8> globalInstrTable;
  std::map<u8, u16> instrADSRHints;
  std::vector<u8> usedSRCNs;
};

// *******************
// PandoraBoxSnesInstr
// *******************

class PandoraBoxSnesInstr
    : public VGMInstr {
 public:
  PandoraBoxSnesInstr(VGMInstrSet *instrSet,
                      PandoraBoxSnesVersion ver,
                      u32 offset,
                      u8 theInstrNum,
                      u8 srcn,
                      u32 spcDirAddr,
                      u16 adsr = 0x8fe0,
                      const std::string &name = "PandoraBoxSnesInstr");
  virtual ~PandoraBoxSnesInstr();

  virtual bool loadInstr();

  PandoraBoxSnesVersion version;

 protected:
  u32 spcDirAddr;
  u8 srcn;
  u16 adsr;
};

// *****************
// PandoraBoxSnesRgn
// *****************

class PandoraBoxSnesRgn
    : public VGMRgn {
 public:
  PandoraBoxSnesRgn(PandoraBoxSnesInstr *instr,
                    PandoraBoxSnesVersion ver,
                    u32 offset,
                    u8 srcn,
                    u32 spcDirAddr,
                    u16 adsr = 0x8fe0);
  virtual ~PandoraBoxSnesRgn();

  virtual bool loadRgn();

  PandoraBoxSnesVersion version;
};
