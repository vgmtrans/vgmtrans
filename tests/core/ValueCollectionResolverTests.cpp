/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/scan/CollectionResolver.h"

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

void matchFactsAreJoinedOncePerAsset() {
  SourceStore sources;
  const SourceId source = sources.add(SourceFile{.name = "resolver.probe"}, std::vector<u8>(64));
  const AssetId sequenceId{10};
  const AssetId samplesId{11};

  SessionSnapshotBuilder builder;
  builder.sources.push_back(sources.source(source));
  builder.assets.push_back(SequenceProgramAsset{
      .metadata =
          AssetMetadata{
              .id = sequenceId,
              .format = "Probe",
              .name = "Sequence",
              .range = sources.reader(source).range(4, 8),
          },
  });
  builder.assets.push_back(SampleCollectionAsset{
      .metadata =
          AssetMetadata{
              .id = samplesId,
              .format = "Probe",
              .name = "Samples",
              .range = sources.reader(source).range(20, 16),
          },
  });
  const MatchScope scope{.kind = MatchScopeKind::Source, .source = source};
  const auto addFact = [&](AssetId asset, MatchFactPayload payload) {
    builder.matchFacts.push_back(MatchFact{
        .asset = asset,
        .format = "Probe",
        .scope = scope,
        .payload = std::move(payload),
    });
  };
  addFact(sequenceId, IdMatchFact{.domain = "sequence", .value = 9});
  addFact(sequenceId, IdMatchFact{.domain = "sample-set", .value = 3});
  addFact(sequenceId, OffsetOrderFact{.offset = 4});
  addFact(sequenceId, SampleRequirementFact{.domain = "articulation", .required = {7, 5}});
  addFact(sequenceId, SampleRequirementFact{.domain = "articulation", .required = {7, 8}});
  addFact(samplesId, SampleCoverageFact{.domain = "articulation", .first = 5, .count = 4});

  const SessionSnapshot snapshot = builder.finish();
  const MatchFactIndex index(MatchContext{.sources = sources, .snapshot = snapshot});
  const auto sequences = index.assets<SequenceProgramAsset>("Probe");
  expect(sequences.size() == 1 && sequences[0].asset().metadata.id == sequenceId && sequences[0].sourceId == source &&
             sequences[0].source != nullptr,
         "the match index should expose one source-aware fact set per asset");
  expect(sequences[0].id("sequence") == 9 && sequences[0].id("sample-set") == 3 && sequences[0].offset() == 4 &&
             sequences[0].requirements("articulation") == std::vector<u32>({5, 7, 8}),
         "an asset fact set should join ids, offsets, and deduplicated requirements without caller maps");

  const auto sampleSets = index.assets<SampleCollectionAsset>("Probe");
  const auto coverage = sampleSets[0].coverage("articulation");
  expect(sampleSets.size() == 1 && coverage && coverage->first == 5 && coverage->count == 4,
         "sample coverage should be available from the same aggregated fact surface");

  DesiredCollection collection{
      .sequence = sequenceId,
      .sampleCollections = {samplesId},
  };
  ScanIdAllocator ids;
  const MaterializationContext context{
      .sources = sources,
      .snapshot = snapshot,
      .collection = collection,
      .ids = ids,
      .assetIdForSlot = [](std::string_view) { return AssetId{100}; },
  };
  const auto input = context.inputFor(snapshot.asset<SequenceProgramAsset>(sequenceId)->metadata.range);
  expect(context.sequenceAsset() != nullptr && context.selectedSampleCollections().size() == 1 && input &&
             input->reader.source() == source && !context.inputFor(SourceRange{}),
         "materialization should resolve selected assets and source input through one common context API");
}

}  // namespace

void runValueCollectionResolverTests() {
  matchFactsAreJoinedOncePerAsset();
}
