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

  const CollectionDiscoveryContext context(sources, SharedSequence<Asset>{std::move(assets)});
  const auto sequences = context.assetsWithData<SequenceProgramAsset, ProbeData>();
  expect(sequences.size() == 1 && sequences[0].id() == sequenceId && sequences[0].data->value == 9 &&
             sequences[0].sourceId() == source && sequences[0].source == &sources.source(source),
         "collection discovery should expose typed data and source metadata directly from an asset");
  expect(context.asset<SequenceProgramAsset>(sequenceId) == sequences[0].asset &&
             context.asset<SamplePoolAsset>(samplesId) != nullptr &&
             context.assetsWithData<SamplePoolAsset, ProbeData>().empty(),
         "collection discovery should provide typed id lookup and omit assets without the requested private data");
}

}  // namespace

void runValueCollectionDiscoveryTests() {
  discoveryExposesTypedAssetDataAndSources();
}
