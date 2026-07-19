/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/KonamiSnes/KonamiSnesModule.h"

#include "value/formats/KonamiSnes/KonamiSnesLayout.h"
#include "value/formats/KonamiSnes/KonamiSnesSequence.h"
#include "value/formats/KonamiSnes/KonamiSnesSynth.h"
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
  const auto sequence = result.reserveSequence();
  const auto instrumentSet = result.reserveInstrumentSet();
  const auto samples = result.reserveSampleCollection();

  result.sequence(sequence, [&](AssetId id) {
    return parseKonamiSnesSequence(input, *layout, id, displayName, &result.sourceMap(), &result.diagnostics());
  });

  auto collection = result.sourceCollection(displayName);
  collection.sequence(sequence);

  const bool hasSynthLayout = layout->spcDirAddress && layout->commonInstrumentTableAddress &&
                              layout->bankedInstrumentTableAddress && layout->percussionInstrumentTableAddress;
  if (hasSynthLayout) {
    if (addKonamiSnesSynth(input, result, instrumentSet, samples, *layout, displayName)) {
      collection.instrumentSet(instrumentSet).samples(samples);
    } else {
      result.warning("KonamiSnes sequence found, but no valid instruments or samples were discovered",
                     input.reader.range(0, input.reader.size()));
    }
  } else {
    result.warning("KonamiSnes sequence found, but instrument table or SPC DIR address was not detected",
                   input.reader.range(0, input.reader.size()));
  }

  result.sourceFact(sequence.id,
                    FormatSpecificFact{
                        .kind = "konami-snes-version",
                        .fields = {MatchField{.name = "version", .value = konamiSnesVersionName(layout->version)}},
                    });

  return result.finish();
}

void registerKonamiSnesModule(FormatRegistry& registry) {
  registry.add(FormatModule{
      .name = "KonamiSnes",
      .canScan = canScanKonamiSnes,
      .scan = scanKonamiSnes,
  });
}

}  // namespace vgmtrans::formats::konami_snes
