/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
*/
#pragma once

#include <memory>
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
  const std::vector<VGMInstrSet*>& instrsets,
  const std::vector<VGMSampColl*>& sampcolls,
  const VGMColl* coll,
  const ConversionContext& context
);
std::unique_ptr<SynthFile> createSynthFile(
  const std::vector<VGMInstrSet*>& instrsets,
  const std::vector<VGMSampColl*>& sampcolls
);
void unpackSampColl(SynthFile &synthfile, const VGMSampColl *sampColl, std::vector<VGMSamp *> &finalSamps);

} // conversion
