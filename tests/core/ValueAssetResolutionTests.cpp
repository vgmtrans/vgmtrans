/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "../TestSupport.h"

#include "value/scan/AssetResolution.h"

#include "value/export/AssetPreparation.h"
#include "value/export/CollectionBinding.h"
#include "SessionSnapshotBuilder.h"

#include <array>
#include <stdexcept>
#include <string>
#include <vector>

using namespace vgmtrans::core;

namespace {

struct ProbeData {
  u32 value = 0;
};

void collectionIssuesDeriveImpact() {
  const CollectionIssue missingSequence = missingSequenceIssue();
  expect(missingSequence.impact == CollectionIssueImpact::Incomplete && missingSequence.severity == Severity::Warning &&
             missingSequence.code == "missing-sequence",
         "missing sequence helper should create a warning issue");
  const std::vector<CollectionIssue> missingIssues{missingSequence};
  expect(Collection{.issues = missingIssues}.issueImpact() == CollectionIssueImpact::Incomplete,
         "missing issues should make a collection incomplete");

  const CollectionIssue missingInstrument = missingSoundBankIssue(AssetId{7});
  expect(missingInstrument.severity == Severity::Error && missingInstrument.asset == AssetId{7},
         "missing instrument helper should preserve a broken asset reference");

  const CollectionIssue ambiguous = ambiguousMatchIssue("multiple banks match");
  const std::vector<CollectionIssue> ambiguousIssues{ambiguous};
  expect(Collection{.issues = ambiguousIssues}.issueImpact() == CollectionIssueImpact::Ambiguous,
         "ambiguous match issue should make a collection ambiguous");
  expect(Collection{.issues = {missingSequence, ambiguous}}.issueImpact() == CollectionIssueImpact::Ambiguous,
         "ambiguity should take precedence when a collection is also incomplete");

  const Collection incomplete{.issues = {missingSamplePoolIssue()}};
  expect(incomplete.issueImpact() == CollectionIssueImpact::Incomplete,
         "collection impact should be derived from its issues");
}

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

  const AssetCatalog context(sources, SharedSequence<Asset>{std::move(assets)});
  const auto sequences = context.assetsWithData<SequenceProgramAsset, ProbeData>();
  expect(sequences.size() == 3 && sequences[0].id() == sequenceId && sequences[0].data->value == 9 &&
             sequences[0].sourceId() == source && sequences[0].source != nullptr && sequences[0].source->id == source,
         "collection discovery should expose typed data and source metadata directly from an asset");
  expect(!sequences[1].sourceId().valid() && sequences[1].source == nullptr &&
             sequences[2].sourceId() == SourceId{99} && sequences[2].source == nullptr,
         "an unavailable source file should not erase a valid source ID");
  expect(context.asset<SequenceProgramAsset>(sequenceId) == sequences[0].asset &&
             context.asset<SamplePoolAsset>(samplesId) != nullptr &&
             context.assetsWithData<SamplePoolAsset, ProbeData>().empty(),
         "collection discovery should provide typed id lookup and omit assets without the requested private data");
}

struct PoolData {};

DependencySelector exact(AssetId id, u32 position = 0) {
  return [id, position](const DependencyContext&) {
    DependencySelection result;
    result.add(id, AssetPrivateData::make(position));
    return result;
  };
}

