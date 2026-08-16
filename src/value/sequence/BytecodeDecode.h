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
#include <string_view>
#include <utility>
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
// TrackProgram retains the final source-command snapshot.
struct DecodedBytecodeCommand {
  SourceRange range;
  SourceAnnotationId annotation;
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

inline void appendDecodedBytecodeCommand(TrackProgram& track, DecodedBytecodeCommand decoded, u32 offset) {
  const SequenceSemantic semantic = decoded.presentation.semantic;
  track.addCommand(Address{offset}, decoded.opcode, decoded.range, std::move(decoded.operands),
                   std::move(decoded.flow), decoded.annotation, std::move(decoded.execution), semantic);
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
    const auto next = decoded.flow.discoveryContinuation();
    const std::optional<Address> followedJump = decoded.flow.unconditionalJump() && policy.followUnconditionalJumps
                                                    ? decoded.flow.defaultDestination()
                                                    : std::nullopt;
    decoded.flow.forEachDiscoveryTarget([&](Address target) {
      if ((next && target.value == next->value) || (followedJump && target.value == followedJump->value)) {
        return;
      }
      queueSideTarget(target);
    });
    appendDecodedBytecodeCommand(track, std::move(decoded), begin);

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
      decoded.flow.forEachDiscoveryTarget([&](Address target) {
        if (target.value < bytecodeEnd && !commandsByOffset.contains(target.value)) {
          pendingBlocks.push_back(target.value);
        }
      });
      const auto next = decoded.flow.discoveryContinuation();
      commandsByOffset.emplace(offset, std::move(decoded));
      if (!next) {
        break;
      }
      offset = next->value;
    }
  }

  for (auto& [offset, decoded] : commandsByOffset) {
    appendDecodedBytecodeCommand(track, std::move(decoded), offset);
  }
  return track;
}

}  // namespace vgmtrans::core
