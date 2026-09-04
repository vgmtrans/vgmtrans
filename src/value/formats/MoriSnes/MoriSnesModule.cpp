/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/MoriSnes/MoriSnes.h"

#include "value/sequence/CommandSourceMap.h"

#include <string>
#include <utility>

namespace vgmtrans::formats::mori_snes {

using namespace core;

namespace {

[[nodiscard]] ScanResult scan(const ScanInput& input) {
  const auto layout = findLayout(input.reader);
  if (!layout) {
    return {};
  }

  ScanResultBuilder result(input, "MoriSnes");
  const std::string displayName = result.sourceDisplayName();
  auto sequence = result.sequence(displayName);
  SequenceParse parsed =
      decodeSequence(input.reader, *layout, sequence.id(), &result.sourceMap(), &result.diagnostics());
  sequence.range(sequenceSourceRange(input.reader, parsed.headerRange, parsed.program))
      .program(std::move(parsed.program));

  auto collection = result.sourceCollection(displayName).sequence(sequence);
  if (const auto synth = addSynth(result, *layout, parsed.references, displayName)) {
    collection.soundBank(*synth);
  } else {
    result.warning("MoriSnes sequence found, but no valid referenced instruments or samples were discovered",
                   input.reader.range(0, input.reader.size()));
  }
  return result.finish();
}

}  // namespace

FormatModule module() {
  return FormatModule{.name = "MoriSnes",
                      .preferredSampleFilter = SampleFilter::SnesDspLowPass,
                      .acceptedFormats = {source_formats::kSnesAram},
                      .scan = scan};
}

}  // namespace vgmtrans::formats::mori_snes
