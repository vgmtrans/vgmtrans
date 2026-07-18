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
    // Legacy commands refer into a track-level byte pool. Semantic commands
    // must be byte-free and have unique, source-bounded operand names. Names
    // deliberately serve as identity so format code does not maintain a second
    // numeric operand vocabulary solely for execution.
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

      if (command.semantic() && command.range.valid() && command.encodedSize != command.range.size) {
        report.error("sequence.command.semantic-size",
                     "Semantic sequence command encoded size did not match its source range", command.range);
      }

      std::unordered_set<std::string> operandNames;
      operandNames.reserve(command.operands.size());
      for (const auto& operand : command.operands) {
        if (command.semantic() && (operand.name.empty() || !operandNames.insert(operand.name).second)) {
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

  return report;
}

}  // namespace vgmtrans::core
