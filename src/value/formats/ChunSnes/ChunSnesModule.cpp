/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/ChunSnes/ChunSnes.h"

#include <string>

namespace vgmtrans::formats::chun_snes {

using namespace core;

namespace {

[[nodiscard]] ScanResult scan(const ScanInput& input) {
  const auto layout = findLayout(input.reader);
  if (!layout) {
    return {};
  }

  ScanResultBuilder result(input, "ChunSnes");
  const std::string displayName = result.sourceDisplayName();
  auto sequence = result.sequence(displayName);
  sequence.program(decodeSequence(input.retain(), *layout, sequence.id(), &result.sourceMap(), &result.diagnostics()));

  sequence.collection();
  if (const auto synth = addSynth(result, *layout, displayName)) {
    sequence.useBank(*synth);
  } else {
    result.warning("ChunSnes sequence found, but its active sound bank did not contain usable samples",
                   input.reader.range(0, input.reader.size()));
  }
  return result.finish();
}

}  // namespace

FormatModule module() {
  return FormatModule{.name = "ChunSnes",
                      .preferredSampleFilter = SampleFilter::SnesDspLowPass,
                      .acceptedFormats = {source_formats::kSnesAram},
                      .scan = scan};
}

}  // namespace vgmtrans::formats::chun_snes
