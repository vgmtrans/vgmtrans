/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/core/ScanTypes.h"

#include <optional>
#include <string>

namespace vgmtrans::formats::nds {

struct NdsSequenceRange {
  u32 offset = 0;
  u32 size = 0;
  u32 sequenceEnd = 0;
  bool recoverMalformedSdatRange = false;
};

[[nodiscard]] core::SequenceProgramAsset parseNdsSequenceProgram(const core::ScanInput& input, core::AssetId id,
                                                                 NdsSequenceRange range, const std::string& name,
                                                                 std::optional<core::AssetId> instrumentSet);

}  // namespace vgmtrans::formats::nds
