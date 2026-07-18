/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/sequence/BytecodeDecode.h"

#include <optional>

namespace vgmtrans::core {

// Project one already-decoded semantic command into the durable SourceMap.
// Format decoders provide data; this function owns the annotation mechanics.
[[nodiscard]] SourceAnnotationId projectDecodedCommand(SourceMapBuilder* sourceMap,
                                                       const DecodedBytecodeCommand& command,
                                                       std::optional<SourceAnnotationId> parent = std::nullopt);

// Track annotations have the same lifecycle for semantic and cursor dialects:
// create the parent before command decoding, then expand it to the discovered
// command range afterward.
[[nodiscard]] std::optional<SourceAnnotationId> createSequenceTrackAnnotation(ByteReader reader,
                                                                              const TrackDecodeInput& input);
void finishSequenceTrackAnnotation(ByteReader reader, const TrackDecodeInput& input,
                                   std::optional<SourceAnnotationId> annotation, const TrackProgram& track);

}  // namespace vgmtrans::core
