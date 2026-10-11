/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/WinkysoftSnes/WinkysoftSnes.h"

namespace vgmtrans::formats::winkysoft_snes {

using namespace core;

namespace {

ScanResult scan(const ScanInput& input) {
  const auto layout = findLayout(input.reader);
  if (!layout) {
    return {};
  }
  ScanResultBuilder result(input, "WinkysoftSnes");
  const auto data = readDriverData(input.reader, *layout);
  const auto name = result.sourceDisplayName();
  auto sequence = result.sequence(name);
  auto parsed = decodeSequence(input.reader, *layout, data, sequence.id(), &result.sourceMap(), &result.diagnostics());
  sequence.program(std::move(parsed.program));
  if (const auto bank = addSynth(result, *layout, data, parsed.instruments, parsed.noiseRates, name)) {
    sequence.useBank(*bank);
  } else {
    result.warning("WinkysoftSnes sequence found without valid referenced BRR instruments",
                   input.reader.range(layout->instrumentTable, 8));
  }
  return result.finish();
}

}  // namespace

FormatModule module() {
  return FormatModule{.name = "WinkysoftSnes", .preferredSampleFilter = SampleFilter::SnesDspLowPass,
                      .acceptedFormats = {source_formats::kSnesAram}, .scan = scan};
}

}  // namespace vgmtrans::formats::winkysoft_snes
