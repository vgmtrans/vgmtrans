/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/formats/Akao/AkaoTypes.h"
#include "value/scan/ScanResultBuilder.h"
#include "value/sequence/SequenceDialect.h"

#include <optional>
#include <vector>

namespace vgmtrans::formats::akao {

[[nodiscard]] bool isPossibleAkaoSequence(core::ByteReader reader, u32 offset);
[[nodiscard]] std::optional<AkaoSequenceAnalysis> analyzeAkaoSequence(core::ByteReader reader,
                                                                      const core::SourceFile& source, u32 offset);
[[nodiscard]] core::SequenceProgramAsset parseAkaoSequenceProgram(
    const core::ScanInput& input, core::AssetId id, const AkaoSequenceAnalysis& analysis,
    std::optional<core::ScanInstrumentSetRef> instrumentSet, core::SourceMapBuilder* sourceMap,
    std::vector<core::Diagnostic>* diagnostics);

void registerAkaoSequenceDialects(core::SequenceDialectRegistry& registry);

}  // namespace vgmtrans::formats::akao
