/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/CapcomSnes/CapcomSnes.h"
#include "value/scan/ScanResultBuilder.h"

#include <string>

namespace vgmtrans::formats::capcom_snes {

using namespace core;

namespace {

// Turns one recognized Capcom snapshot into a sequence collection and adds its
// instruments and samples when both required table locations were found.
[[nodiscard]] ScanResult scanCapcomSnes(const ScanInput& input) {
  const auto layout = findCapcomSnesLayout(input.reader);
  if (!layout) {
    return {};
  }

  ScanResultBuilder result(input, "CapcomSnes");
  const std::string displayName = result.sourceDisplayName();
  auto sequence = result.sequence(displayName, layout->sequenceHeaderRange);
  sequence.program(
      decodeCapcomSnesSequence(input.reader, *layout, sequence.id(), &result.sourceMap(), &result.diagnostics()));

  auto collection = result.sourceCollection(displayName);
  collection.sequence(sequence);

  if (!layout->instrumentTableAddress || !layout->spcDirAddress) {
    result.warning("CapcomSnes sequence found, but instrument table or SPC DIR address was not detected",
                   input.reader.range(0, input.reader.size()));
  } else {
    if (const auto synth =
            addCapcomSnesSynth(result, *layout->instrumentTableAddress, *layout->spcDirAddress, displayName)) {
      collection.instrumentSet(synth->instruments).samples(synth->samples);
    } else {
      result.warning("CapcomSnes sequence found, but no valid instruments or samples were discovered",
                     input.reader.range(0, input.reader.size()));
    }
  }

  return result.finish();
}

}  // namespace

FormatDefinition capcomSnesDefinition() {
  return FormatDefinition{
      .module = {.name = "CapcomSnes",
                 .preferredSampleFilter = SampleFilter::SnesDspLowPass,
                 .scan = scanCapcomSnes},
      .sequenceDialects = {capcomSnesSequenceDialect()},
  };
}

}  // namespace vgmtrans::formats::capcom_snes
