/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/validation/SequenceValidation.h"

#include "value/sequence/SequenceProgram.h"

#include <algorithm>
#include <string>
#include <unordered_set>

namespace vgmtrans::core {

ValidationReport validateSequenceProgram(const SequenceProgram& program) {
  ValidationReport report;

  for (const auto& track : program.tracks) {
    for (const auto& stream : track.streams) {
      std::unordered_set<u32> channels;
      if (stream.channels.empty()) {
        report.error("sequence.stream.no-channels", "Sequence stream had no output channels");
      }
      for (const u32 channel : stream.channels) {
        if (!channels.insert(channel).second) {
          report.error("sequence.stream.duplicate-channel", "Sequence stream contained a duplicate channel");
        }
      }
    }
    for (size_t i = 1; i < track.commands.size(); ++i) {
      const SourceCommand& previous = track.commands[i - 1];
      const SourceCommand& command = track.commands[i];
      if (previous.address.value >= command.address.value) {
        report.error("sequence.track.command-order",
                     "Sequence track contained duplicate or out-of-order command address " +
                         std::to_string(command.address.value),
                     command.range);
      }
    }
    if (!track.commands.empty() &&
        std::ranges::none_of(track.commands, [&](const SourceCommand& command) {
          return command.address.value == track.startAddress.value;
        })) {
      report.error("sequence.track.missing-start",
                   "Sequence track start address did not reference a decoded command");
    }
  }

  if (program.sectionPlaylist) {
    const SectionPlaylist& playlist = *program.sectionPlaylist;
    std::unordered_set<u64> playlistAddresses;
    for (const auto& command : playlist.commands) {
      if (!playlistAddresses.insert(command.address.value).second) {
        report.error("sequence.playlist.duplicate-command",
                     "Sequence playlist contained duplicate command address " + std::to_string(command.address.value),
                     command.range);
      }
    }
    if (!playlistAddresses.contains(playlist.startAddress.value)) {
      report.error("sequence.playlist.missing-start",
                   "Sequence playlist start address did not reference a playlist command");
    }

    for (const auto& command : playlist.commands) {
      if (command.kind == PlaylistCommandKind::PlaySection) {
        if (command.streamStarts.empty()) {
          report.error("sequence.playlist.missing-section",
                       "Sequence playlist referenced a section that was not decoded", command.range);
        } else if (command.streamStarts.size() != program.streamCount()) {
          report.error("sequence.playlist.track-count",
                       "Sequence play command entries did not match the execution stream count", command.range);
        } else {
          size_t streamIndex = 0;
          for (const auto& track : program.tracks) {
            for (size_t i = 0; i < track.streams.size(); ++i) {
              const auto start = command.streamStarts[streamIndex++];
              if (start && !track.commandIndex(*start)) {
                report.error("sequence.playlist.missing-track-start",
                             "Sequence play command referenced a track start that was not decoded", command.range);
              }
            }
          }
        }
        if (!playlistAddresses.contains(command.fallthrough.value)) {
          report.error("sequence.playlist.missing-fallthrough",
                       "Sequence playlist play command had no decoded fallthrough", command.range);
        }
      } else if (command.kind == PlaylistCommandKind::Repeat) {
        if (!playlistAddresses.contains(command.target.value)) {
          report.error("sequence.playlist.missing-repeat-target",
                       "Sequence playlist repeat target was not decoded", command.range);
        }
        if (command.additionalPlays != 0 && !playlistAddresses.contains(command.fallthrough.value)) {
          report.error("sequence.playlist.missing-fallthrough",
                       "Sequence playlist repeat command had no decoded fallthrough", command.range);
        }
      }
    }
  }

  return report;
}

}  // namespace vgmtrans::core
