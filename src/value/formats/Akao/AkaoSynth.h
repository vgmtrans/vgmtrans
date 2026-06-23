/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/formats/Akao/AkaoTypes.h"
#include "value/scan/ScanResultBuilder.h"

#include <optional>
#include <vector>

namespace vgmtrans::formats::akao {

[[nodiscard]] std::optional<AkaoInstrDatLocation> ff7HardcodedAkaoSampleLocation(core::ByteReader reader);
[[nodiscard]] bool isPossibleAkaoSampleCollection(core::ByteReader reader, u32 offset);
[[nodiscard]] std::optional<AkaoSampleCollectionParse> parseAkaoSampleCollectionData(
    const core::ScanInput& input, core::ScanSampleCollectionRef ref, u32 offset, AkaoPs1Version version);
[[nodiscard]] std::optional<AkaoSampleCollectionParse> parseAkaoSampleCollectionData(
    const core::ScanInput& input, core::ScanSampleCollectionRef ref, AkaoInstrDatLocation location);
[[nodiscard]] std::optional<AkaoSampleCollectionParse> parseAkaoSampleCollection(const core::ScanInput& input,
                                                                                 core::ScanResultBuilder& result,
                                                                                 core::ScanSampleCollectionRef ref,
                                                                                 u32 offset, AkaoPs1Version version);
[[nodiscard]] std::optional<AkaoSampleCollectionParse> parseAkaoSampleCollection(const core::ScanInput& input,
                                                                                 core::ScanResultBuilder& result,
                                                                                 core::ScanSampleCollectionRef ref,
                                                                                 AkaoInstrDatLocation location);

}  // namespace vgmtrans::formats::akao
