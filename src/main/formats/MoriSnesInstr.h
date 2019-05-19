/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "VGMInstrSet.h"
#include "VGMSampColl.h"
#include "VGMRgn.h"
#include "MoriSnesFormat.h"

struct MoriSnesInstrHint {
    MoriSnesInstrHint()
        : startAddress(0),
          size(0),
          seqAddress(0),
          seqSize(0),
          rgnAddress(0),
          transpose(0),
          pan(0) {}

    uint16_t startAddress;
    uint16_t size;
    uint16_t seqAddress;
    uint16_t seqSize;
    uint16_t rgnAddress;
    int8_t transpose;
    int8_t pan;
};

struct MoriSnesInstrHintDir {
    MoriSnesInstrHintDir() : startAddress(0), size(0), percussion(false) {}

    uint16_t startAddress;
    uint16_t size;

    bool percussion;
    MoriSnesInstrHint instrHint;
    std::vector<MoriSnesInstrHint> percHints;
};

// ****************
// MoriSnesInstrSet
// ****************

class MoriSnesInstrSet : public VGMInstrSet {
   public:
    MoriSnesInstrSet(RawFile *file, MoriSnesVersion ver, uint32_t spcDirAddr,
                     std::vector<uint16_t> instrumentAddresses,
                     std::map<uint16_t, MoriSnesInstrHintDir> instrumentHints,
                     const std::wstring &name = L"MoriSnesInstrSet");
    virtual ~MoriSnesInstrSet(void);

    virtual bool GetHeaderInfo();
    virtual bool GetInstrPointers();

    MoriSnesVersion version;

   protected:
    uint32_t spcDirAddr;
    std::vector<uint16_t> instrumentAddresses;
    std::map<uint16_t, MoriSnesInstrHintDir> instrumentHints;
    std::vector<uint8_t> usedSRCNs;
};

// *************
// MoriSnesInstr
// *************

class MoriSnesInstr : public VGMInstr {
   public:
    MoriSnesInstr(VGMInstrSet *instrSet, MoriSnesVersion ver, uint8_t instrNum, uint32_t spcDirAddr,
                  const MoriSnesInstrHintDir &instrHintDir,
                  const std::wstring &name = L"MoriSnesInstr");
    virtual ~MoriSnesInstr(void);

    virtual bool LoadInstr();

    MoriSnesVersion version;

   protected:
    uint32_t spcDirAddr;
    MoriSnesInstrHintDir instrHintDir;
};

// ***********
// MoriSnesRgn
// ***********

class MoriSnesRgn : public VGMRgn {
   public:
    MoriSnesRgn(MoriSnesInstr *instr, MoriSnesVersion ver, uint32_t spcDirAddr,
                const MoriSnesInstrHint &instrHint, int8_t percNoteKey = -1);
    virtual ~MoriSnesRgn(void);

    virtual bool LoadRgn();

    MoriSnesVersion version;
};
