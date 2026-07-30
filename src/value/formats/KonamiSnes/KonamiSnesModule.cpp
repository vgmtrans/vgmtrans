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

[[nodiscard]] bool canScanKonamiSnes(const SourceFile&, std::span<const u8> bytes) {
  return findKonamiSnesLayout(ByteReader(SourceId{}, bytes)).has_value();
}

[[nodiscard]] ScanResult scanKonamiSnes(const ScanInput& input) {
  const auto layout = findKonamiSnesLayout(input.reader);
  if (!layout) {
    return {};
  }

  ScanResultBuilder result(input, "KonamiSnes");
  const std::string displayName = result.sourceDisplayName();
  auto sequence = result.sequence(displayName, konamiSnesSequenceHeaderRange(input.reader, *layout));
  sequence.program(
      decodeKonamiSnesSequence(input.reader, *layout, sequence.id(), &result.sourceMap(), &result.diagnostics()));

  // A sequence is useful on its own, so publish it even when the snapshot does
  // not contain enough information to reconstruct instruments and samples.
  auto collection = result.sourceCollection(displayName);
  collection.sequence(sequence);

  const bool hasSynthLayout = layout->spcDirAddress && layout->commonInstrumentTableAddress &&
                              layout->bankedInstrumentTableAddress && layout->percussionInstrumentTableAddress;
  if (hasSynthLayout) {
    if (const auto synth = addKonamiSnesSynth(result, *layout, displayName)) {
      collection.instrumentSet(synth->instruments).samples(synth->samples);
    } else {
      result.warning("KonamiSnes sequence found, but no valid instruments or samples were discovered",
                     input.reader.range(0, input.reader.size()));
    }
  } else {
    result.warning("KonamiSnes sequence found, but instrument table or SPC DIR address was not detected",
                   input.reader.range(0, input.reader.size()));
  }

  result.sourceFact(sequence.id(),
                    FormatSpecificFact{
                        .kind = "konami-snes-version",
                        .fields = {MatchField{.name = "version", .value = konamiSnesVersionName(layout->version)}},
                    });

  return result.finish();
}

FormatDefinition konamiSnesDefinition() {
  return FormatDefinition{
      .module = {.name = "KonamiSnes", .canScan = canScanKonamiSnes, .scan = scanKonamiSnes},
      .sequenceDialects = konamiSnesSequenceDialects(),
  };
}

}  // namespace vgmtrans::formats::konami_snes
