/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/Source.h"
#include "value/sequence/SequenceProgram.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <vector>

namespace vgmtrans::core {

class SourceMapBuilder;

// Temporary decoded form used while a bytecode decoder is deciding control flow.
// TrackProgramBuilder still owns the final immutable source-command snapshot.
struct DecodedBytecodeCommand {
  SourceRange range;
  SourceAnnotationId annotation;
  std::vector<u8> bytes;
  DecodeFlow flow;
};

struct BytecodeDecodeContext {
  // bytecodeEnd is the first offset the decoder must not read. sequenceEnd can be
  // smaller when a driver stores several sequences in one larger byte buffer.
  u32 bytecodeEnd = std::numeric_limits<u32>::max();
  u32 sequenceOffset = 0;
  u32 sequenceEnd = std::numeric_limits<u32>::max();
  std::optional<SourceAnnotationId> parentAnnotation;
  SourceMapBuilder* sourceMap = nullptr;
  std::vector<Diagnostic>* diagnostics = nullptr;
};

[[nodiscard]] inline bool hasBytecodeBytes(ByteReader reader, u32 offset, u32 size, u32 end) {
  return offset <= end && size <= end - offset && reader.has(offset, size);
}

inline void appendDecodedBytecodeCommand(TrackProgramBuilder& builder, const DecodedBytecodeCommand& decoded,
                                         u32 offset) {
  builder.addDecoded(Address{offset}, decoded.range, decoded.bytes, decoded.annotation);
}

}  // namespace vgmtrans::core
