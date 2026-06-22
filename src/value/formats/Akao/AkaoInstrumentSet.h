/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/formats/Akao/AkaoTypes.h"
#include "value/scan/ScanResultBuilder.h"

#include <vector>

namespace vgmtrans::formats::akao {

struct AkaoInstrumentSetParse {
  core::InstrumentSetAsset asset;
  std::vector<u32> requiredArticulations;
};

[[nodiscard]] AkaoInstrumentSetParse parseAkaoInstrumentSet(const core::ScanInput& input, core::AssetId id,
                                                            const AkaoSequenceAnalysis& sequence,
                                                            const AkaoArtMap& artMap);
[[nodiscard]] std::vector<u32> requiredArticulations(core::ByteReader reader, const AkaoSequenceAnalysis& sequence);

}  // namespace vgmtrans::formats::akao
