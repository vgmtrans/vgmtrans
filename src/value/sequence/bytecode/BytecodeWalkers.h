/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/sequence/bytecode/BytecodeSequenceDecoder.h"

#include <cstddef>
#include <map>
#include <set>
#include <vector>

namespace vgmtrans::core {

// Common walkers own traversal mechanics and limits. Formats still own opcode
// decoding and control-flow classification for their source driver.
struct LinearBytecodeDecodePolicy {
  u32 maxCommands = 4096;
  bool stopAtVisitedOffset = true;
  bool followUnconditionalJumps = true;
};

// Linear decoding matches drivers whose tracks are physically laid out in the
// order they play, with optional following of one-way jumps.
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
  u32 offset = startAddress;

  while (reader.has(offset, 1) && track.commands.size() < policy.maxCommands) {
    if (policy.stopAtVisitedOffset && !visitedOffsets.insert(offset).second) {
      break;
    }

    const u32 begin = offset;
    auto decoded = decodeCommand(begin);
    const auto next = decoded.flow.fallthrough;
    const bool terminal = decoded.flow.terminal;
    const auto targets = decoded.flow.staticTargets;
    appendDecodedBytecodeCommand(builder, decoded, begin);

    if (terminal) {
      break;
    }
    if (next) {
      offset = next->value;
      continue;
    }
    if (policy.followUnconditionalJumps && targets.size() == 1) {
      offset = targets.front().value;
      continue;
    }
    break;
  }

  return track;
}

struct ReachableBytecodeDecodePolicy {
  u32 maxCommands = 262144;
};

// Reachable-block decoding discovers all statically reachable source blocks
// before appending commands in address order. That keeps snapshots stable even
// when calls and jumps lead to out-of-order bytecode.
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
      // Static targets become new pending blocks. Fallthrough continues the
      // current block so ordinary linear command runs stay cheap.
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
