/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/ChunSnes/ChunSnes.h"

#include <algorithm>
#include <string>

namespace vgmtrans::formats::chun_snes {

using namespace core;

namespace {

[[nodiscard]] SourceRange sequenceRange(ByteReader reader, SourceRange header, const SequenceProgram& program) {
  u64 first = header.offset;
  u64 last = header.endOffset();
  for (const TrackProgram& track : program.tracks) {
    for (const SourceCommand& command : track.commands) {
      if (command.range.valid() && command.range.source == header.source) {
        first = std::min(first, command.range.offset);
        last = std::max(last, command.range.endOffset());
      }
    }
  }
  return reader.range(first, last - first);
}

[[nodiscard]] ScanResult scan(const ScanInput& input) {
  const auto layout = findLayout(input.reader);
  if (!layout) {
    return {};
  }

  ScanResultBuilder result(input, "ChunSnes");
  const std::string displayName = result.sourceDisplayName();
  auto sequence = result.sequence(displayName);
  SequenceParse parsed =
      decodeSequence(input.reader, *layout, sequence.id(), &result.sourceMap(), &result.diagnostics());
  sequence.range(sequenceRange(input.reader, parsed.headerRange, parsed.program)).program(std::move(parsed.program));

  auto collection = result.sourceCollection(displayName).sequence(sequence);
  if (const auto synth = addSynth(result, *layout, displayName)) {
    collection.instrumentSet(synth->instruments).samples(synth->samples);
  } else {
    result.warning("ChunSnes sequence found, but its active instrument set did not contain usable samples",
                   input.reader.range(0, input.reader.size()));
  }
  return result.finish();
}

}  // namespace

FormatDefinition definition() {
  return FormatDefinition{
      .module = {.name = "ChunSnes", .preferredSampleFilter = SampleFilter::SnesDspLowPass, .scan = scan},
      .sequenceDialects = {sequenceDialect()},
  };
}

}  // namespace vgmtrans::formats::chun_snes
