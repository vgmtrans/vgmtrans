#pragma once
#include "VGMInstrSet.h"
#include "VGMSampColl.h"
#include "VGMRgn.h"
#include "NamcoSnesFormat.h"

// *****************
// NamcoSnesInstrSet
// *****************

class NamcoSnesInstrSet:
    public VGMInstrSet {
 public:
  NamcoSnesInstrSet(RawFile *file,
                    NamcoSnesVersion ver,
                    uint32_t spcDirAddr,
                    uint16_t addrTuningTable,
                    const std::string &name = "NamcoSnesInstrSet");
  virtual ~NamcoSnesInstrSet();

  virtual bool parseHeader();
  virtual bool parseInstrPointers();

  NamcoSnesVersion version;

 protected:
  uint32_t spcDirAddr;
  uint16_t addrTuningTable;
  std::vector<uint8_t> usedSRCNs;
};

// **************
// NamcoSnesInstr
// **************

class NamcoSnesInstr
    : public VGMInstr {
 public:
  NamcoSnesInstr(VGMInstrSet *instrSet,
                 NamcoSnesVersion ver,
                 uint8_t srcn,
                 uint32_t spcDirAddr,
                 uint16_t addrTuningEntry,
                 const std::string &name = "NamcoSnesInstr");
  virtual ~NamcoSnesInstr();

  virtual bool loadInstr();

  NamcoSnesVersion version;

 protected:
  uint32_t spcDirAddr;
  uint16_t addrTuningEntry;
};

// ************
// NamcoSnesRgn
// ************

class NamcoSnesRgn
    : public VGMRgn {
 public:
  NamcoSnesRgn
      (NamcoSnesInstr *instr, NamcoSnesVersion ver, uint8_t srcn, uint32_t spcDirAddr, uint16_t addrTuningEntry);
  virtual ~NamcoSnesRgn();

  virtual bool loadRgn();

  NamcoSnesVersion version;
};
