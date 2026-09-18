/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/scan/CollectionDiscovery.h"

#include <stdexcept>
#include <string>
#include <vector>

using namespace vgmtrans::core;

namespace {

void expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

struct ProbeData {
  u32 value = 0;
};

void matchingKeepsTiesAndRejectsIncompatibleCandidates() {
  const std::vector<int> scores{-2, 0, 2, 1, 2, -1};
  expect(bestMatches(scores, [](int score) { return score; }) == std::vector<const int*>{&scores[2], &scores[4]},
         "a stronger match should replace weaker candidates and retain every tie in input order");
  expect(bestMatches(scores, [](int) { return -1; }).empty(), "negative scores should reject all candidates");
  expect(bestMatches(scores, [](int) { return 0; }).size() == scores.size(),
         "zero-score candidates should remain available as equally weak fallbacks");
}

void discoveryExposesTypedAssetDataAndSources() {
  SourceStore sources;
  const SourceId source = sources.add(SourceFile{.name = "resolver.probe"}, std::vector<u8>(64));
  const AssetId sequenceId{10};
  const AssetId samplesId{11};

  std::vector<Asset> assets;
  assets.push_back(SequenceProgramAsset{
      .metadata =
          AssetMetadata{
              .id = sequenceId,
              .format = "Probe",
              .name = "Sequence",
              .range = sources.reader(source).range(4, 8),
          },
      .privateData = AssetPrivateData::make(ProbeData{.value = 9}),
  });
  assets.push_back(SamplePoolAsset{
      .metadata =
          AssetMetadata{
              .id = samplesId,
              .format = "Probe",
              .name = "Samples",
              .range = sources.reader(source).range(20, 16),
          },
  });

  for (const auto range : {SourceRange{}, SourceRange{.source = SourceId{99}}}) {
    auto detached = std::get<SequenceProgramAsset>(assets.front());
    detached.metadata.id = AssetId{static_cast<u32>(10 + assets.size())};
    detached.metadata.range = range;
    assets.push_back(std::move(detached));
  }

  const CollectionDiscoveryContext context(sources, SharedSequence<Asset>{std::move(assets)});
  const auto sequences = context.assetsWithData<SequenceProgramAsset, ProbeData>();
  expect(sequences.size() == 3 && sequences[0].id() == sequenceId && sequences[0].data->value == 9 &&
             sequences[0].sourceId() == source && sequences[0].source == &sources.source(source),
         "collection discovery should expose typed data and source metadata directly from an asset");
  expect(!sequences[1].sourceId().valid() && sequences[1].source == nullptr &&
             sequences[2].sourceId() == SourceId{99} && sequences[2].source == nullptr,
         "an unavailable source file should not erase a valid source ID");
  expect(context.asset<SequenceProgramAsset>(sequenceId) == sequences[0].asset &&
             context.asset<SamplePoolAsset>(samplesId) != nullptr &&
             context.assetsWithData<SamplePoolAsset, ProbeData>().empty(),
         "collection discovery should provide typed id lookup and omit assets without the requested private data");
}

}  // namespace

void runValueCollectionDiscoveryTests() {
  matchingKeepsTiesAndRejectsIncompatibleCandidates();
  discoveryExposesTypedAssetDataAndSources();
}
