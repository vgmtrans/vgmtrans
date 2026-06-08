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

namespace vgmtrans::psf {

[[nodiscard]] std::vector<VGMMetadataHint> collectMetadataHintsForOpenedFile(
    const RawFile& file, const PSFFile& psf);

}  // namespace vgmtrans::psf
