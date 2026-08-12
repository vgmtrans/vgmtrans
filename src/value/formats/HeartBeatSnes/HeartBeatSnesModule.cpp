/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/HeartBeatSnes/HeartBeatSnes.h"

#include <fmt/format.h>

#include <string>
#include <utility>

namespace vgmtrans::formats::heartbeat_snes {

using namespace core;

namespace {

[[nodiscard]] ScanResult scan(const ScanInput& input) {
  const auto layout = findLayout(input.reader);
  if (!layout) {
    return {};
  }

  ScanResultBuilder result(input, "HeartBeatSnes");
  const std::string displayName = fmt::format("{} ({})", result.sourceDisplayName(), versionName(layout->version));
  auto sequence = result.sequence(displayName);
  SequenceParse parsed =
      decodeSequence(input.reader, *layout, sequence.id(), &result.sourceMap(), &result.diagnostics());
  sequence.range(sequenceSourceRange(input.reader, parsed.headerRange, parsed.program))
      .program(std::move(parsed.program));

  auto collection = result.sourceCollection(displayName).sequence(sequence);
  if (const auto synth = addSynth(result, *layout, parsed.programs, displayName)) {
    collection.instrumentSet(synth->instruments).samples(synth->samples);
  } else {
    result.warning("HeartBeatSnes sequence found, but no valid instruments or samples were discovered",
                   input.reader.range(0, input.reader.size()));
  }
  return result.finish();
}

}  // namespace

FormatDefinition definition() {
  return FormatDefinition{
      .module = {.name = "HeartBeatSnes", .preferredSampleFilter = SampleFilter::SnesDspLowPass, .scan = scan},
      .sequenceDialects = {sequenceDialect()},
  };
}

}  // namespace vgmtrans::formats::heartbeat_snes
