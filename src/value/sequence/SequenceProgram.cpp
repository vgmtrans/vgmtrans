/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/sequence/SequenceProgram.h"

#include "value/base/Source.h"

#include <algorithm>
#include <utility>

namespace vgmtrans::core {

std::optional<u32> TrackProgram::commandIndex(Address address) const {
  const auto found = std::ranges::lower_bound(commands, address.value, {},
                                              [](const SourceCommand& command) { return command.address.value; });
  if (found == commands.end() || found->address.value != address.value) {
    return std::nullopt;
  }
  return static_cast<u32>(std::distance(commands.begin(), found));
}

const SourceCommand* TrackProgram::command(CommandId id) const {
  if (!id.valid() || id.value >= commands.size()) {
    return nullptr;
  }
  return &commands[id.value];
}

const SourceCommand* SequenceProgram::command(SourceCommandRef source) const {
  if (!source.valid() || source.track.value >= tracks.size()) {
    return nullptr;
  }
  return tracks[source.track.value].command(source.id);
}

size_t SequenceProgram::playbackTrackCount() const {
  size_t count = 0;
  for (const auto& track : tracks) {
    for (const auto& stream : track.streams) {
      count += stream.channels.size();
    }
  }
  return count;
}

size_t SequenceProgram::streamCount() const {
  size_t count = 0;
  for (const auto& track : tracks) {
    count += track.streams.size();
  }
  return count;
}

bool trackUsesSemantic(const TrackProgram& track, SequenceSemantic semantic) {
  return std::ranges::any_of(track.commands,
                             [semantic](const SourceCommand& command) { return command.semantic == semantic; });
}

bool sequenceUsesSemantic(const SequenceProgram& program, SequenceSemantic semantic) {
  return std::ranges::any_of(program.tracks,
                             [semantic](const TrackProgram& track) { return trackUsesSemantic(track, semantic); });
}

SourceRange sequenceSourceRange(ByteReader reader, SourceRange baseRange, const SequenceProgram& program) {
  const SourceId source = baseRange.valid() ? baseRange.source : reader.source();
  for (const TrackProgram& track : program.tracks) {
    for (const SourceCommand& command : track.commands) {
      if (command.range.valid() && command.range.source == source) {
        baseRange.include(command.range);
      }
    }
  }
  return reader.range(baseRange.offset, baseRange.size);
}

}  // namespace vgmtrans::core
