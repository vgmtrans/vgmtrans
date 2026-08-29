/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/NamcoSnes/NamcoSnes.h"

#include <fmt/format.h>

namespace vgmtrans::formats::namco_snes {

using namespace core;

namespace {

[[nodiscard]] ScanResult scan(const ScanInput& input) {
  const auto layout = findLayout(input.reader);
  if (!layout) {
    return {};
  }

  ScanResultBuilder result(input, "NamcoSnes");
  const std::string name = fmt::format("{} ({})", result.sourceDisplayName(), versionName(layout->version));
  auto sequence = result.sequence(name);
  SequenceParse parsed =
      decodeSequence(input.retain(), *layout, sequence.id(), &result.sourceMap(), &result.diagnostics());
  sequence.range(sequenceSourceRange(input.reader, parsed.headerRange, parsed.program))
      .program(std::move(parsed.program));

  auto collection = result.sourceCollection(name).sequence(sequence);
  if (const auto synth = addSynth(result, *layout, parsed.srcns, parsed.percussion, name)) {
    collection.soundBank(*synth);
  } else {
    result.warning("NamcoSnes sequence found, but no valid instruments or samples were discovered",
                   input.reader.range(0, input.reader.size()));
  }
  return result.finish();
}

}  // namespace

FormatModule module() {
  return FormatModule{.name = "NamcoSnes",
                      .preferredSampleFilter = SampleFilter::SnesDspLowPass,
                      .acceptedFormats = {source_formats::kSnesAram},
                      .scan = scan};
}

}  // namespace vgmtrans::formats::namco_snes
