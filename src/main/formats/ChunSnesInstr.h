/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "VGMInstrSet.h"
#include "VGMSampColl.h"
#include "VGMRgn.h"
#include "ChunSnesFormat.h"

// ****************
// ChunSnesInstrSet
// ****************

class ChunSnesInstrSet : public VGMInstrSet {
   public:
    ChunSnesInstrSet(RawFile *file, ChunSnesVersion ver, uint16_t addrInstrSetTable,
                     uint16_t addrSampNumTable, uint16_t addrSampleTable, uint32_t spcDirAddr,
                     const std::wstring &name = L"ChunSnesInstrSet");
    virtual ~ChunSnesInstrSet(void);

    virtual bool GetHeaderInfo();
    virtual bool GetInstrPointers();

    ChunSnesVersion version;

   protected:
    uint32_t spcDirAddr;
    uint16_t addrSampNumTable;
    uint16_t addrSampleTable;
    std::vector<uint8_t> usedSRCNs;
};

// *************
// ChunSnesInstr
// *************

class ChunSnesInstr : public VGMInstr {
   public:
    ChunSnesInstr(VGMInstrSet *instrSet, ChunSnesVersion ver, uint8_t theInstrNum,
                  uint16_t addrInstr, uint16_t addrSampleTable, uint32_t spcDirAddr,
                  const std::wstring &name = L"ChunSnesInstr");
    virtual ~ChunSnesInstr(void);

    virtual bool LoadInstr();

    ChunSnesVersion version;

   protected:
    uint16_t addrSampleTable;
    uint32_t spcDirAddr;
};

// ***********
// ChunSnesRgn
// ***********

class ChunSnesRgn : public VGMRgn {
   public:
    ChunSnesRgn(ChunSnesInstr *instr, ChunSnesVersion ver, uint8_t srcn, uint16_t addrRgn,
                uint32_t spcDirAddr);
    virtual ~ChunSnesRgn(void);

    virtual bool LoadRgn();

    ChunSnesVersion version;
};
