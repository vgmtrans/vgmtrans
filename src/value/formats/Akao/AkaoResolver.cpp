/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/Akao/Akao.h"
#include "value/scan/CollectionResolver.h"

#include <algorithm>
#include <cstddef>
#include <cctype>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace vgmtrans::formats::akao {

using namespace core;

namespace {

struct SequenceFactEntry {
  AssetId asset;
  std::string name;
  std::optional<SourceId> sourceId;
  const SourceFile* source = nullptr;
  u32 sequenceId = 0;
  std::optional<u32> sampleSetId;
  u32 offset = 0;
  std::vector<u32> requiredArticulations;
};

struct SampleFactEntry {
  AssetId asset;
  std::optional<SourceId> sourceId;
  const SourceFile* source = nullptr;
  std::optional<u32> sampleSetId;
  u32 firstArticulationId = 0;
  u32 articulationCount = 0;
  u32 sourceOffset = 0;
};

struct InstrumentSetFactEntry {
  AssetId asset;
  AssetId sequenceAsset;
};

[[nodiscard]] bool covers(const AkaoSampleCoverageProvider& provider, u32 value) {
  return value >= provider.first && static_cast<u64>(value) < static_cast<u64>(provider.first) + provider.count;
}

void markCovered(std::set<u32>& remaining, const AkaoSampleCoverageProvider& provider) {
  for (auto it = remaining.begin(); it != remaining.end();) {
    if (covers(provider, *it)) {
      it = remaining.erase(it);
    } else {
      ++it;
    }
  }
}

[[nodiscard]] bool psfLike(const SourceFile* source) {
  if (source == nullptr) {
    return false;
  }
  std::string ext = source->path.extension().string();
  std::ranges::transform(ext, ext.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return ext == ".psf" || ext == ".minipsf" || ext == ".psflib";
}

[[nodiscard]] CollectionKey collectionKey(const SequenceFactEntry& sequence) {
  return CollectionKey{
      .resolver = std::string(kAkaoCollectionResolver),
      .value = "seq:" + std::to_string(sequence.sequenceId) +
               ":source:" + std::to_string(sequence.sourceId ? sequence.sourceId->value : 0) +
               ":offset:" + std::to_string(sequence.offset),
  };
}

[[nodiscard]] std::string missingSampleMessage(const SequenceFactEntry& sequence) {
  if (sequence.sampleSetId) {
    return "Akao sequence references sample set " + std::to_string(*sequence.sampleSetId) +
           ", but no matching sample collection was found";
  }
  return "Akao sequence has no matching sample collection";
}

[[nodiscard]] std::vector<SequenceFactEntry> sequenceFacts(const MatchFactIndex& index) {
  std::vector<SequenceFactEntry> entries;
  for (const auto& facts : index.assets<SequenceProgramAsset>(kAkaoFormatName)) {
    const auto sequenceId = facts.id(kAkaoSequenceIdDomain);
    if (!sequenceId) {
      continue;
    }
    auto required = facts.requirements(kAkaoArticulationDomain);
    std::erase(required, 0);
    entries.push_back(SequenceFactEntry{
        .asset = facts.asset().metadata.id,
        .name = facts.asset().metadata.name,
        .sourceId = facts.sourceId,
        .source = facts.source,
        .sequenceId = *sequenceId,
        .sampleSetId = facts.id(kAkaoSampleSetDomain),
        .offset = static_cast<u32>(facts.asset().metadata.range.offset),
        .requiredArticulations = std::move(required),
    });
  }
  return entries;
}

[[nodiscard]] std::vector<SampleFactEntry> sampleFacts(const MatchFactIndex& index) {
  std::vector<SampleFactEntry> entries;
  for (const auto& facts : index.assets<SampleCollectionAsset>(kAkaoFormatName)) {
    const auto coverage = facts.coverage(kAkaoArticulationDomain);
    if (!coverage) {
      continue;
    }
    entries.push_back(SampleFactEntry{
        .asset = facts.asset().metadata.id,
        .sourceId = facts.sourceId,
        .source = facts.source,
        .sampleSetId = facts.id(kAkaoSampleSetDomain),
        .firstArticulationId = coverage->first,
        .articulationCount = coverage->count,
        .sourceOffset = static_cast<u32>(facts.asset().metadata.range.offset),
    });
  }
  std::ranges::sort(entries, {}, &SampleFactEntry::sourceOffset);
  return entries;
}

[[nodiscard]] std::vector<InstrumentSetFactEntry> instrumentSetFacts(const MatchFactIndex& index) {
  std::vector<InstrumentSetFactEntry> entries;
  for (const auto& facts : index.assets<InstrumentSetAsset>(kAkaoFormatName)) {
    const auto sequenceAsset = facts.relation(kAkaoInstrumentSequenceDomain);
    if (!sequenceAsset) {
      continue;
    }
    entries.push_back(InstrumentSetFactEntry{
        .asset = facts.asset().metadata.id,
        .sequenceAsset = *sequenceAsset,
    });
  }
  return entries;
}

std::vector<SampleFactEntry> chooseSamplesForSequence(const SequenceFactEntry& sequence,
                                                      const std::vector<SampleFactEntry>& samples,
                                                      std::set<u32>& remaining, CollectionAssembly& collection) {
  std::vector<SampleFactEntry> candidates;
  const bool isolated = psfLike(sequence.source);
  for (const auto& sample : samples) {
    const bool sameSource = sequence.sourceId && sample.sourceId && *sequence.sourceId == *sample.sourceId;
    if (!isolated || sameSource) {
      candidates.push_back(sample);
    }
  }
  std::ranges::sort(candidates, std::ranges::greater{}, &SampleFactEntry::sourceOffset);

  std::vector<AkaoSampleCoverageProvider> providers;
  providers.reserve(candidates.size());
  for (std::size_t i = 0; i < candidates.size(); ++i) {
    const auto& candidate = candidates[i];
    providers.push_back(AkaoSampleCoverageProvider{
        .index = i,
        .sampleSetId = candidate.sampleSetId,
        .first = candidate.firstArticulationId,
        .count = candidate.articulationCount,
        .sourceOffset = candidate.sourceOffset,
    });
  }

  std::vector<u32> required(remaining.begin(), remaining.end());
  const auto selection = selectAkaoSampleCoverage(sequence.sampleSetId, required, providers);
  if (sequence.sampleSetId && *sequence.sampleSetId > 0 && !psfLike(sequence.source) &&
      !selection.requestedSampleSetFound) {
    collection.incomplete(CollectionIssue{
        .severity = Severity::Warning,
        .code = "missing-preferred-sample-set",
        .message = missingSampleMessage(sequence),
        .asset = sequence.asset,
    });
  }

  std::vector<SampleFactEntry> selected;
  for (const std::size_t selectedIndex : selection.providers) {
    selected.push_back(candidates[selectedIndex]);
  }
  remaining = std::set<u32>(selection.missing.begin(), selection.missing.end());
  return selected;
}

void attachSamplesAndReportGaps(CollectionAssembly& collection, const SequenceFactEntry& sequence,
                                const std::vector<SampleFactEntry>& selected, const std::set<u32>& remaining) {
  for (const auto& sample : selected) {
    collection.sampleCollection(sample.asset);
  }
  if (selected.empty()) {
    collection.incomplete(CollectionIssue{
        .severity = Severity::Warning,
        .code = "missing-sample-collection",
        .message = missingSampleMessage(sequence),
        .asset = sequence.asset,
    });
  } else if (!remaining.empty()) {
    std::string message = "Akao sample collections do not cover required articulation ids:";
    for (const u32 articulation : remaining) {
      message += " " + std::to_string(articulation);
    }
    collection.incomplete(CollectionIssue{
        .severity = Severity::Warning,
        .code = "missing-articulation-coverage",
        .message = std::move(message),
        .asset = sequence.asset,
    });
  }
}

[[nodiscard]] Diagnostic bindingWarning(std::string message, std::optional<SourceRange> range = std::nullopt) {
  return Diagnostic{
      .severity = Severity::Warning,
      .message = std::move(message),
      .range = range,
  };
}

[[nodiscard]] std::optional<ScanInput> inputFor(const CollectionBindingContext& context, SourceRange range,
                                                ScanIdAllocator& ids) {
  if (!range.valid() || !context.sources.contains(range.source)) {
    return std::nullopt;
  }
  return ScanInput{
      .source = context.sources.source(range.source),
      .reader = context.sources.reader(range.source),
      .ids = ids,
  };
}

[[nodiscard]] std::optional<AkaoSampleCollectionParse> parseSampleCollectionForBinding(
    const CollectionBindingContext& context, ScanIdAllocator& ids, const SampleCollectionAsset& sampleCollection,
    std::vector<Diagnostic>& diagnostics) {
  const SourceRange range = sampleCollection.metadata.range;
  auto input = inputFor(context, range, ids);
  if (!input) {
    diagnostics.push_back(bindingWarning("Akao binding could not read selected sample collection source",
                                         range.valid() ? std::optional<SourceRange>{range} : std::nullopt));
    return std::nullopt;
  }

  const ScanSampleCollectionRef ref{.id = sampleCollection.metadata.id};
  if (const auto hardcoded = ff7HardcodedAkaoSampleLocation(input->reader)) {
    const u32 hardcodedOffset = std::min(hardcoded->sampleHeaderOffset, hardcoded->articulationTableOffset);
    if (range.offset == hardcodedOffset) {
      return parseAkaoSampleCollectionData(*input, ref, *hardcoded);
    }
  }

  if (range.offset > std::numeric_limits<u32>::max()) {
    diagnostics.push_back(
        bindingWarning("Akao sample collection source offset is outside the supported address range", range));
    return std::nullopt;
  }

  AkaoPs1Version version = determineVersionFromSource(input->source);
  if (version == AkaoPs1Version::Unknown) {
    version = guessSampleVersion(input->reader, static_cast<u32>(range.offset));
  }
  return parseAkaoSampleCollectionData(*input, ref, static_cast<u32>(range.offset), version);
}

[[nodiscard]] AkaoArticulationMap buildResolvedArticulations(const CollectionBindingContext& context,
                                                             ScanIdAllocator& ids,
                                                             std::vector<Diagnostic>& diagnostics) {
  AkaoArticulationMap articulations;
  for (const auto* sampleCollection : context.sampleCollections) {
    auto parsed = parseSampleCollectionForBinding(context, ids, *sampleCollection, diagnostics);
    if (!parsed) {
      diagnostics.push_back(
          bindingWarning("Akao binding could not parse selected sample collection", sampleCollection->metadata.range));
      continue;
    }
    for (const auto& articulation : parsed->articulations) {
      articulations[articulation.articulationId] = AkaoArticulationBinding{
          .collection = ScanSampleCollectionRef{.id = sampleCollection->metadata.id},
          .sampleIndex = articulation.sampleIndex,
          .articulation = articulation,
      };
    }
  }
  return articulations;
}

}  // namespace

AkaoSampleCoverageSelection selectAkaoSampleCoverage(std::optional<u32> requestedSampleSetId,
                                                     const std::vector<u32>& required,
                                                     const std::vector<AkaoSampleCoverageProvider>& providers) {
  // An Akao sequence refers to articulation IDs, while each discovered sample
  // collection supplies only a range of those IDs. A sequence may therefore
  // need several collections. If the sequence header specifies a sample-set
  // ID, first select the matching collection with the greatest source offset.
  // Then examine the remaining collections in order from greatest to smallest
  // source offset. Select one when its sample-set ID matches the ID from the
  // sequence header or when it supplies an articulation the sequence still
  // needs. Return any required articulation IDs that remain uncovered so the
  // resolver can explain an incomplete match.
  std::vector<AkaoSampleCoverageProvider> ordered = providers;
  std::ranges::sort(ordered, std::ranges::greater{}, &AkaoSampleCoverageProvider::sourceOffset);

  std::set<u32> remaining(required.begin(), required.end());
  std::vector<AkaoSampleCoverageProvider> selected;
  bool requestedSampleSetFound = false;

  // First honor the sequence's explicit sample-set ID, when it has one.
  if (requestedSampleSetId && *requestedSampleSetId > 0) {
    const auto preferred = std::ranges::find_if(ordered, [&](const AkaoSampleCoverageProvider& provider) {
      return provider.sampleSetId && *provider.sampleSetId == *requestedSampleSetId;
    });
    if (preferred != ordered.end()) {
      requestedSampleSetFound = true;
      selected.push_back(*preferred);
      markCovered(remaining, *preferred);
    }
  }

  // A missing or zero sample-set ID denotes Akao's unnamed sample set.
  for (const auto& provider : ordered) {
    if (std::ranges::find(selected, provider.index, &AkaoSampleCoverageProvider::index) != selected.end()) {
      continue;
    }
    const bool sameSampleSet = requestedSampleSetId.value_or(0) == provider.sampleSetId.value_or(0);
    const bool contributes = std::ranges::any_of(remaining, [&](u32 value) { return covers(provider, value); });
    if (!sameSampleSet && !contributes) {
      continue;
    }
    selected.push_back(provider);
    markCovered(remaining, provider);
    if (remaining.empty()) {
      break;
    }
  }

  // Order the chosen collections by the articulation ranges they supply rather
  // than by source offset, giving later sample binding a stable order.
  std::ranges::sort(selected, {}, &AkaoSampleCoverageProvider::first);
  AkaoSampleCoverageSelection result{.requestedSampleSetFound = requestedSampleSetFound};
  result.providers.reserve(selected.size());
  for (const auto& provider : selected) {
    result.providers.push_back(provider.index);
  }
  result.missing.assign(remaining.begin(), remaining.end());
  return result;
}

std::vector<DesiredCollection> resolveAkaoCollections(const MatchContext& context) {
  const MatchFactIndex index(context);
  const auto sequences = sequenceFacts(index);
  const auto instrumentSets = instrumentSetFacts(index);
  const auto samples = sampleFacts(index);

  std::vector<DesiredCollection> collections;
  for (const auto& sequence : sequences) {
    CollectionAssembly collection(collectionKey(sequence), sequence.name.empty() ? "Akao Collection" : sequence.name);
    collection.sequence(sequence.asset);
    const auto instrumentSet =
        std::ranges::find(instrumentSets, sequence.asset, &InstrumentSetFactEntry::sequenceAsset);
    if (instrumentSet != instrumentSets.end()) {
      collection.instrumentSet(instrumentSet->asset);
    } else {
      collection.incomplete(CollectionIssue{
          .severity = Severity::Warning,
          .code = "missing-instrument-set",
          .message = "Akao sequence has no detected instrument set",
          .asset = sequence.asset,
      });
    }

    std::set<u32> remaining(sequence.requiredArticulations.begin(), sequence.requiredArticulations.end());

    const auto selected = chooseSamplesForSequence(sequence, samples, remaining, collection);
    attachSamplesAndReportGaps(collection, sequence, selected, remaining);
    collection.requireSequence().requireInstrumentSet().requireSampleCollection();
    collections.push_back(std::move(collection).finish());
  }
  return collections;
}

void bindAkaoCollection(CollectionBindingContext& context) {
  if (context.sequence == nullptr || context.instrumentSets.empty() || context.sampleCollections.empty()) {
    return;
  }

  const auto& sequence = *context.sequence;
  const std::string instrumentSetName = context.instrumentSets.front().metadata.name;

  ScanIdAllocator ids;
  const SourceRange sequenceRange = sequence.metadata.range;
  auto input = inputFor(context, sequenceRange, ids);
  if (!input) {
    context.diagnostics.push_back(
        bindingWarning("Akao binding could not read sequence source",
                       sequenceRange.valid() ? std::optional<SourceRange>{sequenceRange} : std::nullopt));
    return;
  }
  if (sequenceRange.offset > std::numeric_limits<u32>::max()) {
    context.diagnostics.push_back(
        bindingWarning("Akao sequence source offset is outside the supported address range", sequenceRange));
    return;
  }

  auto analysis = analyzeAkaoSequence(*input, sequence);
  if (!analysis) {
    context.diagnostics.push_back(bindingWarning("Akao binding could not re-analyze sequence", sequenceRange));
    return;
  }

  // The scanned bank owns the durable layout and source annotations. Rebuild
  // the same values here only to bind their articulation ids to the sample
  // collections selected for this particular collection.
  auto articulations = buildResolvedArticulations(context, ids, context.diagnostics);
  if (articulations.empty()) {
    context.diagnostics.push_back(bindingWarning("Akao binding produced no articulation bindings", sequenceRange));
    return;
  }

  InstrumentSetBuilder instruments(AssetId{}, nullptr, &context.diagnostics);
  (void)buildAkaoInstrumentSet(*input, *analysis, articulations, instruments);
  auto built = std::move(instruments).finish();
  context.instrumentSets = {
      InstrumentSetAsset{
          .metadata =
              AssetMetadata{
                  .format = std::string(kAkaoFormatName),
                  .name = instrumentSetName,
                  .range = built.range,
              },
          .instruments = std::move(built.values),
      },
  };
}

}  // namespace vgmtrans::formats::akao
