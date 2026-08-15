/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/sequence/BytecodeDecode.h"

#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace vgmtrans::core {

struct SequenceDialect;

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
                     std::optional<SourceAnnotationId> parentAnnotation, SourceMapBuilder* sourceMap,
                     bool sourceHasTracks);

  [[nodiscard]] DecodedBytecodeCommand project(DecodedBytecodeCommand command) const;

  ByteReader reader_;
  u32 startOffset_ = 0;
  SourceMapBuilder* sourceMap_ = nullptr;
  std::optional<SourceAnnotationId> annotation_;
  std::optional<SourceAnnotationId> commandParent_;
  std::optional<AssetId> rootSequenceAsset_;
  TrackProgram track_;
};

// Holds the reader, source-map context, and safety limits used to decode a
// sequence into TrackPrograms. Each format still resolves its relative
// addresses, validates its format-specific bounds, and reports malformed commands.
struct TrackDecodeScope {
  ByteReader reader;
  u32 bytecodeEnd = std::numeric_limits<u32>::max();
  u32 maxCommands = 4096;
  // Controls source annotation hierarchy only. TrackPrograms are still
  // produced for playback when the encoded sequence has no source tracks.
  bool sourceHasTracks = true;
  std::optional<AssetId> sequenceAsset;
  std::optional<SourceAnnotationId> parentAnnotation;
  SourceMapBuilder* sourceMap = nullptr;

  [[nodiscard]] TrackDecodeSession begin(u32 trackIndex, u32 startOffset) const {
    return TrackDecodeSession(reader, trackIndex, startOffset, sequenceAsset, parentAnnotation, sourceMap,
                              sourceHasTracks);
  }

  // Decode a mostly sequential track. Fallthrough is visited immediately;
  // branch and call targets are queued and decoded afterward. Commands remain
  // in discovery order, and the reader itself supplies the bytecode boundary.
  template <class DecodeCommand>
  [[nodiscard]] TrackProgram linear(u32 trackIndex, u32 startOffset, DecodeCommand decodeCommand) const {
    auto session = begin(trackIndex, startOffset);
    const auto decodeAndProject = [&](u32 offset) { return session.project(decodeCommand(offset)); };
    auto track = decodeLinearBytecodeTrack(reader, trackIndex, startOffset,
                                           LinearBytecodeDecodePolicy{.maxCommands = maxCommands}, decodeAndProject);
    return session.finish(std::move(track));
  }

  // Decode every block reachable from the track start within bytecodeEnd.
  // Fallthrough, jumps, and calls all contribute reachable blocks; commands
  // are stored in address order so the resulting program is stable.
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

// Owns the generic sequence-header/track-pointer and track -> command
// annotation lifecycle. Headers and tracks are siblings; pointer fields remain
// under the header that encodes them.
class SequenceDecodeSession {
public:
  // maxTrackCommands is a safety cap for damaged control flow. Formats with
  // unusually large valid tracks can raise it without replacing shared assembly.
  SequenceDecodeSession(ByteReader reader, const SequenceDialect& dialect, AssetId sequenceAsset,
                        SourceRange headerRange, SourceMapBuilder* sourceMap, u32 maxTrackCommands = 4096,
                        u32 bytecodeEnd = std::numeric_limits<u32>::max());

  template <class DecodeCommand>
  void addLinearTrack(u32 trackIndex, SourceRange pointerRange, u32 startOffset, DecodeCommand decodeCommand,
                      std::optional<u64> encodedStartOffset = std::nullopt) {
    annotateTrackPointer(trackIndex, pointerRange, startOffset, encodedStartOffset);
    program_.tracks.push_back(tracks_.linear(trackIndex, startOffset, std::move(decodeCommand)));
  }

  template <class DecodeCommand>
  void addReachableTrack(u32 trackIndex, SourceRange pointerRange, u32 startOffset, DecodeCommand decodeCommand,
                         std::optional<u64> encodedStartOffset = std::nullopt) {
    annotateTrackPointer(trackIndex, pointerRange, startOffset, encodedStartOffset);
    program_.tracks.push_back(tracks_.reachable(trackIndex, startOffset, std::move(decodeCommand)));
  }

  [[nodiscard]] std::optional<SourceAnnotationId> headerAnnotation() const noexcept { return headerAnnotation_; }

  [[nodiscard]] SequenceProgram finish(SequenceRuntime runtime) {
    program_.runtime = std::move(runtime);
    return std::move(program_);
  }

private:
  void annotateTrackPointer(u32 trackIndex, SourceRange pointerRange, u32 startOffset,
                            std::optional<u64> encodedStartOffset);

  TrackDecodeScope tracks_;
  std::optional<SourceAnnotationId> headerAnnotation_;
  SequenceProgram program_;
  std::string sourceKindPrefix_;
};

}  // namespace vgmtrans::core
