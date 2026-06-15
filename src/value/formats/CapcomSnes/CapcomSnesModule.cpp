/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/CapcomSnes/CapcomSnesModule.h"

#include "value/scan/FormatRegistry.h"
#include "value/formats/CapcomSnes/CapcomSnesSequence.h"
#include "value/formats/CapcomSnes/CapcomSnesLayout.h"
#include "value/formats/CapcomSnes/CapcomSnesSynth.h"
#include "value/scan/CollectionResolver.h"

#include <optional>
#include <string>
#include <utility>
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

void addCapcomCollectionMember(ScanResult& result, const ScanInput& input, AssetId asset, std::string displayName,
                               CollectionMemberRole role) {
  result.matchFacts.push_back(MatchFact{
      .asset = asset,
      .format = "CapcomSnes",
      .scope = MatchScope{.kind = MatchScopeKind::Source, .source = input.source.id},
      .payload =
          CollectionMemberFact{
              .key = capcomCollectionKey(input.source.id),
              .collectionName = std::move(displayName),
              .role = role,
          },
  });
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
    instrumentInfos =
        parseCapcomSnesInstrumentInfos(input.reader, *layout->instrumentTableAddress, *layout->spcDirAddress);
    sampleInfos = parseCapcomSnesSampleInfos(input.reader, *layout->spcDirAddress, instrumentInfos);
  }

  const bool hasInstrumentSet = !instrumentInfos.empty() && !sampleInfos.empty();
  result.assets.emplace_back(
      parseCapcomSnesSequence(input, *layout, sequenceId,
                              hasInstrumentSet ? std::optional<AssetId>{instrumentSetId} : std::nullopt, displayName));
  addCapcomCollectionMember(result, input, sequenceId, displayName, CollectionMemberRole::Sequence);

  if (hasInstrumentSet) {
    result.assets.emplace_back(parseCapcomSnesInstrumentSet(input, instrumentSetId, sampleCollectionId, instrumentInfos,
                                                            sampleInfos, displayName));
    result.assets.emplace_back(parseCapcomSnesSamples(input, sampleCollectionId, sampleInfos, displayName));
    addCapcomCollectionMember(result, input, instrumentSetId, displayName, CollectionMemberRole::InstrumentSet);
    addCapcomCollectionMember(result, input, sampleCollectionId, displayName, CollectionMemberRole::SampleCollection);
  }

  if (!layout->instrumentTableAddress || !layout->spcDirAddress) {
    result.diagnostics.push_back(Diagnostic{
        .severity = Severity::Warning,
        .message = "CapcomSnes sequence found, but instrument table or SPC DIR address was not detected",
        .range = input.reader.range(0, input.reader.size()),
    });
  }

  return result;
}

[[nodiscard]] std::vector<DesiredCollection> resolveCapcomSnesCollections(const MatchContext& context) {
  return resolveCollectionMemberFacts(context, "CapcomSnes", "CapcomSnes");
}

void registerCapcomSnesModule(FormatRegistry& registry) {
  registry.add(FormatModule{
      .name = "CapcomSnes",
      .canScan = canScanCapcomSnes,
      .scan = scanCapcomSnes,
      .resolveCollections = resolveCapcomSnesCollections,
  });
}

}  // namespace vgmtrans::formats::capcom_snes
