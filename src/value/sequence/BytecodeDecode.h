/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/Source.h"
#include "value/sequence/SequenceProgram.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace vgmtrans::core {

class SourceMapBuilder;

// Presentation is transient decode output. The durable command keeps semantic
// operands and an annotation ID; this metadata lets one shared projector build
// the annotation without teaching format execution about SourceMapBuilder.
struct DecodedCommandPresentation {
  std::string label;
  std::string localKind;
  std::string detailKind;
  SequenceSemantic semantic = SequenceSemantic::Unknown;
  CommandPlaybackStatus playback = CommandPlaybackStatus::AffectsPlayback;
};

// Temporary decoded form used while a bytecode decoder is deciding control flow.
// TrackProgramBuilder still owns the final immutable source-command snapshot.
struct DecodedBytecodeCommand {
  SourceRange range;
  SourceAnnotationId annotation;
  u8 opcode = 0;
  u32 encodedSize = 0;
  std::vector<u8> bytes;
  DecodeFlow flow;
  SemanticCommandKind kind;
  std::vector<SemanticOperand> operands;
  DecodedCommandPresentation presentation;
  bool retainBytes = true;
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
  if (!decoded.retainBytes && decoded.kind.valid()) {
    builder.addSemantic(Address{offset}, decoded.opcode, decoded.encodedSize, decoded.range, decoded.kind,
                        decoded.operands, decoded.flow, decoded.annotation);
    return;
  }
  builder.addDecoded(Address{offset}, decoded.range, decoded.bytes, decoded.annotation, decoded.flow);
}

// Shared limits for walking source bytecode. Formats still decide what each opcode means.
struct LinearBytecodeDecodePolicy {
  u32 maxCommands = 4096;
  bool stopAtVisitedOffset = true;
  bool followUnconditionalJumps = true;
};

// For drivers whose track bytes mostly play in order. This can optionally follow
// a one-way jump instead of stopping at it.
template <class DecodeCommand>
[[nodiscard]] TrackProgram decodeLinearBytecodeTrack(ByteReader reader, u32 sourceTrackNumber, u32 startAddress,
                                                     LinearBytecodeDecodePolicy policy, DecodeCommand decodeCommand) {
  TrackProgram track{
      .id = TrackId{sourceTrackNumber},
      .sourceTrackNumber = sourceTrackNumber,
      .startAddress = Address{startAddress},
  };
  TrackProgramBuilder builder{track};
  std::set<u32> visitedOffsets;
  std::vector<u32> pendingOffsets;
  u32 offset = startAddress;

  const auto nextPendingOffset = [&]() -> std::optional<u32> {
    while (!pendingOffsets.empty()) {
      const u32 pending = pendingOffsets.back();
      pendingOffsets.pop_back();
      if (reader.has(pending, 1) && (!policy.stopAtVisitedOffset || !visitedOffsets.contains(pending))) {
        return pending;
      }
    }
    return std::nullopt;
  };

  const auto queueSideTarget = [&](Address target) {
    if (!reader.has(target.value, 1) || (policy.stopAtVisitedOffset && visitedOffsets.contains(target.value))) {
      return;
    }
    if (std::find(pendingOffsets.begin(), pendingOffsets.end(), target.value) == pendingOffsets.end()) {
      pendingOffsets.push_back(target.value);
    }
  };

  while (track.commands.size() < policy.maxCommands) {
    if (!reader.has(offset, 1)) {
      const auto pending = nextPendingOffset();
      if (!pending) {
        break;
      }
      offset = *pending;
      continue;
    }

    if (policy.stopAtVisitedOffset && !visitedOffsets.insert(offset).second) {
      const auto pending = nextPendingOffset();
      if (!pending) {
        break;
      }
      offset = *pending;
      continue;
    }

    const u32 begin = offset;
    auto decoded = decodeCommand(begin);
    const auto next = decoded.flow.fallthrough;
    const bool terminal = decoded.flow.terminal;
    const auto targets = decoded.flow.staticTargets;
    const std::optional<Address> followedJump = !next && policy.followUnconditionalJumps && targets.size() == 1
                                                    ? std::optional<Address>{targets.front()}
                                                    : std::nullopt;
    appendDecodedBytecodeCommand(builder, decoded, begin);

    for (const Address target : targets) {
      if ((next && target.value == next->value) || (followedJump && target.value == followedJump->value)) {
        continue;
      }
      queueSideTarget(target);
    }

    if (terminal) {
      const auto pending = nextPendingOffset();
      if (!pending) {
        break;
      }
      offset = *pending;
      continue;
    }
    if (next) {
      offset = next->value;
      continue;
    }
    if (followedJump && reader.has(followedJump->value, 1) &&
        (!policy.stopAtVisitedOffset || !visitedOffsets.contains(followedJump->value))) {
      offset = followedJump->value;
      continue;
    }

    const auto pending = nextPendingOffset();
    if (pending) {
      offset = *pending;
      continue;
    }
    break;
  }

  return track;
}

struct ReachableBytecodeDecodePolicy {
  u32 maxCommands = 262144;
};

// Follow the track start, jumps, and calls to find every command that can be reached.
// Commands are appended in address order so the parsed result is stable.
template <class DecodeCommand>
[[nodiscard]] TrackProgram decodeReachableBytecodeBlocks(ByteReader reader, u32 bytecodeEnd, u32 startOffset,
                                                         u32 trackIndex, ReachableBytecodeDecodePolicy policy,
                                                         DecodeCommand decodeCommand) {
  TrackProgram track{
      .id = TrackId{trackIndex},
      .sourceTrackNumber = trackIndex,
      .startAddress = Address{startOffset},
  };
  TrackProgramBuilder builder{track};
  std::map<u32, DecodedBytecodeCommand> commandsByOffset;
  std::vector<u32> pendingBlocks{startOffset};
  size_t decodedCommands = 0;

  while (!pendingBlocks.empty() && decodedCommands < policy.maxCommands) {
    u32 offset = pendingBlocks.back();
    pendingBlocks.pop_back();
    while (hasBytecodeBytes(reader, offset, 1, bytecodeEnd) && !commandsByOffset.contains(offset) &&
           decodedCommands < policy.maxCommands) {
      auto decoded = decodeCommand(offset);
      ++decodedCommands;
      // Jump and call targets start new blocks. The next sequential command stays
      // in this inner loop.
      for (const Address target : decoded.flow.staticTargets) {
        if (target.value < bytecodeEnd && !commandsByOffset.contains(target.value)) {
          pendingBlocks.push_back(target.value);
        }
      }
      const auto next = decoded.flow.fallthrough;
      commandsByOffset.emplace(offset, std::move(decoded));
      if (!next) {
        break;
      }
      offset = next->value;
    }
  }

  for (const auto& [offset, decoded] : commandsByOffset) {
    appendDecodedBytecodeCommand(builder, decoded, offset);
  }
  return track;
}

}  // namespace vgmtrans::core
