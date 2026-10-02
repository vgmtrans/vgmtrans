/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/Source.h"
#include "value/sequence/SequenceProgram.h"

#include <string>
#include <string_view>
#include <vector>

namespace vgmtrans::core {

// Presentation is transient decode output used by one shared projector to build
// source annotations without teaching format execution about SourceMapBuilder.
struct DecodedCommandPresentation {
  std::string label;
  std::string kind;
  SequenceSemantic semantic = SequenceSemantic::Unknown;
  CommandPlaybackStatus playback = CommandPlaybackStatus::AffectsPlayback;
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

// Only tagged values are needed after decoding for channel attribution and
// instrument/control-flow links. Their display fields use SourceField directly.
struct SemanticOperand {
  SourceValue value = false;
  SemanticOperandRole role = SemanticOperandRole::Value;
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
  std::string_view name;
  SourceValueDisplay display = SourceValueDisplay::Default;
};

[[nodiscard]] inline bool hasBytecodeBytes(ByteReader reader, u32 offset, u32 size, u32 end) {
  return offset <= end && size <= end - offset && reader.has(offset, size);
}

}  // namespace vgmtrans::core
