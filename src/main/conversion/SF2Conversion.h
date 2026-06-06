/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
*/
#pragma once

#include <memory>
#include <span>
#include <vector>

class SF2File;
class VGMColl;
class VGMInstrSet;
class VGMSampColl;
class SynthFile;
class VGMSamp;
struct ConversionContext;

namespace conversion {

std::unique_ptr<SF2File> createSF2File(const VGMColl& coll);
std::unique_ptr<SF2File> createSF2File(const VGMColl& coll, const ConversionContext& context);
std::unique_ptr<SF2File> createSF2File(
  std::span<VGMInstrSet* const> instrsets,
  std::span<VGMSampColl* const> sampcolls,
  const VGMColl* coll,
  const ConversionContext& context
);
std::unique_ptr<SynthFile> createSynthFile(
  std::span<VGMInstrSet* const> instrsets,
  std::span<VGMSampColl* const> sampcolls
);
void unpackSampColl(SynthFile &synthfile, const VGMSampColl *sampColl, std::vector<VGMSamp *> &finalSamps);

} // conversion
