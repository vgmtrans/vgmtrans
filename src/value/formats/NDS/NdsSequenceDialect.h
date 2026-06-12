/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/core/SequenceDialect.h"
#include "value/core/Source.h"

#include <vector>

namespace vgmtrans::formats::nds {

inline constexpr auto kNdsSequenceDialectId = "nds:sseq";

[[nodiscard]] core::SequenceDialect ndsSequenceDialect();
void registerNdsSequenceDialect(core::SequenceDialectRegistry& registry);

[[nodiscard]] core::TrackProgram decodeNdsSequenceTrack(core::ByteReader reader, const core::SequenceDialect& dialect,
                                                        u32 sequenceOffset, u32 sequenceEnd, u32 startOffset,
                                                        u32 trackIndex, bool recoverMalformedSdatRange = false);

[[nodiscard]] std::vector<u32> ndsSequenceTrackStarts(core::ByteReader reader, u32 sequenceOffset, u32 sequenceEnd);

}  // namespace vgmtrans::formats::nds
