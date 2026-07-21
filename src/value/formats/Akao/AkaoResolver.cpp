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
        .offset = static_cast<u32>(facts.offset().value_or(0)),
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
        .sourceOffset = static_cast<u32>(facts.offset().value_or(0)),
    });
  }
  std::ranges::sort(entries, {}, &SampleFactEntry::sourceOffset);
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

  std::vector<SampleCoverageProvider> providers;
  providers.reserve(candidates.size());
  for (std::size_t i = 0; i < candidates.size(); ++i) {
    const auto& candidate = candidates[i];
    providers.push_back(SampleCoverageProvider{
        .index = i,
        .groupId = candidate.sampleSetId,
        .first = candidate.firstArticulationId,
        .count = candidate.articulationCount,
        .priority = candidate.sourceOffset,
    });
  }

  std::vector<u32> required(remaining.begin(), remaining.end());
  const auto selection = selectSampleCoverage(sequence.sampleSetId, required, providers);
  if (sequence.sampleSetId && *sequence.sampleSetId > 0 && !psfLike(sequence.source) &&
      !selection.preferredGroupFound) {
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

inline constexpr std::string_view kBoundInstrumentSetSlot = "akao.bound-instrument-set";

[[nodiscard]] Diagnostic materializationWarning(std::string message, std::optional<SourceRange> range = std::nullopt) {
  return Diagnostic{
      .severity = Severity::Warning,
      .message = std::move(message),
      .range = range,
  };
}

[[nodiscard]] std::optional<AkaoSampleCollectionParse> parseSampleCollectionForBinding(
    const MaterializationContext& context, const SampleCollectionAsset& sampleCollection,
    std::vector<Diagnostic>& diagnostics) {
  const SourceRange range = sampleCollection.metadata.range;
  auto input = context.inputFor(range);
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
  for (const auto* sampleCollection : context.selectedSampleCollections()) {
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

std::vector<DesiredCollection> resolveAkaoCollections(const MatchContext& context) {
  const MatchFactIndex index(context);
  const auto sequences = sequenceFacts(index);
  const auto samples = sampleFacts(index);

  std::vector<DesiredCollection> collections;
  for (const auto& sequence : sequences) {
    CollectionAssembly collection(collectionKey(sequence), sequence.name.empty() ? "Akao Collection" : sequence.name);
    collection.sequence(sequence.asset);

    std::set<u32> remaining(sequence.requiredArticulations.begin(), sequence.requiredArticulations.end());

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

  const auto* sequence = context.sequenceAsset();
  if (sequence == nullptr) {
    return MaterializationResult{.collection = context.collection};
  }

  CollectionPreparation prepared(context);
  const SourceRange sequenceRange = sequence->metadata.range;
  auto input = context.inputFor(sequenceRange);
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
