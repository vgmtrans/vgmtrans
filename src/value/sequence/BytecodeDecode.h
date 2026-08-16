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
#include <vector>

namespace vgmtrans::core {

// Presentation is transient decode output used by one shared projector to build
// source annotations without teaching format execution about SourceMapBuilder.
struct DecodedCommandPresentation {
  std::string label;
  std::string localKind;
  std::string detailKind;
  SequenceSemantic semantic = SequenceSemantic::Unknown;
  CommandPlaybackStatus playback = CommandPlaybackStatus::AffectsPlayback;
};

// Temporary decoded form used while a bytecode decoder is deciding control flow.
// TrackProgram retains the final source-command snapshot.
struct DecodedBytecodeCommand {
  SourceRange range;
  u8 opcode = 0;
  CommandFlow flow;
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
      decoded.flow.forEachDiscoveryTarget([&](Address target) {
        if (target.value < bytecodeEnd && !commands.hasCommand(static_cast<u32>(target.value))) {
          pendingBlocks.push_back(target.value);
        }
      });
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
