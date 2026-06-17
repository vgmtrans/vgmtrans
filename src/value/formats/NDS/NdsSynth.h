/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/formats/NDS/NdsTypes.h"
#include "value/scan/ScanResultBuilder.h"

#include <array>
#include <optional>
#include <string>

namespace vgmtrans::formats::nds {

[[nodiscard]] bool isNdsWaveArchive(core::ByteReader reader, u32 offset);

[[nodiscard]] core::SampleCollectionAsset parseNdsPsgSamples(const core::ScanInput& input, core::AssetId id);

[[nodiscard]] core::SampleCollectionAsset parseNdsWaveArchive(const core::ScanInput& input, core::AssetId id,
                                                              NdsFileRange range, const std::string& name,
                                                              core::ScanResultBuilder* diagnostics = nullptr);

[[nodiscard]] core::InstrumentSetAsset parseNdsInstrumentSet(
    const core::ScanInput& input, core::AssetId id, NdsFileRange range, const std::string& name,
    core::ScanResultBuilder& builder, core::ScanSampleCollectionRef psgCollection,
    const std::array<std::optional<core::ScanSampleCollectionRef>, 4>& waveCollections);

}  // namespace vgmtrans::formats::nds
