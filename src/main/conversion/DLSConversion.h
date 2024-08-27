/*
 * VGMTrans (c) - 2002-2024
 * Licensed under the zlib license
 * See the included LICENSE for more information
*/
#pragma once

#include <vector>

class DLSFile;
class VGMInstrSet;
class VGMSampColl;
class VGMSamp;
class VGMColl;

namespace conversion {

bool createDLSFile(DLSFile& dls, const VGMColl& coll);
bool createDLSFile(
  DLSFile& dls,
  const std::vector<VGMInstrSet*>& instrsets,
  const std::vector<VGMSampColl*>& sampcolls,
  const VGMColl* coll
);
bool mainDLSCreation(
  DLSFile& dls,
  const std::vector<VGMInstrSet*>& instrsets,
  const std::vector<VGMSampColl*>& sampcolls
);
void unpackSampColl(DLSFile& dls, const VGMSampColl* sampColl, std::vector<VGMSamp*>& finalSamps);

} // conversion

