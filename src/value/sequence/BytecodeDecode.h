/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/Source.h"
#include "value/sequence/SequenceProgram.h"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
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

using SemanticOperandValue = std::variant<bool, u64, s64, double, Address, std::string>;

// The role is intentionally small and format-independent. Operand names are
// each format's vocabulary; roles let generic projection and transient format
// analysis recognize the few relationships shared by all drivers.
enum class SemanticOperandRole : u8 {
  Value,
  Channel,
  NoteKey,
  Duration,
  Pitch,
  Level,
  Pan,
  Modulation,
  State,
  Count,
  Address,
  JumpTarget,
  CallTarget,
  LoopTarget,
  RepeatTarget,
  Instrument,
  InstrumentBank,
  InstrumentProgram,
  InstrumentTablePointer,
};

struct SemanticOperand {
  SemanticOperandValue value;
  SourceRange range;
  std::string name;
  SourceValueDisplay display = SourceValueDisplay::Default;
  SemanticOperandRole role = SemanticOperandRole::Value;
  std::optional<SemanticOperandValue> encodedValue;
  std::string encodedName;
  SourceValueDisplay encodedDisplay = SourceValueDisplay::Default;
};

// Temporary decoded form used for reachability, source annotation projection,
// and any format-specific analysis that must observe command fields.
struct DecodedBytecodeCommand {
  SourceRange range;
  u8 opcode = 0;
  CommandFlow flow;
  std::vector<Address> discoveryTargets;
  std::vector<SemanticOperand> operands;
  CommandExecution execution;
  DecodedCommandPresentation presentation;
};

// A source field whose encoded value is replaced by an interpreted value in
// executable IR. SourceMap projection retains both forms without making
// playback know about source bytes.
template <class T>
struct EncodedSemanticField {
  T value{};
  SourceRange range;
  std::string_view name;
  SourceValueDisplay display = SourceValueDisplay::Default;
  bool valid = false;
};

[[nodiscard]] inline bool hasBytecodeBytes(ByteReader reader, u32 offset, u32 size, u32 end) {
  return offset <= end && size <= end - offset && reader.has(offset, size);
}

// Follow the track start, jumps, and calls to find every command that can be reached.
// The caller owns the ordered command buffer and final TrackProgram assembly.
template <class CommandBuffer, class DecodeCommand>
void decodeBytecode(ByteReader reader, u32 bytecodeEnd, std::span<const Address> startAddresses, u32 maxCommands,
                    CommandBuffer& commands, DecodeCommand decodeCommand) {
  std::vector<u32> pendingBlocks;
  pendingBlocks.reserve(startAddresses.size());
  for (const Address start : startAddresses) {
    pendingBlocks.push_back(static_cast<u32>(start.value));
  }
  size_t decodedCommands = 0;

  while (!pendingBlocks.empty() && decodedCommands < maxCommands) {
    u32 offset = pendingBlocks.back();
    pendingBlocks.pop_back();
    while (hasBytecodeBytes(reader, offset, 1, bytecodeEnd) && !commands.hasCommand(offset) &&
           decodedCommands < maxCommands) {
      auto decoded = decodeCommand(offset);
      ++decodedCommands;
      // Jump and call targets start new blocks. The next sequential command stays
      // in this inner loop.
      if (const auto target = decoded.flow.defaultDestination();
          target && target->value < bytecodeEnd && !commands.hasCommand(target->value)) {
        pendingBlocks.push_back(target->value);
      }
      for (const Address target : decoded.discoveryTargets) {
        if (target.value < bytecodeEnd && !commands.hasCommand(static_cast<u32>(target.value))) {
          pendingBlocks.push_back(target.value);
        }
      }
      const auto next = decoded.flow.discoveryContinuation();
      commands.findOrAppend(std::move(decoded), offset);
      if (!next) {
        break;
      }
      offset = next->value;
    }
  }
}

}  // namespace vgmtrans::core
