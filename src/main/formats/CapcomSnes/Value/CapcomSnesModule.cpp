/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "formats/CapcomSnes/Value/CapcomSnesModule.h"

#include "formats/CapcomSnes/Value/CapcomSnesValueLayout.h"
#include "formats/CapcomSnes/Value/CapcomSnesValueSequence.h"
#include "formats/CapcomSnes/Value/CapcomSnesValueSynth.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace vgmtrans::formats::capcom_snes {

using namespace core;

std::string_view CapcomSnesModule::name() const {
  return "CapcomSnes";
}

bool CapcomSnesModule::canScan(const SourceFile&, std::span<const u8> bytes) const {
  return findCapcomSnesLayout(ByteReader(SourceId{}, bytes)).has_value();
}

ScanResult CapcomSnesModule::scan(const ScanInput& input) const {
  const auto layout = findCapcomSnesLayout(input.reader);
  if (!layout) {
    return {};
  }

  // The module only orchestrates value construction; layout, sequence, and synth parsing stay isolated.
  const std::string displayName = capcomSnesSourceDisplayName(input.source);
  const auto sequenceId = input.ids.nextAssetId();
  const auto instrumentBankId = input.ids.nextAssetId();
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

  const bool hasInstrumentBank = !instrumentInfos.empty() && !sampleInfos.empty();
  result.assets.emplace_back(parseCapcomSnesSequence(input,
                                                     *layout,
                                                     sequenceId,
                                                     hasInstrumentBank ? std::optional<AssetId>{instrumentBankId}
                                                                       : std::nullopt,
                                                     displayName));

  if (hasInstrumentBank) {
    result.assets.emplace_back(parseCapcomSnesInstrumentBank(input,
                                                             instrumentBankId,
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
  if (hasInstrumentBank) {
    collection.instrumentBanks.push_back(instrumentBankId);
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
  registry.add(std::make_unique<CapcomSnesModule>());
}

}  // namespace vgmtrans::formats::capcom_snes
