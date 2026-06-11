/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/CapcomSnes/CapcomSnesModule.h"

#include "value/core/FormatRegistry.h"
#include "value/formats/CapcomSnes/CapcomSnesSequenceProgram.h"
#include "value/formats/CapcomSnes/CapcomSnesValueLayout.h"
#include "value/formats/CapcomSnes/CapcomSnesValueSynth.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace vgmtrans::formats::capcom_snes {

using namespace core;

[[nodiscard]] bool canScanCapcomSnes(const SourceFile&, std::span<const u8> bytes) {
  return findCapcomSnesLayout(ByteReader(SourceId{}, bytes)).has_value();
}

[[nodiscard]] ScanResult scanCapcomSnes(const ScanInput& input) {
  const auto layout = findCapcomSnesLayout(input.reader);
  if (!layout) {
    return {};
  }

  // The module only orchestrates value construction; layout, sequence, and synth parsing stay isolated.
  const std::string displayName = capcomSnesSourceDisplayName(input.source);
  const auto sequenceId = input.ids.nextAssetId();
  const auto instrumentSetId = input.ids.nextAssetId();
  const auto sampleCollectionId = input.ids.nextAssetId();

  ScanResult result;

  std::vector<CapcomSnesInstrumentInfo> instrumentInfos;
  std::vector<CapcomSnesSampleInfo> sampleInfos;
  if (layout->instrumentTableAddress && layout->spcDirAddress) {
    instrumentInfos = parseCapcomSnesInstrumentInfos(input.reader,
                                                     *layout->instrumentTableAddress,
                                                     *layout->spcDirAddress);
    sampleInfos = parseCapcomSnesSampleInfos(input.reader, *layout->spcDirAddress, instrumentInfos);
  }

  const bool hasInstrumentSet = !instrumentInfos.empty() && !sampleInfos.empty();
  result.assets.emplace_back(parseCapcomSnesSequenceProgram(input,
                                                            *layout,
                                                            sequenceId,
                                                            hasInstrumentSet ? std::optional<AssetId>{instrumentSetId}
                                                                              : std::nullopt,
                                                            displayName));

  if (hasInstrumentSet) {
    result.assets.emplace_back(parseCapcomSnesInstrumentSet(input,
                                                             instrumentSetId,
                                                             sampleCollectionId,
                                                             instrumentInfos,
                                                             sampleInfos,
                                                             displayName));
    result.assets.emplace_back(parseCapcomSnesSamples(input, sampleCollectionId, sampleInfos, displayName));
  }

  Collection collection{
      .id = input.ids.nextCollectionId(),
      .name = displayName,
      .sequence = sequenceId,
  };
  if (hasInstrumentSet) {
    collection.instrumentSets.push_back(instrumentSetId);
    collection.sampleCollections.push_back(sampleCollectionId);
  }
  result.collections.push_back(std::move(collection));

  if (!layout->instrumentTableAddress || !layout->spcDirAddress) {
    result.diagnostics.push_back(Diagnostic{
        .severity = Severity::Warning,
        .message = "CapcomSnes sequence found, but instrument table or SPC DIR address was not detected",
        .range = input.reader.range(0, input.reader.size()),
    });
  }

  return result;
}

void registerCapcomSnesModule(FormatRegistry& registry) {
  registry.add(FormatModule{
      .name = "CapcomSnes",
      .canScan = canScanCapcomSnes,
      .scan = scanCapcomSnes,
  });
}

}  // namespace vgmtrans::formats::capcom_snes
