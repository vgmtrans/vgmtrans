/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/validation/SequenceValidation.h"

#include "value/sequence/SequenceProgram.h"

#include <string>
#include <unordered_set>

namespace vgmtrans::core {

namespace {

[[nodiscard]] bool byteSpanFits(ByteSpan span, size_t poolSize) noexcept {
  return span.offset <= poolSize && span.size <= poolSize - span.offset;
}

[[nodiscard]] bool operandSpanFits(OperandSpan span, size_t poolSize) noexcept {
  return span.offset <= poolSize && span.size <= poolSize - span.offset;
}

}  // namespace

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

    std::unordered_set<u32> commandIds;
    commandIds.reserve(track.commands.size());
    // Commands refer into track-level byte and operand pools. Bad spans mean
    // later UI/export code could read the wrong command details.
    for (const auto& command : track.commands) {
      if (!command.id.valid()) {
        report.error("sequence.command.missing-id", "Sequence program contained a command without an id",
                     command.range.valid() ? std::optional<SourceRange>{command.range} : std::nullopt);
      } else if (!commandIds.insert(command.id.value).second) {
        report.error("sequence.command.duplicate-id",
                     "Sequence program contained duplicate command id " + std::to_string(command.id.value),
                     command.range.valid() ? std::optional<SourceRange>{command.range} : std::nullopt);
      }

      if (!byteSpanFits(command.bytes, track.commandBytes.size())) {
        report.error("sequence.command.byte-span", "Sequence command byte span was outside its track byte pool",
                     command.range.valid() ? std::optional<SourceRange>{command.range} : std::nullopt);
      }
      if (!operandSpanFits(command.operands, track.operands.size())) {
        report.error("sequence.command.operand-span",
                     "Sequence command operand span was outside its track operand pool",
                     command.range.valid() ? std::optional<SourceRange>{command.range} : std::nullopt);
      }
    }
  }

  return report;
}

}  // namespace vgmtrans::core
