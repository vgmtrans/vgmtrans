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
#include <vector>

namespace vgmtrans::formats::konami_snes {

using namespace core;

[[nodiscard]] bool canScanKonamiSnes(const SourceFile&, std::span<const u8> bytes) {
  return findKonamiSnesLayout(ByteReader(SourceId{}, bytes)).has_value();
}

[[nodiscard]] CollectionKey konamiCollectionKey(SourceId source) {
  return CollectionKey{
      .resolver = "KonamiSnes",
      .value = "source:" + std::to_string(source.value),
  };
}

[[nodiscard]] ScanResult scanKonamiSnes(const ScanInput& input) {
  const auto layout = findKonamiSnesLayout(input.reader);
  if (!layout) {
    return {};
  }

  const std::string displayName = konamiSnesSourceDisplayName(input.source);
  ScanResultBuilder result(input, "KonamiSnes");
  const auto sequence = result.reserveSequence();
  const auto instrumentSet = result.reserveInstrumentSet();
  const auto samples = result.reserveSampleCollection();

  static_cast<void>(result.sequence(sequence, [&](AssetId id) {
    std::vector<Diagnostic> diagnostics;
    auto asset = parseKonamiSnesSequence(input, *layout, id, displayName, &result.sourceMap(), &diagnostics);
    for (auto& diagnostic : diagnostics) {
      result.diagnostic(std::move(diagnostic));
    }
    return asset;
  }));

  auto collection = result.collection(displayName, konamiCollectionKey(input.source.id));
  collection.sequence(sequence);

  const bool hasSynthLayout = layout->spcDirAddress && layout->commonInstrumentTableAddress &&
                              layout->bankedInstrumentTableAddress && layout->percussionInstrumentTableAddress;
  if (hasSynthLayout) {
    const auto instrumentInfos = parseKonamiSnesInstrumentInfos(input.reader, *layout);
    const auto sampleInfos = parseKonamiSnesSampleInfos(input.reader, *layout->spcDirAddress, instrumentInfos);
    if (!instrumentInfos.empty() && !sampleInfos.empty()) {
      static_cast<void>(result.instrumentSet(instrumentSet, [&](AssetId id) {
        return parseKonamiSnesInstrumentSet(input, result, id, samples, layout->version, *layout->spcDirAddress,
                                            instrumentInfos, sampleInfos, displayName);
      }));
      static_cast<void>(result.sampleCollection(samples, [&](AssetId id) {
        return parseKonamiSnesSamples(input, id, sampleInfos, displayName, &result.sourceMap());
      }));
      collection.instrumentSet(instrumentSet).samples(samples);
    } else {
      result.warning("KonamiSnes sequence found, but no valid instruments or samples were discovered",
                     input.reader.range(0, input.reader.size()));
    }
  } else {
    result.warning("KonamiSnes sequence found, but instrument table or SPC DIR address was not detected",
                   input.reader.range(0, input.reader.size()));
  }

  result.sourceFact(sequence.id, FormatSpecificFact{
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
