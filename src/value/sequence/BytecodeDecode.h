/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/Source.h"
#include "value/sequence/SequenceProgram.h"

#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace vgmtrans::core {

// Presentation is transient decode output used by one shared projector to build
// source annotations without teaching format execution about SourceMapBuilder.
struct DecodedCommandPresentation {
  std::string label;
  std::string kind;
  SequenceSemantic semantic = SequenceSemantic::Unknown;
};

// Roles identify relationships used by source links, channel attribution, and
// instrument discovery. Ordinary values need only a name and display style.
enum class SemanticOperandRole : u8 {
  Value,
  Channel,
  JumpTarget,
  CallTarget,
  LoopTarget,
  RepeatTarget,
  Instrument,
  InstrumentBank,
  InstrumentProgram,
  InstrumentTablePointer,
};

// Relationships use unsigned values. An empty value retains the position of a
// non-unsigned tag; its full display value remains in SourceField.
struct SemanticOperand {
  std::optional<u64> value;
  SemanticOperandRole role = SemanticOperandRole::Value;

  [[nodiscard]] std::optional<u32> unsigned32() const {
    return value && *value <= std::numeric_limits<u32>::max() ? std::optional{static_cast<u32>(*value)}
                                                           : std::nullopt;
  }
};

// Temporary decoded form used for reachability, source annotation projection,
// and any format-specific analysis that must observe command fields.
struct DecodedBytecodeCommand {
  SourceRange range;
  u8 opcode = 0;
  CommandFlow flow;
  std::vector<Address> discoveryTargets;
  std::vector<SourceField> fields;
  std::vector<SemanticOperand> operands;
  CommandExecution execution;
  DecodedCommandPresentation presentation;
};

// A raw source field held until its interpreted value is known. The compiler
// records both display forms without making playback know about source bytes.
template <class T>
struct EncodedSemanticField : RangedValue<T> {
  std::string name;
  SourceValueDisplay display = SourceValueDisplay::Default;
};

[[nodiscard]] inline bool hasBytecodeBytes(ByteReader reader, u32 offset, u32 size, u32 end) {
  return offset <= end && size <= end - offset && reader.has(offset, size);
}

}  // namespace vgmtrans::core
