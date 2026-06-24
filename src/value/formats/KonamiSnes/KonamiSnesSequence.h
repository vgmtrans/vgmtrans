/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "value/formats/KonamiSnes/KonamiSnesLayout.h"
#include "value/scan/ScanTypes.h"
#include "value/sequence/SequenceDialect.h"

#include <optional>
#include <string_view>
#include <vector>

namespace vgmtrans::formats::konami_snes {

struct KonamiSnesSequenceDescriptor {
  core::SequenceDialect dialect;
};

[[nodiscard]] const KonamiSnesSequenceDescriptor& konamiSnesSequenceDescriptor(KonamiSnesVersion version);
void registerKonamiSnesSequenceDialects(core::SequenceDialectRegistry& registry);

[[nodiscard]] core::TrackProgram decodeKonamiSnesSourceTrack(core::ByteReader reader,
                                                             const KonamiSnesSequenceDescriptor& descriptor,
                                                             u32 sourceTrackNumber, u32 startAddress,
                                                             core::SourceMapBuilder* sourceMap = nullptr,
                                                             std::vector<core::Diagnostic>* diagnostics = nullptr,
                                                             std::optional<core::SourceAnnotationId> parent =
                                                                 std::nullopt,
                                                             std::optional<core::AssetId> sequenceAsset =
                                                                 std::nullopt);

[[nodiscard]] core::SequenceProgramAsset parseKonamiSnesSequence(
    const core::ScanInput& input, const KonamiSnesLayout& layout, core::AssetId sequenceId,
    std::string_view displayName, core::SourceMapBuilder* sourceMap = nullptr,
    std::vector<core::Diagnostic>* diagnostics = nullptr);

}  // namespace vgmtrans::formats::konami_snes
