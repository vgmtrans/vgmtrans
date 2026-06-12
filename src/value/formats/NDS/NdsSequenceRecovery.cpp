/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/NDS/NdsSequenceRecovery.h"

#include <algorithm>
#include <set>
#include <vector>

namespace vgmtrans::formats::nds {

using namespace core;

namespace {

struct PendingBlock {
  u32 offset = 0;
  bool callTarget = false;
};

[[nodiscard]] TrackProgram makeTrack(u32 startOffset, u32 trackIndex) {
  return TrackProgram{
      .id = TrackId{trackIndex},
      .sourceTrackNumber = trackIndex,
      .startAddress = Address{startOffset},
  };
}

[[nodiscard]] DecodedBytecodeCommand terminalRecoveryCommand(const BytecodeCommandSpec& terminalSpec, ByteReader reader,
                                                             u32 offset) {
  auto command = recordSizedPreservedBytecodeCommand(terminalSpec, reader, offset, offset + 1);
  command.flow = DecodeFlow::terminalFlow();
  return command;
}

}  // namespace

// Recovery decoder for malformed SDAT ranges that do not contain a normal SSEQ
// header. The normal dialect decoder stays source-driver oriented; this file
// handles the range-level repair needed before those commands can be trusted.
TrackProgram decodeMalformedSdatRangeTrack(ByteReader reader, const BytecodeDispatchTable& dispatch,
                                           const BytecodeCommandSpec& noOpSpec, const BytecodeCommandSpec& terminalSpec,
                                           u32 sequenceOffset, u32 sequenceEnd, u32 startOffset, u32 trackIndex,
                                           size_t maxCommands) {
  TrackProgram track = makeTrack(startOffset, trackIndex);
  TrackProgramBuilder builder{track};
  u32 offset = startOffset;
  size_t decodedCommands = 0;
  std::set<u32> visitedControlDestinations;
  std::set<u32> decodedOffsets;
  std::set<u32> callTargetOffsets;
  std::vector<PendingBlock> pendingBlocks{{.offset = startOffset}};

  while (!pendingBlocks.empty()) {
    const PendingBlock block = pendingBlocks.back();
    pendingBlocks.pop_back();
    offset = block.offset;

    while (hasBytecodeBytes(reader, offset, 1, sequenceEnd) && decodedCommands++ < maxCommands) {
      const u32 begin = offset;
      if (decodedOffsets.contains(begin)) {
        break;
      }
      decodedOffsets.insert(begin);

      auto decoded = dispatch.decode(reader, offset,
                                     BytecodeDecodeContext{
                                         .bytecodeEnd = sequenceEnd,
                                         .sequenceOffset = sequenceOffset,
                                         .sequenceEnd = sequenceEnd,
                                     });

      if (!block.callTarget) {
        const auto overlap = std::ranges::find_if(
            callTargetOffsets, [&](u32 target) { return begin < target && target < decoded.range.endOffset(); });
        if (overlap != callTargetOffsets.end()) {
          // Some malformed FAT entries fall through one byte before a real call
          // target. Stop before consuming the overlapping subroutine bytes.
          appendDecodedBytecodeCommand(builder, terminalRecoveryCommand(terminalSpec, reader, begin), begin);
          break;
        }
      }

      if (decoded.flow.unconditionalJump()) {
        const u32 destination = decoded.flow.staticTargets.front().value;
        if (visitedControlDestinations.contains(destination)) {
          appendDecodedBytecodeCommand(builder, terminalRecoveryCommand(terminalSpec, reader, begin), begin);
          break;
        }
        visitedControlDestinations.insert(destination);
        auto noOp =
            recordSizedPreservedBytecodeCommand(noOpSpec, reader, begin, static_cast<u32>(decoded.range.endOffset()));
        appendDecodedBytecodeCommand(builder, noOp, begin);
        offset = destination;
        continue;
      }

      if (decoded.flow.callTarget()) {
        const u32 destination = decoded.flow.staticTargets.front().value;
        if (!decodedOffsets.contains(destination) && callTargetOffsets.insert(destination).second) {
          pendingBlocks.push_back(PendingBlock{.offset = destination, .callTarget = true});
        }
      }

      const auto next = decoded.flow.fallthrough;
      appendDecodedBytecodeCommand(builder, decoded, begin);
      if (!next || decoded.flow.terminal) {
        break;
      }
      offset = next->value;
    }
  }

  return track;
}

}  // namespace vgmtrans::formats::nds
