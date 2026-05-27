#pragma once

#include "base/types.h"
#include "VGMInstrSet.h"
#include "VGMSampColl.h"
#include "VGMRgn.h"
#include "TamSoftPS1Format.h"

// ******************
// TamSoftPS1InstrSet
// ******************

class TamSoftPS1InstrSet:
    public VGMInstrSet {
 public:
  TamSoftPS1InstrSet(RawFile *file, u32 offset, bool ps2, const std::string &name = "TamSoftPS1InstrSet");
  virtual ~TamSoftPS1InstrSet();

  virtual bool parseHeader();
  virtual bool parseInstrPointers();

  bool ps2;
};

// ***************
// TamSoftPS1Instr
// ***************

class TamSoftPS1Instr
    : public VGMInstr {
 public:
  TamSoftPS1Instr(TamSoftPS1InstrSet *instrSet, u8 instrNum, const std::string &name = "TamSoftPS1Instr");
  virtual ~TamSoftPS1Instr();

  virtual bool loadInstr();

 protected:
  u32 spcDirAddr;
  u32 addrSampTuningTable;
};

// *************
// TamSoftPS1Rgn
// *************

class TamSoftPS1Rgn
    : public VGMRgn {
 public:
  TamSoftPS1Rgn(TamSoftPS1Instr *instr, u32 offset, bool ps2);
  virtual ~TamSoftPS1Rgn();

  virtual bool loadRgn();
};
