/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "value/formats/AkaoSnes/AkaoSnesLayout.h"
#include "value/scan/ScanTypes.h"
#include "value/sequence/SequenceDialect.h"

#include <optional>
#include <string_view>
#include <vector>

namespace vgmtrans::formats::akao_snes {

struct AkaoSnesSequenceDescriptor {
  core::SequenceDialect dialect;
};

[[nodiscard]] AkaoSnesSequenceDescriptor akaoSnesSequenceDescriptor(AkaoSnesVersion version,
                                                                    AkaoSnesMinorVersion minorVersion);
void registerAkaoSnesSequenceDialects(core::SequenceDialectRegistry& registry);

[[nodiscard]] core::TrackProgram decodeAkaoSnesSourceTrack(
    core::ByteReader reader, const AkaoSnesSequenceDescriptor& descriptor, u32 sourceTrackNumber, u32 startAddress,
    u32 bytecodeEnd, u32 sequenceOffset, u32 sequenceEnd, core::SourceMapBuilder* sourceMap = nullptr,
    std::vector<core::Diagnostic>* diagnostics = nullptr, std::optional<core::SourceAnnotationId> parent = std::nullopt,
    std::optional<core::AssetId> sequenceAsset = std::nullopt);

[[nodiscard]] core::SequenceProgramAsset parseAkaoSnesSequence(const core::ScanInput& input,
                                                               const AkaoSnesLayout& layout, core::AssetId sequenceId,
                                                               std::string_view displayName,
                                                               core::SourceMapBuilder* sourceMap = nullptr,
                                                               std::vector<core::Diagnostic>* diagnostics = nullptr);

}  // namespace vgmtrans::formats::akao_snes
