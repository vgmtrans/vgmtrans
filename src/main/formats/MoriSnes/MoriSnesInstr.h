#pragma once

#include "base/Types.h"
#include "MoriSnesFormat.h"
#include "VGMInstrSet.h"
#include "VGMRgn.h"
#include "VGMSampColl.h"

#include <map>
#include <string>
#include <vector>

struct MoriSnesInstrHint {
  MoriSnesInstrHint() :
      startAddress(0),
      size(0),
      seqAddress(0),
      seqSize(0),
      rgnAddress(0),
      transpose(0),
      pan(0) {
  }

  u16 startAddress;
  u16 size;
  u16 seqAddress;
  u16 seqSize;
  u16 rgnAddress;
  s8 transpose;
  s8 pan;
};

struct MoriSnesInstrHintDir {
  MoriSnesInstrHintDir() :
      startAddress(0),
      size(0),
      percussion(false) {
  }

  u16 startAddress;
  u16 size;

  bool percussion;
  MoriSnesInstrHint instrHint;
  std::vector<MoriSnesInstrHint> percHints;
};

// ****************
// MoriSnesInstrSet
// ****************

class MoriSnesInstrSet:
    public VGMInstrSet {
 public:
  MoriSnesInstrSet(RawFile *file,
                   MoriSnesVersion ver,
                   u32 spcDirAddr,
                   std::vector<u16> instrumentAddresses,
                   std::map<u16, MoriSnesInstrHintDir> instrumentHints,
                   const std::string &name = "MoriSnesInstrSet");
  virtual ~MoriSnesInstrSet();

  virtual bool parseHeader();
  virtual bool parseInstrPointers();

  MoriSnesVersion version;

 protected:
  u32 spcDirAddr;
  std::vector<u16> instrumentAddresses;
  std::map<u16, MoriSnesInstrHintDir> instrumentHints;
  std::vector<u8> usedSRCNs;
};

// *************
// MoriSnesInstr
// *************

class MoriSnesInstr
    : public VGMInstr {
 public:
  MoriSnesInstr(VGMInstrSet *instrSet,
                MoriSnesVersion ver,
                u8 instrNum,
                u32 spcDirAddr,
                const MoriSnesInstrHintDir &instrHintDir,
                const std::string &name = "MoriSnesInstr");
  virtual ~MoriSnesInstr();

  virtual bool loadInstr();

  MoriSnesVersion version;

 protected:
  u32 spcDirAddr;
  MoriSnesInstrHintDir instrHintDir;
};

// ***********
// MoriSnesRgn
// ***********

class MoriSnesRgn
    : public VGMRgn {
 public:
  MoriSnesRgn(MoriSnesInstr *instr,
              MoriSnesVersion ver,
              u32 spcDirAddr,
              const MoriSnesInstrHint &instrHint,
              s8 percNoteKey = -1);
  virtual ~MoriSnesRgn();

  virtual bool loadRgn();

  MoriSnesVersion version;
};
