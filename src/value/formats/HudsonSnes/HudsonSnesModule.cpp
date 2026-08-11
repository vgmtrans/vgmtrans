/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/HudsonSnes/HudsonSnes.h"

#include <fmt/format.h>

#include <algorithm>
#include <string>

namespace vgmtrans::formats::hudson_snes {

using namespace core;

namespace {

[[nodiscard]] SourceRange sequenceRange(ByteReader reader, SourceRange header, const SequenceProgram& program) {
  u64 first = header.offset;
  u64 last = header.endOffset();
  for (const TrackProgram& track : program.tracks) {
    for (const SourceCommand& command : track.commands) {
      if (!command.range.valid() || command.range.source != header.source) {
        continue;
      }
      first = std::min(first, command.range.offset);
      last = std::max(last, command.range.endOffset());
    }
  }
  return reader.range(first, last - first);
}

[[nodiscard]] ScanResult scan(const ScanInput& input) {
  const auto layout = findLayout(input.reader);
  if (!layout) {
    return {};
  }
  ScanResultBuilder result(input, "HudsonSnes");
  const std::string displayName =
      fmt::format("{} (Hudson {})", result.sourceDisplayName(), versionName(layout->version));
  auto sequence = result.sequence(displayName);
  SequenceParse parsed =
      decodeSequence(input.reader, *layout, sequence.id(), &result.sourceMap(), &result.diagnostics());
  sequence.range(sequenceRange(input.reader, parsed.headerRange, parsed.program)).program(std::move(parsed.program));

  auto collection = result.sourceCollection(displayName).sequence(sequence);
  if (const auto synth = addSynth(result, *layout, parsed.recipes, displayName)) {
    collection.instrumentSet(synth->instruments).samples(synth->samples);
  } else {
    result.warning("HudsonSnes sequence found, but no valid instruments or samples were discovered",
                   input.reader.range(0, input.reader.size()));
  }
  return result.finish();
}

}  // namespace

FormatDefinition definition() {
  return FormatDefinition{
      .module = {.name = "HudsonSnes", .preferredSampleFilter = SampleFilter::SnesDspLowPass, .scan = scan},
      .sequenceDialects = {sequenceDialect()},
  };
}

}  // namespace vgmtrans::formats::hudson_snes
