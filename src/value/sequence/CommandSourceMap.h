/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/sequence/BytecodeDecode.h"

#include <limits>
#include <optional>
#include <utility>

namespace vgmtrans::core {

// Project one already-decoded semantic command into the durable SourceMap.
// Format decoders provide data; this function owns the annotation mechanics.
[[nodiscard]] SourceAnnotationId projectDecodedCommand(SourceMapBuilder* sourceMap,
                                                       const DecodedBytecodeCommand& command,
                                                       std::optional<SourceAnnotationId> parent = std::nullopt);

// One track's annotation and command-projection lifecycle. Most formats use
// TrackDecodeScope::linear/reachable; exceptional walkers can begin a session
// and append the commands they discover themselves.
class TrackDecodeSession {
public:
  [[nodiscard]] std::optional<SourceAnnotationId> annotation() const noexcept { return annotation_; }

  void append(DecodedBytecodeCommand command, u32 offset);
  [[nodiscard]] TrackProgram finish();
  [[nodiscard]] TrackProgram finish(TrackProgram track);

private:
  friend struct TrackDecodeScope;

  TrackDecodeSession(ByteReader reader, u32 trackIndex, u32 startOffset, std::optional<AssetId> sequenceAsset,
                     std::optional<SourceAnnotationId> parentAnnotation, SourceMapBuilder* sourceMap);

  [[nodiscard]] DecodedBytecodeCommand project(DecodedBytecodeCommand command) const;

  ByteReader reader_;
  u32 startOffset_ = 0;
  SourceMapBuilder* sourceMap_ = nullptr;
  std::optional<SourceAnnotationId> annotation_;
  TrackProgram track_;
};

// Sequence-wide services and limits for source-track discovery. Relative
// address bases, semantic sequence bounds, and diagnostics remain in the
// format decoder because they affect command meaning rather than track walking.
struct TrackDecodeScope {
  ByteReader reader;
  u32 bytecodeEnd = std::numeric_limits<u32>::max();
  u32 maxCommands = 4096;
  std::optional<AssetId> sequenceAsset;
  std::optional<SourceAnnotationId> parentAnnotation;
  SourceMapBuilder* sourceMap = nullptr;

  [[nodiscard]] TrackDecodeSession begin(u32 trackIndex, u32 startOffset) const {
    return TrackDecodeSession(reader, trackIndex, startOffset, sequenceAsset, parentAnnotation, sourceMap);
  }

  template <class DecodeCommand>
  [[nodiscard]] TrackProgram linear(u32 trackIndex, u32 startOffset, DecodeCommand decodeCommand) const {
    auto session = begin(trackIndex, startOffset);
    const auto decodeAndProject = [&](u32 offset) { return session.project(decodeCommand(offset)); };
    auto track = decodeLinearBytecodeTrack(reader, trackIndex, startOffset,
                                           LinearBytecodeDecodePolicy{.maxCommands = maxCommands}, decodeAndProject);
    return session.finish(std::move(track));
  }

  template <class DecodeCommand>
  [[nodiscard]] TrackProgram reachable(u32 trackIndex, u32 startOffset, DecodeCommand decodeCommand) const {
    auto session = begin(trackIndex, startOffset);
    const auto decodeAndProject = [&](u32 offset) { return session.project(decodeCommand(offset)); };
    const u32 end = bytecodeEnd == std::numeric_limits<u32>::max()
                        ? static_cast<u32>(reader.size())
                        : std::min(static_cast<u32>(reader.size()), bytecodeEnd);
    auto track =
        decodeReachableBytecodeBlocks(reader, end, startOffset, trackIndex,
                                      ReachableBytecodeDecodePolicy{.maxCommands = maxCommands}, decodeAndProject);
    return session.finish(std::move(track));
  }
};

// Temporary bridge for formats still exposing the old per-track parameter bag.
[[nodiscard]] TrackDecodeScope makeTrackDecodeScope(ByteReader reader, const TrackDecodeInput& input);

}  // namespace vgmtrans::core
