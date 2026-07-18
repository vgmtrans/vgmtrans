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
#include "value/sequence/BytecodeDecode.h"
#include "value/sequence/SequenceDialect.h"

#include <string>
#include <vector>

namespace vgmtrans::formats::nds {

inline constexpr auto kNdsSequenceDialectId = "nds:sseq";

[[nodiscard]] const core::SequenceDialect& ndsSequenceDialect();
void registerNdsSequenceDialect(core::SequenceDialectRegistry& registry);

[[nodiscard]] core::TrackProgram decodeNdsSequenceTrack(core::ByteReader reader, core::TrackDecodeInput input,
                                                        bool recoverMalformedSdatRange = false);

[[nodiscard]] std::vector<u32> ndsSequenceTrackAddresses(core::ByteReader reader, u32 sequenceOffset, u32 sequenceEnd);

[[nodiscard]] NdsSequenceRange ndsSequenceRangeForFatEntry(core::ByteReader reader, u32 offset, u32 size);

[[nodiscard]] core::SequenceProgramAsset parseNdsSequenceProgram(const core::ScanInput& input, core::AssetId id,
                                                                 NdsSequenceRange range, const std::string& name,
                                                                 core::SourceMapBuilder* sourceMap = nullptr,
                                                                 std::vector<core::Diagnostic>* diagnostics = nullptr);

}  // namespace vgmtrans::formats::nds
