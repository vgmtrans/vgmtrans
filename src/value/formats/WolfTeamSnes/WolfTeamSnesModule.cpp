/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/WolfTeamSnes/WolfTeamSnes.h"

#include <fmt/format.h>

#include <algorithm>
#include <string>

namespace vgmtrans::formats::wolf_team_snes {

using namespace core;

namespace {

[[nodiscard]] SourceRange sequenceRange(ByteReader reader, SourceRange header, const SequenceProgram& program) {
  SourceRange result = header;
  for (const TrackProgram& track : program.tracks) {
    for (const SourceCommand& command : track.commands) {
      if (!command.range.valid() || command.range.source != result.source) {
        continue;
      }
      const u64 begin = std::min(result.offset, command.range.offset);
      const u64 end = std::max(result.endOffset(), command.range.endOffset());
      result = reader.range(begin, end - begin);
    }
  }
  return result;
}

[[nodiscard]] ScanResult scan(const ScanInput& input) {
  const auto layout = findLayout(input.reader);
  if (!layout) {
    return {};
  }

  ScanResultBuilder result(input, "WolfTeamSnes");
  const std::string sourceName = result.sourceDisplayName();
  const std::string displayName = fmt::format("{} ({})", sourceName, variantName(layout->variant));
  auto sequence = result.sequence(displayName);
  SequenceParse parsed =
      decodeSequence(input.reader, *layout, sequence.id(), &result.sourceMap(), &result.diagnostics());
  sequence.range(sequenceRange(input.reader, parsed.headerRange, parsed.program)).program(std::move(parsed.program));

  auto collection = result.sourceCollection(displayName).sequence(sequence);
  if (const auto synth = addSynth(result, *layout, displayName)) {
    collection.instrumentSet(synth->instruments).samples(synth->samples);
  } else {
    result.warning("WolfTeamSnes sequence found, but no valid instruments or samples were discovered",
                   input.reader.range(0, input.reader.size()));
  }
  return result.finish();
}

}  // namespace

FormatDefinition definition() {
  return FormatDefinition{
      .module = {.name = "WolfTeamSnes", .preferredSampleFilter = SampleFilter::SnesDspLowPass, .scan = scan},
      .sequenceDialects = {sequenceDialect()},
  };
}

}  // namespace vgmtrans::formats::wolf_team_snes
