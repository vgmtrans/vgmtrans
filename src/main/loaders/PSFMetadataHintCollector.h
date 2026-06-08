/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "components/VGMMetadataHint.h"

#include <vector>

class PSFFile;
class RawFile;

class PSFMetadataHintCollector {
public:
  [[nodiscard]] std::vector<VGMMetadataHint> collectForOpenedFile(
      const RawFile& file, const PSFFile& psf) const;
};
