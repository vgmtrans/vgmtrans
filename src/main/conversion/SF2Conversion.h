/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
*/
#pragma once

#include <vector>

class SF2File;
class VGMColl;
class VGMInstrSet;
class VGMSampColl;
class SynthFile;
class VGMSamp;

namespace conversion {

SF2File* createSF2File(const VGMColl& coll);
SF2File* createSF2File(
  const std::vector<VGMInstrSet*>& instrsets,
  const std::vector<VGMSampColl*>& sampcolls,
  const VGMColl* coll
);
SynthFile* createSynthFile(
  const std::vector<VGMInstrSet*>& instrsets,
  const std::vector<VGMSampColl*>& sampcolls
);
void unpackSampColl(SynthFile &synthfile, const VGMSampColl *sampColl, std::vector<VGMSamp *> &finalSamps);

} // conversion
