/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/validation/SequenceValidation.h"

#include "value/sequence/SequenceProgram.h"

#include <string>
#include <type_traits>
#include <unordered_set>

namespace vgmtrans::core {

ValidationReport validateSequenceProgram(const SequenceProgram& program) {
  ValidationReport report;

  // Track IDs are used by performance events and lookup helpers, so they need a
  // stable one-to-one relationship with the tracks in this program.
  std::unordered_set<u32> trackIds;
  trackIds.reserve(program.tracks.size());

  for (const auto& track : program.tracks) {
    if (!track.id.valid()) {
      report.error("sequence.track.missing-id", "Sequence program contained a track without an id");
    } else if (!trackIds.insert(track.id.value).second) {
      report.error("sequence.track.duplicate-id",
                   "Sequence program contained duplicate track id " + std::to_string(track.id.value));
    }

    // Operand names are unique and source-bounded. Names deliberately serve as
    // identity so format code does not maintain a second numeric operand
    // vocabulary solely for execution.
    for (const auto& command : track.commands) {
      std::unordered_set<std::string> operandNames;
      operandNames.reserve(command.operands.size());
      for (const auto& operand : command.operands) {
        if (operand.name.empty() || !operandNames.insert(operand.name).second) {
          report.error("sequence.command.operand-name",
                       "Semantic sequence command had a missing or duplicate operand name",
                       command.range.valid() ? std::optional<SourceRange>{command.range} : std::nullopt);
        }
        if (operand.encodedValue && !operand.range.valid()) {
          report.error("sequence.command.operand-encoded-range",
                       "Semantic operand retained an encoded value without a source range",
                       command.range.valid() ? std::optional<SourceRange>{command.range} : std::nullopt);
        }
        if (operand.range.valid() && command.range.valid() &&
            (operand.range.source != command.range.source || operand.range.offset < command.range.offset ||
             operand.range.endOffset() > command.range.endOffset())) {
          report.error("sequence.command.operand-range", "Semantic operand range was outside its command range",
                       operand.range);
        }
      }
    }
  }

  if (program.sectionPlaylist) {
    const SectionPlaylist& playlist = *program.sectionPlaylist;
    std::unordered_set<u64> sectionAddresses;
    for (const auto& section : playlist.sections) {
      if (!sectionAddresses.insert(section.address.value).second) {
        report.error("sequence.playlist.duplicate-section",
                     "Sequence playlist contained duplicate section address " +
                         std::to_string(section.address.value));
      }
      if (section.trackStarts.size() != program.tracks.size()) {
        report.error("sequence.playlist.track-count",
                     "Sequence section track entries did not match the program track count");
        continue;
      }
      for (size_t trackIndex = 0; trackIndex < section.trackStarts.size(); ++trackIndex) {
        if (section.trackStarts[trackIndex] &&
            !program.tracks[trackIndex].addressIndex.find(*section.trackStarts[trackIndex])) {
          report.error("sequence.playlist.missing-track-start",
                       "Sequence section referenced a track start that was not decoded");
        }
      }
    }

    std::unordered_set<u64> playlistAddresses;
    for (const auto& command : playlist.commands) {
      if (!playlistAddresses.insert(command.address.value).second) {
        report.error("sequence.playlist.duplicate-command",
                     "Sequence playlist contained duplicate command address " +
                         std::to_string(command.address.value),
                     command.range.valid() ? std::optional<SourceRange>{command.range} : std::nullopt);
      }
    }
    if (!playlistAddresses.contains(playlist.startAddress.value)) {
      report.error("sequence.playlist.missing-start",
                   "Sequence playlist start address did not reference a playlist command");
    }

    for (const auto& command : playlist.commands) {
      std::visit(
          [&](const auto& operation) {
            using T = std::decay_t<decltype(operation)>;
            if constexpr (std::is_same_v<T, PlaylistPlaySection>) {
              if (!sectionAddresses.contains(operation.section.value)) {
                report.error("sequence.playlist.missing-section",
                             "Sequence playlist referenced a section that was not decoded", command.range);
              }
              if (!playlistAddresses.contains(command.fallthrough.value)) {
                report.error("sequence.playlist.missing-fallthrough",
                             "Sequence playlist play command had no decoded fallthrough", command.range);
              }
            } else if constexpr (std::is_same_v<T, PlaylistRepeat>) {
              if (!playlistAddresses.contains(operation.destination.value)) {
                report.error("sequence.playlist.missing-repeat-target",
                             "Sequence playlist repeat target was not decoded", command.range);
              }
              if (!operation.infinite && !playlistAddresses.contains(command.fallthrough.value)) {
                report.error("sequence.playlist.missing-fallthrough",
                             "Sequence playlist repeat command had no decoded fallthrough", command.range);
              }
            }
          },
          command.operation);
    }
  }

  return report;
}

}  // namespace vgmtrans::core
