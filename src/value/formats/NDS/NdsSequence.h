/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/Source.h"
#include "value/formats/NDS/NdsTypes.h"
#include "value/scan/ScanResultBuilder.h"
#include "value/scan/ScanTypes.h"
#include "value/sequence/SequenceDialect.h"

#include <optional>
#include <string>
#include <vector>

namespace vgmtrans::formats::nds {

inline constexpr auto kNdsSequenceDialectId = "nds:sseq";

struct NdsSequenceDescriptor {
  core::SequenceDialect dialect;
};

[[nodiscard]] const NdsSequenceDescriptor& ndsSequenceDescriptor();
[[nodiscard]] core::SequenceDialect ndsSequenceDialect();
void registerNdsSequenceDialect(core::SequenceDialectRegistry& registry);

[[nodiscard]] core::TrackProgram decodeNdsSequenceTrack(core::ByteReader reader,
                                                        const NdsSequenceDescriptor& descriptor, u32 sequenceOffset,
                                                        u32 sequenceEnd, u32 startOffset, u32 trackIndex,
                                                        bool recoverMalformedSdatRange = false,
                                                        core::SourceMapBuilder* sourceMap = nullptr,
                                                        std::vector<core::Diagnostic>* diagnostics = nullptr);

[[nodiscard]] std::vector<u32> ndsSequenceTrackStarts(core::ByteReader reader, u32 sequenceOffset, u32 sequenceEnd);

[[nodiscard]] NdsSequenceRange ndsSequenceRangeForFatEntry(core::ByteReader reader, u32 offset, u32 size);

[[nodiscard]] core::SequenceProgramAsset parseNdsSequenceProgram(
    const core::ScanInput& input, core::AssetId id, NdsSequenceRange range, const std::string& name,
    std::optional<core::ScanInstrumentSetRef> instrumentSet, core::SourceMapBuilder* sourceMap = nullptr,
    std::vector<core::Diagnostic>* diagnostics = nullptr);

}  // namespace vgmtrans::formats::nds