void dependenciesPreserveSharingPlacementsAndPrivatePreparation() {
  SourceStore sources;
  const AssetId sequenceId{1}, firstBank{2}, secondBank{3}, poolId{4};
  SequenceProgramAsset sequence{
      .metadata = {.id = sequenceId, .format = "Sequence"},
      .recipe = {.collectionNamespace = "Sequence",
                 .dependencies = {{.select = exact(firstBank)}, {.select = exact(secondBank)}}},
  };
  auto bank = [&](AssetId id, u32 position) {
    return SoundBankAsset{
        .metadata = {.id = id, .format = "Bank"},
        .instruments = {Instrument{.regions = {Region{.sample = SampleRef::unbound(0)}}}},
        .recipe = {.dependencies = {{.role = DependencyRole::SamplePool, .select = exact(poolId, position)}}},
        .prepare =
            [](BankPreparationContext& context) {
              const auto inputs = context.samples<PoolData>();
              expect(inputs.size() == 1, "each bank must receive exactly its own selected input");
              const auto& input = inputs.front();
              const auto position = *input.placement.get<u32>();
              context.bank.instruments.front().regions.front().sample =
                  SampleRef::resolved(input.asset.metadata.id, position);
              context.bank.instruments.front().explicitAddress = InstrumentAddress{.bank = context.bankIndex};
            },
    };
  };
  test::SessionSnapshotBuilder builder;
  builder.assets = {sequence, bank(firstBank, 2), bank(secondBank, 5),
                    SamplePoolAsset{.metadata = {.id = poolId},
                                    .pool = {.samples = std::vector<Sample>(6)},
                                    .privateData = AssetPrivateData::make(PoolData{})}};
  const AssetCatalog catalog(sources, SharedSequence<Asset>{builder.assets});
  auto roots = dependencyCollections(catalog);
  expect(roots.size() == 1, "one requesting sequence should derive one collection");
  const auto& desired = roots.front().second;
  expect(desired.members.soundBanks == std::vector{firstBank, secondBank} &&
             desired.members.samplePools == std::vector{poolId} && desired.dependencies.size() == 4,
         "membership should deduplicate shared samples while keeping both banks' relationships");
  builder.collections.push_back({.id = CollectionId{1},
                                 .members = desired.members,
                                 .issues = desired.issues,
                                 .dependencies = desired.dependencies});
  const auto snapshot = builder.finish();
  const auto prepared = bindCollection(snapshot, CollectionId{1});
  expect(prepared.collection.has_value(), "cross-format banks should prepare through their own asset hooks");
  const auto& banks = prepared.collection->soundBanks();
  expect(banks[0].instruments.front().regions.front().sample.index() == 2 &&
             banks[1].instruments.front().regions.front().sample.index() == 5 &&
             banks[1].instruments.front().explicitAddress->bank == 1,
         "shared providers should preserve per-bank positions and collection-local bank numbers");
  expect(snapshot.asset<SoundBankAsset>(firstBank)->instruments.front().regions.front().sample.needsBinding(),
         "preparation must leave the durable bank unbound");
  const auto standalone = bindSoundBank(snapshot, secondBank);
  expect(standalone.collection &&
             standalone.collection->soundBanks().front().instruments.front().explicitAddress->bank == 0,
         "standalone bank preparation should resolve its samples and assign its own bank slot");
  expect(dependencyCollections(catalog, std::array{sequenceId}).empty(),
         "an explicit collection should suppress a duplicate automatic root");
}

void manualChoicesOverrideSequenceRequestsAndConstrainBankInputs() {
  SourceStore sources;
  SequenceProgramAsset sequence{
      .metadata = {.id = AssetId{1}},
      .recipe = {.collectionNamespace = "Probe", .dependencies = {{.select = exact(AssetId{99})}}},
  };
  SoundBankAsset bank{
      .metadata = {.id = AssetId{2}},
      .recipe = {.dependencies = {{.role = DependencyRole::SamplePool,
                                   .select =
                                       [](const DependencyContext& context) {
                                         return selectAll(context.candidates<SamplePoolAsset, PoolData>());
                                       }}}},
  };
  std::vector<Asset> assets{sequence, bank};
  for (u32 id : {3, 4, 5}) {
    assets.push_back(
        SamplePoolAsset{.metadata = {.id = AssetId{id}}, .privateData = AssetPrivateData::make(PoolData{})});
  }
  const AssetCatalog catalog(sources, SharedSequence<Asset>{assets});
  DesiredCollection manual{
      .members = {.sequence = AssetId{1}, .soundBanks = {AssetId{2}}, .samplePools = {AssetId{5}, AssetId{3}}}};
  resolveDependencies(catalog, manual, ResolutionMode::Manual);
  expect(
      manual.issues.empty() && manual.members.soundBanks == std::vector{AssetId{2}} && manual.dependencies.size() == 2,
      "manual bank choices should override even a sequence's exact bank request");
  const auto& inputs = manual.dependencies.front().targets;
  expect(inputs.size() == 2 && inputs[0].asset == AssetId{5} && inputs[1].asset == AssetId{3},
         "manual sample candidates must preserve user precedence and exclude unselected assets");

  auto& multipleRequests = std::get<SequenceProgramAsset>(assets.front()).recipe.dependencies;
  multipleRequests.push_back({.select = exact(AssetId{98})});
  DesiredCollection overridden{.members = manual.members};
  resolveDependencies(AssetCatalog{sources, SharedSequence<Asset>{assets}}, overridden, ResolutionMode::Manual);
  expect(overridden.dependencies.size() == 2,
         "manual choices should replace all requests for a role with one ordered selection");

  std::get<SoundBankAsset>(assets[1]).recipe.dependencies.front().select = exact(AssetId{4});
  DesiredCollection escaped{.members = manual.members};
  resolveDependencies(AssetCatalog{sources, SharedSequence<Asset>{assets}}, escaped, ResolutionMode::Manual);
  expect(escaped.issues.size() == 1 && escaped.issues.front().code == "invalid-dependency",
         "a selector must not escape the manual candidate set by returning an arbitrary id");
}

