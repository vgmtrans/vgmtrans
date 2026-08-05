/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/RareSnes/RareSnes.h"

#include <fmt/format.h>

#include <algorithm>
#include <string>

namespace vgmtrans::formats::rare_snes {

using namespace core;

namespace {

[[nodiscard]] SourceRange sequenceRange(ByteReader reader, const Layout& layout, const SequenceProgram& program) {
  const auto include = [&](SourceRange& result, SourceRange range) {
    if (!range.valid() || range.source != result.source) {
      return;
    }
    const u64 begin = std::min(result.offset, range.offset);
    const u64 end = std::max(result.endOffset(), range.endOffset());
    result = reader.range(static_cast<u32>(begin), static_cast<u32>(end - begin));
  };
  SourceRange result = layout.sequenceHeaderRange;
  include(result, layout.initialTempoRange);
  for (const TrackProgram& track : program.tracks) {
    for (const SourceCommand& command : track.commands) {
      include(result, command.range);
    }
  }
  return result;
}

[[nodiscard]] ScanResult scan(const ScanInput& input) {
  const auto layout = findLayout(input.reader);
  if (!layout) {
    return {};
  }

  ScanResultBuilder result(input, "RareSnes");
  const std::string sourceName = result.sourceDisplayName();
  const std::string displayName = fmt::format("{} ({})", sourceName, profileName(layout->profile));
  auto sequence = result.sequence(displayName);
  SequenceParse parsed =
      decodeSequence(input.reader, *layout, sequence.id(), &result.sourceMap(), &result.diagnostics());
  sequence.range(sequenceRange(input.reader, *layout, parsed.program)).program(std::move(parsed.program));

  auto collection = result.sourceCollection(displayName).sequence(sequence);
  if (const auto synth = addSynth(result, *layout, parsed.recipes, displayName)) {
    collection.instrumentSet(synth->instruments).samples(synth->samples);
  } else {
    result.warning("RareSnes sequence found, but no valid used instruments or samples were discovered",
                   input.reader.range(0, input.reader.size()));
  }
  return result.finish();
}

}  // namespace

FormatDefinition definition() {
  return FormatDefinition{
      .module = {.name = "RareSnes",
                 .preferredSampleFilter = SampleFilter::SnesDspLowPass,
                 .scan = scan},
      .sequenceDialects = {sequenceDialect()},
  };
}

}  // namespace vgmtrans::formats::rare_snes
