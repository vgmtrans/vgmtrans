/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/CapcomSnes/CapcomSnesModule.h"

#include "value/formats/CapcomSnes/CapcomSnesLayout.h"
#include "value/formats/CapcomSnes/CapcomSnesSequence.h"
#include "value/formats/CapcomSnes/CapcomSnesSynth.h"
#include "value/scan/FormatRegistry.h"
#include "value/scan/ScanResultBuilder.h"

#include <optional>
#include <string>
#include <vector>

namespace vgmtrans::formats::capcom_snes {

using namespace core;

[[nodiscard]] bool canScanCapcomSnes(const SourceFile&, std::span<const u8> bytes) {
  return findCapcomSnesLayout(ByteReader(SourceId{}, bytes)).has_value();
}

[[nodiscard]] CollectionKey capcomCollectionKey(SourceId source) {
  return CollectionKey{
      .resolver = "CapcomSnes",
      .value = "source:" + std::to_string(source.value),
  };
}

[[nodiscard]] ScanResult scanCapcomSnes(const ScanInput& input) {
  const auto layout = findCapcomSnesLayout(input.reader);
  if (!layout) {
    return {};
  }

  // Keep this file as wiring: layout discovery, sequence parsing, and synth parsing each stay in their own file.
  const std::string displayName = capcomSnesSourceDisplayName(input.source);
  ScanResultBuilder result(input, "CapcomSnes");
  const auto sequence = result.reserveSequence();
  const auto instrumentSet = result.reserveInstrumentSet();
  const auto samples = result.reserveSampleCollection();

  std::vector<CapcomSnesInstrumentInfo> instrumentInfos;
  std::vector<CapcomSnesSampleInfo> sampleInfos;
  if (layout->instrumentTableAddress && layout->spcDirAddress) {
    instrumentInfos =
        parseCapcomSnesInstrumentInfos(input.reader, *layout->instrumentTableAddress, *layout->spcDirAddress);
    sampleInfos = parseCapcomSnesSampleInfos(input.reader, *layout->spcDirAddress, instrumentInfos);
  }

  const bool hasInstrumentSet = !instrumentInfos.empty() && !sampleInfos.empty();
  static_cast<void>(result.sequence(sequence, [&](AssetId id) {
    std::vector<Diagnostic> sequenceDiagnostics;
    auto asset = parseCapcomSnesSequence(input, *layout, id, displayName, &result.sourceMap(), &sequenceDiagnostics);
    for (auto& diagnostic : sequenceDiagnostics) {
      result.diagnostic(std::move(diagnostic));
    }
    return asset;
  }));

  auto collection = result.collection(displayName, capcomCollectionKey(input.source.id));
  collection.sequence(sequence);

  if (hasInstrumentSet) {
    static_cast<void>(result.instrumentSet(instrumentSet, [&](AssetId id) {
      return parseCapcomSnesInstrumentSet(input, result, id, samples, instrumentInfos, sampleInfos, displayName);
    }));
    static_cast<void>(result.sampleCollection(samples, [&](AssetId id) {
      return parseCapcomSnesSamples(input, id, sampleInfos, displayName, &result.sourceMap());
    }));
    collection.instrumentSet(instrumentSet).samples(samples);
  }

  if (!layout->instrumentTableAddress || !layout->spcDirAddress) {
    result.warning("CapcomSnes sequence found, but instrument table or SPC DIR address was not detected",
                   input.reader.range(0, input.reader.size()));
  }

  return result.finish();
}

void registerCapcomSnesModule(FormatRegistry& registry) {
  registry.add(FormatModule{
      .name = "CapcomSnes",
      .canScan = canScanCapcomSnes,
      .scan = scanCapcomSnes,
  });
}

}  // namespace vgmtrans::formats::capcom_snes
