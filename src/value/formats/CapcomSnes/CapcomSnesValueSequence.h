/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/core/FormatModule.h"
#include "value/formats/CapcomSnes/CapcomSnesValueLayout.h"

#include <optional>
#include <string_view>

namespace vgmtrans::formats::capcom_snes {

[[nodiscard]] core::TrackProgram decodeCapcomSnesTrack(
    core::ByteReader reader,
    CapcomSnesEngineVersion version,
    u32 sourceTrackNumber,
    u32 startAddress);

[[nodiscard]] core::SequenceAsset parseCapcomSnesSequence(
    const core::ScanInput& input,
    const CapcomSnesLayout& layout,
    core::AssetId sequenceId,
    std::optional<core::AssetId> instrumentSetId,
    std::string_view displayName);

}  // namespace vgmtrans::formats::capcom_snes
