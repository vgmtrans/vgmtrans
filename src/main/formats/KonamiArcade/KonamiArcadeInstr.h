/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "KonamiArcadeScanner.h"
#include "VGMSampColl.h"
#include "VGMInstrSet.h"

// ********************
// KonamiArcadeInstrSet
// ********************

class KonamiArcadeInstrSet
    : public VGMInstrSet {
public:
  KonamiArcadeInstrSet(RawFile *file,
                     uint32_t offset,
                     std::string name);
  ~KonamiArcadeInstrSet() override = default;

  bool parseInstrPointers() override;
};

// ********************
// KonamiArcadeSampColl
// ********************

class KonamiArcadeSampColl
    : public VGMSampColl {
public:
  KonamiArcadeSampColl(
    RawFile* file,
    KonamiArcadeInstrSet* instrset,
    std::vector<konami_mw_sample_info>& sampInfos,
    u32 offset,
    u32 length = 0,
    std::string name = std::string("Konami MW Sample Collection")
  );
  bool parseHeader() override;
  bool parseSampleInfo() override;

private:
  u32 determineSampleSize(u32 startOffset, konami_mw_sample_info::sample_type type);

  std::vector<VGMItem*> samplePointers;
  KonamiArcadeInstrSet *instrset;
  std::vector<konami_mw_sample_info> sampInfos;
};
