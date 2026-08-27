/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/SoftCreat/SoftCreat.h"

#include <string>

namespace vgmtrans::formats::softcreat {

using namespace core;

namespace {

[[nodiscard]] ScanResult scan(const ScanInput& input) {
  const auto layout = findLayout(input.reader);
  if (!layout) {
    return {};
  }

  ScanResultBuilder result(input, "SoftCreat");
  const std::string displayName = result.sourceDisplayName();
  auto sequence = result.sequence(displayName);
  SequenceParse parsed =
      decodeSequence(input.reader, *layout, sequence.id(), &result.sourceMap(), &result.diagnostics());
  sequence.range(sequenceSourceRange(input.reader, layout->sequenceHeaderRange, parsed.program))
      .program(std::move(parsed.program));

  auto collection = result.sourceCollection(displayName).sequence(sequence);
  if (const auto synth = addSynth(result, *layout, parsed.references, displayName)) {
    collection.soundBank(*synth);
  } else {
    result.warning("SoftCreat sequence found, but no valid referenced BRR instruments were discovered",
                   layout->sequenceHeaderRange);
  }
  return result.finish();
}

}  // namespace

FormatModule module() {
  return FormatModule{
      .name = "SoftCreat",
      .preferredSampleFilter = SampleFilter::SnesDspLowPass,
      .acceptedFormats = {source_formats::kSnesAram},
      .scan = scan,
  };
}

}  // namespace vgmtrans::formats::softcreat
