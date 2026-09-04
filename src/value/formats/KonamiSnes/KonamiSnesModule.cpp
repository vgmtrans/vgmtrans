/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/KonamiSnes/KonamiSnes.h"
#include "value/scan/FormatRegistry.h"
#include "value/scan/ScanResultBuilder.h"

#include <string>

namespace vgmtrans::formats::konami_snes {

using namespace core;

[[nodiscard]] ScanResult scanKonamiSnes(const ScanInput& input) {
  const auto layout = findKonamiSnesLayout(input.reader);
  if (!layout) {
    return {};
  }

  ScanResultBuilder result(input, "KonamiSnes");
  const std::string displayName = result.sourceDisplayName();
  const auto instruments = parseKonamiSnesInstrumentInfos(input.reader, *layout);
  auto sequence = result.sequence(displayName, konamiSnesSequenceHeaderRange(input.reader, *layout));
  sequence.program(decodeKonamiSnesSequence(input.reader, *layout, sequence.id(), instruments, &result.sourceMap(),
                                            &result.diagnostics()));

  // A sequence is useful on its own, so publish it even when the snapshot does
  // not contain enough information to reconstruct instruments and samples.
  auto collection = result.sourceCollection(displayName);
  collection.sequence(sequence);

  const bool hasSynthLayout = layout->spcDirAddress && layout->commonInstrumentTableAddress &&
                              layout->bankedInstrumentTableAddress && layout->percussionInstrumentTableAddress;
  if (hasSynthLayout) {
    if (const auto synth = addKonamiSnesSynth(result, *layout, instruments, displayName)) {
      collection.soundBank(*synth);
    } else {
      result.warning("KonamiSnes sequence found, but no valid instruments or samples were discovered",
                     input.reader.range(0, input.reader.size()));
    }
  } else {
    result.warning("KonamiSnes sequence found, but instrument table or SPC DIR address was not detected",
                   input.reader.range(0, input.reader.size()));
  }

  return result.finish();
}

FormatModule konamiSnesModule() {
  return FormatModule{.name = "KonamiSnes",
                      .preferredSampleFilter = SampleFilter::SnesDspLowPass,
                      .acceptedFormats = {source_formats::kSnesAram},
                      .scan = scanKonamiSnes};
}

}  // namespace vgmtrans::formats::konami_snes
