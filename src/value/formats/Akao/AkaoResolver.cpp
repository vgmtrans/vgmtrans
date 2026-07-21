/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/Akao/Akao.h"
#include "value/scan/CollectionPreparation.h"
#include "value/scan/CollectionResolver.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <map>
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

[[nodiscard]] bool psfLike(const SourceFile* source) {
  if (source == nullptr) {
    return false;
  }
  std::string ext = source->path.extension().string();
  std::ranges::transform(ext, ext.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return ext == ".psf" || ext == ".minipsf" || ext == ".psflib";
}

[[nodiscard]] bool covers(const AkaoSampleCandidate& sample, u32 articulationId) {
  return articulationId >= sample.firstArticulationId &&
         articulationId < sample.firstArticulationId + sample.articulationCount;
}

[[nodiscard]] bool sameSampleSet(std::optional<u32> sequenceSampleSet, std::optional<u32> sampleSet) {
  // Legacy Akao treats missing and zero sample-set ids as the same anonymous set.
  // Several PSF rips omit the ids entirely but still expect the source-local sample
  // collection to attach when instruments do not expose useful articulation refs.
  return sequenceSampleSet.value_or(0) == sampleSet.value_or(0);
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

template <class AssetT>
[[nodiscard]] std::map<u32, u32> idValuesByAsset(const MatchFactIndex& index, std::string_view domain) {
  std::map<u32, u32> values;
  for (const auto& view : index.idFacts<AssetT>(kAkaoFormatName, domain)) {
    values[view.asset.metadata.id.value] = view.payload.value;
  }
  return values;
}

template <class AssetT>
[[nodiscard]] std::map<u32, u64> offsetsByAsset(const MatchFactIndex& index) {
  std::map<u32, u64> offsets;
  for (const auto& view : index.offsetFacts<AssetT>(kAkaoFormatName)) {
    offsets[view.asset.metadata.id.value] = view.payload.offset;
  }
  return offsets;
}

[[nodiscard]] std::vector<SequenceFactEntry> sequenceFacts(const MatchFactIndex& index) {
  std::vector<SequenceFactEntry> entries;
  const auto sampleSets = idValuesByAsset<SequenceProgramAsset>(index, kAkaoSampleSetDomain);
  const auto offsets = offsetsByAsset<SequenceProgramAsset>(index);
  for (const auto& view : index.idFacts<SequenceProgramAsset>(kAkaoFormatName, kAkaoSequenceIdDomain)) {
    const auto assetId = view.asset.metadata.id.value;
    const auto sampleSet = sampleSets.find(assetId);
    const auto offset = offsets.find(assetId);
    entries.push_back(SequenceFactEntry{
        .asset = view.asset.metadata.id,
        .name = view.asset.metadata.name,
        .sourceId = view.fact.scope.source,
        .source = view.source,
        .sequenceId = view.payload.value,
        .sampleSetId = sampleSet != sampleSets.end() ? std::optional<u32>{sampleSet->second} : std::nullopt,
        .offset = offset != offsets.end() ? static_cast<u32>(offset->second) : 0,
    });
  }
  std::ranges::sort(entries, {}, [](const SequenceFactEntry& entry) { return entry.asset.value; });
  return entries;
}

[[nodiscard]] std::vector<SampleFactEntry> sampleFacts(const MatchFactIndex& index) {
  std::vector<SampleFactEntry> entries;
  const auto sampleSets = idValuesByAsset<SampleCollectionAsset>(index, kAkaoSampleSetDomain);
  const auto offsets = offsetsByAsset<SampleCollectionAsset>(index);
  for (const auto& view : index.sampleCoverageFacts<SampleCollectionAsset>(kAkaoFormatName, kAkaoArticulationDomain)) {
    const auto assetId = view.asset.metadata.id.value;
    const auto sampleSet = sampleSets.find(assetId);
    const auto offset = offsets.find(assetId);
    entries.push_back(SampleFactEntry{
        .asset = view.asset.metadata.id,
        .sourceId = view.fact.scope.source,
        .source = view.source,
        .sampleSetId = sampleSet != sampleSets.end() ? std::optional<u32>{sampleSet->second} : std::nullopt,
        .firstArticulationId = view.payload.first,
        .articulationCount = view.payload.count,
        .sourceOffset = offset != offsets.end() ? static_cast<u32>(offset->second) : 0,
    });
  }
  std::ranges::sort(entries, {}, &SampleFactEntry::sourceOffset);
  return entries;
}

[[nodiscard]] std::map<u32, std::set<u32>> requiredArtFacts(const MatchFactIndex& index) {
  std::map<u32, std::set<u32>> requiredBySequence;
  for (const auto& view :
       index.sampleRequirementFacts<SequenceProgramAsset>(kAkaoFormatName, kAkaoArticulationDomain)) {
    auto& required = requiredBySequence[view.asset.metadata.id.value];
    for (const u32 articulation : view.payload.required) {
      if (articulation != 0) {
        required.insert(articulation);
      }
    }
  }
  return requiredBySequence;
}

void markCoveredArticulations(std::set<u32>& remaining, const AkaoSampleCandidate& sample) {
  for (auto it = remaining.begin(); it != remaining.end();) {
    if (covers(sample, *it)) {
      it = remaining.erase(it);
    } else {
      ++it;
    }
  }
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

  std::vector<AkaoSampleCandidate> sampleCandidates;
  sampleCandidates.reserve(candidates.size());
  for (std::size_t i = 0; i < candidates.size(); ++i) {
    const auto& candidate = candidates[i];
    sampleCandidates.push_back(AkaoSampleCandidate{
        .index = i,
        .sampleSetId = candidate.sampleSetId,
        .firstArticulationId = candidate.firstArticulationId,
        .articulationCount = candidate.articulationCount,
        .sourceOffset = candidate.sourceOffset,
    });
  }

  if (sequence.sampleSetId && *sequence.sampleSetId > 0 && !psfLike(sequence.source)) {
    const auto preferred = std::ranges::find_if(sampleCandidates, [&](const AkaoSampleCandidate& sample) {
      return sample.sampleSetId && *sample.sampleSetId == *sequence.sampleSetId;
    });
    if (preferred == sampleCandidates.end()) {
      collection.incomplete(CollectionIssue{
          .severity = Severity::Warning,
          .code = "missing-preferred-sample-set",
          .message = missingSampleMessage(sequence),
          .asset = sequence.asset,
      });
    }
  }

  std::vector<u32> required(remaining.begin(), remaining.end());
  std::vector<SampleFactEntry> selected;
  for (const std::size_t index : selectAkaoSampleCandidates(sequence.sampleSetId, required, sampleCandidates)) {
    selected.push_back(candidates[index]);
    markCoveredArticulations(remaining, sampleCandidates[index]);
  }
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

inline constexpr std::string_view kBoundInstrumentSetSlot = "akao.bound-instrument-set";

[[nodiscard]] Diagnostic materializationWarning(std::string message, std::optional<SourceRange> range = std::nullopt) {
  return Diagnostic{
      .severity = Severity::Warning,
      .message = std::move(message),
      .range = range,
  };
}

[[nodiscard]] std::optional<ScanInput> scanInputForRange(const MaterializationContext& context, SourceRange range) {
  if (!range.valid() || !context.sources.contains(range.source)) {
    return std::nullopt;
  }
  return ScanInput{
      .source = context.sources.source(range.source),
      .reader = context.sources.reader(range.source),
      .ids = context.ids,
  };
}

[[nodiscard]] std::optional<AkaoSampleCollectionParse> parseSampleCollectionForBinding(
    const MaterializationContext& context, const SampleCollectionAsset& sampleCollection,
    std::vector<Diagnostic>& diagnostics) {
  const SourceRange range = sampleCollection.metadata.range;
  auto input = scanInputForRange(context, range);
  if (!input) {
    diagnostics.push_back(
        materializationWarning("Akao materialization could not read selected sample collection source",
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
        materializationWarning("Akao sample collection source offset is outside the supported address range", range));
    return std::nullopt;
  }

  AkaoPs1Version version = determineVersionFromSource(input->source);
  if (version == AkaoPs1Version::Unknown) {
    version = guessSampleVersion(input->reader, static_cast<u32>(range.offset));
  }
  return parseAkaoSampleCollectionData(*input, ref, static_cast<u32>(range.offset), version);
}

[[nodiscard]] AkaoArticulationMap buildResolvedArticulations(const MaterializationContext& context,
                                                             std::vector<Diagnostic>& diagnostics) {
  AkaoArticulationMap articulations;
  for (const AssetId sampleId : context.collection.sampleCollections) {
    const auto* sampleCollection = context.snapshot.asset<SampleCollectionAsset>(sampleId);
    if (sampleCollection == nullptr) {
      continue;
    }
    auto parsed = parseSampleCollectionForBinding(context, *sampleCollection, diagnostics);
    if (!parsed) {
      diagnostics.push_back(materializationWarning("Akao materialization could not parse selected sample collection",
                                                   sampleCollection->metadata.range));
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

std::vector<std::size_t> selectAkaoSampleCandidates(std::optional<u32> sequenceSampleSet,
                                                    std::span<const u32> requiredArticulations,
                                                    std::span<const AkaoSampleCandidate> candidates) {
  std::vector<AkaoSampleCandidate> ordered(candidates.begin(), candidates.end());
  std::ranges::sort(ordered, std::ranges::greater{}, &AkaoSampleCandidate::sourceOffset);

  std::set<u32> remaining(requiredArticulations.begin(), requiredArticulations.end());
  std::vector<AkaoSampleCandidate> selected;
  if (sequenceSampleSet && *sequenceSampleSet > 0) {
    const auto preferred = std::ranges::find_if(ordered, [&](const AkaoSampleCandidate& sample) {
      return sample.sampleSetId && *sample.sampleSetId == *sequenceSampleSet;
    });
    if (preferred != ordered.end()) {
      selected.push_back(*preferred);
      markCoveredArticulations(remaining, *preferred);
    }
  }

  for (const auto& sample : ordered) {
    if (std::ranges::find(selected, sample.index, &AkaoSampleCandidate::index) != selected.end()) {
      continue;
    }
    const bool associated = sameSampleSet(sequenceSampleSet, sample.sampleSetId);
    const bool matchesRequired =
        std::ranges::any_of(remaining, [&](u32 articulation) { return covers(sample, articulation); });
    if (!associated && !matchesRequired) {
      continue;
    }
    selected.push_back(sample);
    markCoveredArticulations(remaining, sample);
    if (remaining.empty() && !selected.empty()) {
      break;
    }
  }

  std::ranges::sort(selected, {}, &AkaoSampleCandidate::firstArticulationId);

  std::vector<std::size_t> indexes;
  indexes.reserve(selected.size());
  for (const auto& sample : selected) {
    indexes.push_back(sample.index);
  }
  return indexes;
}

std::vector<DesiredCollection> resolveAkaoCollections(const MatchContext& context) {
  const MatchFactIndex index(context);
  const auto sequences = sequenceFacts(index);
  const auto samples = sampleFacts(index);
  const auto requiredBySequence = requiredArtFacts(index);

  std::vector<DesiredCollection> collections;
  for (const auto& sequence : sequences) {
    CollectionAssembly collection(collectionKey(sequence), sequence.name.empty() ? "Akao Collection" : sequence.name);
    collection.sequence(sequence.asset);

    std::set<u32> remaining;
    if (auto found = requiredBySequence.find(sequence.asset.value); found != requiredBySequence.end()) {
      remaining = found->second;
    }

    const auto selected = chooseSamplesForSequence(sequence, samples, remaining, collection);
    attachSamplesAndReportGaps(collection, sequence, selected, remaining);
    collection.requireSequence().requireSampleCollection();
    collections.push_back(std::move(collection).finish());
  }
  return collections;
}

MaterializationResult materializeAkaoCollection(const MaterializationContext& context) {
  if (!context.collection.sequence || context.collection.sampleCollections.empty()) {
    return MaterializationResult{.collection = context.collection};
  }

  const auto* sequence = context.snapshot.asset<SequenceProgramAsset>(*context.collection.sequence);
  if (sequence == nullptr) {
    return MaterializationResult{.collection = context.collection};
  }

  CollectionPreparation prepared(context);
  const SourceRange sequenceRange = sequence->metadata.range;
  auto input = scanInputForRange(context, sequenceRange);
  if (!input) {
    return prepared.incomplete("Akao materialization could not read sequence source",
                               sequenceRange.valid() ? std::optional<SourceRange>{sequenceRange} : std::nullopt);
  }
  if (sequenceRange.offset > std::numeric_limits<u32>::max()) {
    return prepared.incomplete("Akao sequence source offset is outside the supported address range", sequenceRange);
  }

  auto analysis = analyzeAkaoSequence(*input, *sequence);
  if (!analysis) {
    return prepared.incomplete("Akao materialization could not re-analyze sequence", sequenceRange);
  }

  // Akao instruments cannot be finalized during the initial scan: the chosen
  // collection may combine several sample sets, and each articulation must
  // point into the set that actually supplied it. Build that lookup only after
  // collection matching has made the selection.
  auto articulations = buildResolvedArticulations(context, prepared.diagnostics());
  if (articulations.empty()) {
    return prepared.incomplete("Akao materialization produced no articulation bindings", sequenceRange);
  }

  auto instruments = prepared.instruments(kBoundInstrumentSetSlot);
  buildAkaoInstrumentSet(*input, *analysis, articulations, instruments);
  prepared.replaceInstrumentSet(akaoInstrumentSetName(*analysis), std::move(instruments));
  return std::move(prepared).finish();
}

}  // namespace vgmtrans::formats::akao
