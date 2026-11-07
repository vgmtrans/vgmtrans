#pragma once

#include "PSDSEPS2Header.h"
#include "PSXSPU.h"
#include "VGMInstrSet.h"
#include "VGMRgn.h"
#include "VGMSampColl.h"

class PSDSEPS2SampColl : public VGMSampColl {
public:
  PSDSEPS2SampColl(RawFile* file, const PSDSEPS2::BankHeader& header);

  bool parseHeader() override;
  bool parseSampleInfo() override;

  PSDSEPS2::BankHeader m_header;
};

class PSDSEPS2InstrSet : public VGMInstrSet {
public:
  PSDSEPS2InstrSet(RawFile* file, const PSDSEPS2::BankHeader& header, PSDSEPS2SampColl* sampColl);

  bool parseHeader() override;
  bool parseInstrPointers() override;

  PSDSEPS2::BankHeader m_header;
};

class PSDSEPS2Instr : public VGMInstr {
public:
  PSDSEPS2Instr(VGMInstrSet* instrSet, uint32_t offset, uint32_t length, uint32_t bank, uint32_t instrNum,
                std::string name);

  bool loadInstr() override;
};

class PSDSEPS2Rgn : public VGMRgn {
public:
  PSDSEPS2Rgn(VGMInstr* instr, uint32_t offset, uint32_t length);

  bool loadRgn() override;
};
