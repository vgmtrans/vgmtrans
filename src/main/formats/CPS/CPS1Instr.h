#pragma once

#include "VGMInstrSet.h"
#include "CPS1Scanner.h"
#include "VGMSampColl.h"

// ******************
// CPS1SampleInstrSet
// ******************

class CPS1SampleInstrSet
    : public VGMInstrSet {
public:
  CPS1SampleInstrSet(RawFile *file,
                     CPSFormatVer fmt_version,
                     uint32_t offset,
                     std::string &name);
  ~CPS1SampleInstrSet() override;

  bool GetInstrPointers() override;

public:
  CPSFormatVer fmt_version;
};

// **************
// CPS1SampColl
// **************

class CPS1SampColl
    : public VGMSampColl {
public:
  CPS1SampColl(RawFile *file, CPS1SampleInstrSet *instrset, uint32_t offset,
               uint32_t length = 0, std::string name = std::string("CPS1 Sample Collection"));
  bool GetHeaderInfo() override;
  bool GetSampleInfo() override;

private:
  std::vector<VGMItem*> samplePointers;
  CPS1SampleInstrSet *instrset;
};