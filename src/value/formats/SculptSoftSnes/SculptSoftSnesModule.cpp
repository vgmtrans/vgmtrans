/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/SculptSoftSnes/SculptSoftSnes.h"

namespace vgmtrans::formats::sculpt_soft_snes {

using namespace core;

namespace {
ScanResult scan(const ScanInput& input) {
  const auto layout = findLayout(input.reader);
  if (!layout) {
    return {};
  }
  ScanResultBuilder result(input, "SculptSoftSnes");
  const auto name = result.sourceDisplayName();
  const auto data = readDriverData(input.reader, *layout);
  std::set<u8> programs;
  auto sequence = result.sequence(name);
  sequence.program(decodeSequence(input.reader, *layout, data, sequence.id(), &result.sourceMap(),
                                  &result.diagnostics(), &programs));

  if (const auto bank = addSynth(result, *layout, data, programs, name)) {
    sequence.useBank(*bank);
  } else {
    result.warning("SculptSoftSnes sequence found without valid BRR samples", input.reader.range(layout->song, 1));
  }
  return result.finish();
}
}  // namespace

FormatModule module() {
  return FormatModule{.name = "SculptSoftSnes",
                      .preferredSampleFilter = SampleFilter::SnesDspLowPass,
                      .acceptedFormats = {source_formats::kSnesAram},
                      .scan = scan};
}

}  // namespace vgmtrans::formats::sculpt_soft_snes
