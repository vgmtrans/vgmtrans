/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/formats/CapcomSnes/CapcomSnesLayout.h"
#include "value/sequence/SequenceDialect.h"
#include "value/base/Source.h"
#include "value/scan/ScanResultBuilder.h"
#include "value/scan/ScanTypes.h"
#include "value/formats/CapcomSnes/CapcomSnesTypes.h"

#include <optional>
#include <string_view>
#include <vector>

namespace vgmtrans::formats::capcom_snes {

struct CapcomSnesSequenceDescriptor {
  core::SequenceDialect dialect;
};

[[nodiscard]] const CapcomSnesSequenceDescriptor& capcomSnesSequenceDescriptor(CapcomSnesEngineVersion version);
void registerCapcomSnesSequenceDialects(core::SequenceDialectRegistry& registry);

[[nodiscard]] core::TrackProgram decodeCapcomSnesSourceTrack(core::ByteReader reader,
                                                             const CapcomSnesSequenceDescriptor& descriptor,
                                                             u32 sourceTrackNumber, u32 startAddress,
                                                             core::SourceMapBuilder* sourceMap = nullptr,
                                                             std::vector<core::Diagnostic>* diagnostics = nullptr);

[[nodiscard]] core::SequenceProgramAsset parseCapcomSnesSequence(
    const core::ScanInput& input, const CapcomSnesLayout& layout, core::AssetId sequenceId,
    std::optional<core::ScanInstrumentSetRef> instrumentSet, std::string_view displayName,
    core::SourceMapBuilder* sourceMap = nullptr, std::vector<core::Diagnostic>* diagnostics = nullptr);

}  // namespace vgmtrans::formats::capcom_snes
