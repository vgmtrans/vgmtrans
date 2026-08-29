/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/GraphResSnes/GraphResSnes.h"

#include <string>
#include <utility>

namespace vgmtrans::formats::graph_res_snes {

using namespace core;

namespace {

[[nodiscard]] ScanResult scan(const ScanInput& input) {
  const auto layout = findLayout(input.reader);
  if (!layout) {
    return {};
  }

  ScanResultBuilder result(input, "GraphResSnes");
  const std::string displayName = result.sourceDisplayName();
  auto sequence = result.sequence(displayName);
  SequenceParse parsed =
      decodeSequence(input.retain(), *layout, sequence.id(), &result.sourceMap(), &result.diagnostics());
  sequence.range(sequenceSourceRange(input.reader, parsed.headerRange, parsed.program))
      .program(std::move(parsed.program));

  auto collection = result.sourceCollection(displayName).sequence(sequence);
  if (const auto synth = addSynth(result, *layout, parsed.programs, displayName)) {
    collection.soundBank(*synth);
  } else {
    result.warning("GraphResSnes sequence found, but no valid instruments or samples were discovered",
                   input.reader.range(0, input.reader.size()));
  }
  return result.finish();
}

}  // namespace

FormatModule module() {
  return FormatModule{.name = "GraphResSnes",
                      .preferredSampleFilter = SampleFilter::SnesDspLowPass,
                      .acceptedFormats = {source_formats::kSnesAram},
                      .scan = scan};
}

}  // namespace vgmtrans::formats::graph_res_snes