void dependencyFailuresAreLocalAndAmbiguityRetainsAlternatives() {
  SourceStore sources;
  SequenceProgramAsset sequence{
      .metadata = {.id = AssetId{1}},
      .recipe = {.collectionNamespace = "Probe", .dependencies = {{.select = [](const DependencyContext& context) {
                                                   return selectOne(context.candidates<SoundBankAsset>());
                                                 }}}}};
  SoundBankAsset first{.metadata = {.id = AssetId{2}}};
  SoundBankAsset second{.metadata = {.id = AssetId{3}}};
  auto resolve = [&](std::vector<Asset> assets) {
    return dependencyCollections(AssetCatalog{sources, SharedSequence<Asset>{std::move(assets)}});
  };
  const auto ambiguous = resolve({sequence, first, second});
  const auto& choice = ambiguous.front().second;
  expect(choice.members.soundBanks.empty() &&
             choice.dependencies.front().alternatives == std::vector{AssetId{2}, AssetId{3}} &&
             choice.issues.front().impact == CollectionIssueImpact::Ambiguous,
         "an unresolved choice must retain candidates without claiming both providers");

  sequence.recipe.dependencies.front().select = exact(first.metadata.id);
  first.recipe.dependencies.push_back({.select = exact(second.metadata.id)});
  second.recipe.dependencies.push_back({.select = exact(first.metadata.id)});
  const auto cyclic = resolve({sequence, first, second});
  expect(std::ranges::any_of(cyclic.front().second.issues,
                             [](const auto& issue) { return issue.code == "dependency-cycle"; }),
         "malformed cyclic dependencies should report a failure without recursing forever");
  first.recipe.dependencies.front().select = [](const DependencyContext&) -> DependencySelection { throw 7; };
  const auto threw = resolve({sequence, first, second});
  expect(threw.front().second.issues.front().code == "dependency-resolution-failed",
         "nonstandard selector exceptions must also remain local to the affected collection");
}

void automaticCandidatesRespectContainerBoundaries() {
  SourceStore sources;
  const SourceId first = sources.add(SourceFile{.name = "first.container"}, {0});
  const SourceId second = sources.add(SourceFile{.name = "second.container"}, {0});
  const SourceId sequenceFile = sources.add(SourceFile{.name = "song", .parent = first}, {0});
  const SourceId bankFile = sources.add(SourceFile{.name = "bank", .parent = second}, {0});
  SequenceProgramAsset sequence{
      .metadata = {.id = AssetId{1}, .range = sources.reader(sequenceFile).range(0, 1)},
      .recipe = {.collectionNamespace = "Probe",
                 .dependencies = {{.select =
                                       [](const DependencyContext& context) {
                                         return selectOne(context.candidates<SoundBankAsset>());
                                       }}}},
  };
  SoundBankAsset bank{.metadata = {.id = AssetId{2}, .range = sources.reader(bankFile).range(0, 1)}};
  const AssetCatalog catalog(sources, SharedSequence<Asset>{std::vector<Asset>{sequence, bank}});
  const auto automatic = dependencyCollections(catalog);
  expect(automatic.front().second.members.soundBanks.empty(),
         "an incomplete container must not borrow another container's sole bank");
  DesiredCollection manual{.members = {.sequence = AssetId{1}, .soundBanks = {AssetId{2}}}};
  resolveDependencies(catalog, manual, ResolutionMode::Manual);
  expect(manual.issues.empty() && manual.members.soundBanks == std::vector{AssetId{2}},
         "explicit manual choices may intentionally cross container boundaries");
}

}  // namespace

void runValueAssetResolutionTests() {
  automaticCandidatesRespectContainerBoundaries();
  dependenciesPreserveSharingPlacementsAndPrivatePreparation();
  manualChoicesOverrideSequenceRequestsAndConstrainBankInputs();
  dependencyFailuresAreLocalAndAmbiguityRetainsAlternatives();
  collectionIssuesDeriveImpact();
  matchingKeepsTiesAndRejectsIncompatibleCandidates();
  discoveryExposesTypedAssetDataAndSources();
}
