/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/OceanSnes/OceanSnes.h"

namespace vgmtrans::formats::ocean_snes {
using namespace core;

namespace {
ScanResult scan(const ScanInput& input) {
  const auto layout = findLayout(input.reader);
  if (!layout) {
    return {};
  }
  ScanResultBuilder result(input, "OceanSnes");
  const auto data = std::make_shared<const DriverData>(readDriverData(input.reader, *layout, result.diagnostics()));
  const auto name = result.sourceDisplayName();
  auto sequence = result.sequence(name);
  sequence.program(
      decodeSequence(input.retain(), *layout, data, sequence.id(), &result.sourceMap(), &result.diagnostics()));
  if (auto bank = addSynth(result, *layout, *data, name)) {
    sequence.useBank(*bank);
  } else {
    result.warning("OceanSnes music found without valid instruments", input.reader.range(layout->instruments, 2));
  }
  return result.finish();
}
}  // namespace

FormatModule module() {
  return FormatModule{.name = "OceanSnes",
                      .preferredSampleFilter = SampleFilter::SnesDspLowPass,
                      .acceptedFormats = {source_formats::kSnesAram},
                      .scan = scan};
}

}  // namespace vgmtrans::formats::ocean_snes
