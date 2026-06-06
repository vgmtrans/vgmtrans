/*
 * VGMTrans (c) - 2002-2024
 * Licensed under the zlib license
 * See the included LICENSE for more information
*/
#pragma once

#include <span>
#include <vector>

class DLSFile;
class VGMInstrSet;
class VGMSampColl;
class VGMSamp;
class VGMColl;
struct ConversionContext;

namespace conversion {

bool createDLSFile(DLSFile& dls, const VGMColl& coll);
bool createDLSFile(DLSFile& dls, const VGMColl& coll, const ConversionContext& context);
bool createDLSFile(
  DLSFile& dls,
  std::span<VGMInstrSet* const> instrsets,
  std::span<VGMSampColl* const> sampcolls,
  const VGMColl* coll,
  const ConversionContext& context
);
bool mainDLSCreation(
  DLSFile& dls,
  std::span<VGMInstrSet* const> instrsets,
  std::span<VGMSampColl* const> sampcolls,
  const ConversionContext& context
);
void unpackSampColl(DLSFile& dls, const VGMSampColl* sampColl, std::vector<VGMSamp*>& finalSamps);

} // conversion
