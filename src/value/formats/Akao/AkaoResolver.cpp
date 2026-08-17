/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/Akao/Akao.h"
#include "value/scan/CollectionResolver.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
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
           ", but no matching sample pool was found";
  }
  return "Akao sequence has no matching sample pool";
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
  for (const auto& facts : index.assets<SamplePoolAsset>(kAkaoFormatName)) {
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

[[nodiscard]] std::vector<InstrumentSetFactEntry> soundBankFacts(const MatchFactIndex& index) {
  std::vector<InstrumentSetFactEntry> entries;
  for (const auto& facts : index.assets<SoundBankAsset>(kAkaoFormatName)) {
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
    collection.samplePool(sample.asset);
  }
  if (selected.empty()) {
    collection.incomplete(CollectionIssue{
        .severity = Severity::Warning,
        .code = "missing-sample-collection",
        .message = missingSampleMessage(sequence),
        .asset = sequence.asset,
    });
  } else if (!remaining.empty()) {
    std::string message = "Akao sample pools do not cover required articulation ids:";
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

[[nodiscard]] AkaoArticulationMap selectedArticulations(const CollectionBindingContext& context) {
  AkaoArticulationMap articulations;
  for (const auto* samplePool : context.samplePools) {
    const auto* data = samplePool->privateData.get<AkaoSampleBindingData>();
    if (data == nullptr) {
      continue;
    }
    for (const auto& articulation : *data) {
      articulations[articulation.articulationId] = AkaoArticulationBinding{
          .collection = ScanSamplePoolRef{.id = samplePool->metadata.id},
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
  const auto soundBanks = soundBankFacts(index);
  const auto samples = sampleFacts(index);

  std::vector<DesiredCollection> collections;
  for (const auto& sequence : sequences) {
    CollectionAssembly collection(collectionKey(sequence), sequence.name.empty() ? "Akao Collection" : sequence.name);
    collection.sequence(sequence.asset);
    const auto soundBank = std::ranges::find(soundBanks, sequence.asset, &InstrumentSetFactEntry::sequenceAsset);
    if (soundBank != soundBanks.end()) {
      collection.soundBank(soundBank->asset);
    } else {
      collection.incomplete(CollectionIssue{
          .severity = Severity::Warning,
          .code = "missing-instrument-set",
          .message = "Akao sequence has no detected sound bank",
          .asset = sequence.asset,
      });
    }

    std::set<u32> remaining(sequence.requiredArticulations.begin(), sequence.requiredArticulations.end());

    const auto selected = chooseSamplesForSequence(sequence, samples, remaining, collection);
    attachSamplesAndReportGaps(collection, sequence, selected, remaining);
    collection.requireSequence().requireSoundBank().requireSamplePool();
    collections.push_back(std::move(collection).finish());
  }
  return collections;
}

void bindAkaoCollection(CollectionBindingContext& context) {
  const auto* sequence = context.sequence;
  if (sequence == nullptr) {
    return;
  }
  const auto* sequenceData = sequence->privateData.get<AkaoSequenceBindingData>();
  if (sequenceData == nullptr) {
    context.fail("Akao sequence is missing retained collection-binding data", sequence->metadata.range);
    return;
  }
  auto* instruments = context.soundBank(sequenceData->structuralInstrumentSet);
  if (instruments == nullptr) {
    context.fail("Akao collection does not contain the sequence's structural sound bank", sequence->metadata.range);
    return;
  }
  const auto* instrumentData = instruments->privateData.get<AkaoInstrumentSetBindingData>();
  if (instrumentData == nullptr) {
    context.fail("Akao sound bank is missing retained collection-binding data", instruments->metadata.range);
    return;
  }

  for (const auto* samples : context.samplePools) {
    if (samples->metadata.format == kAkaoFormatName && samples->privateData.get<AkaoSampleBindingData>() == nullptr) {
      context.fail("Akao sample pool is missing retained collection-binding data", samples->metadata.range);
      return;
    }
  }

  const auto articulations = selectedArticulations(context);
  if (!applyAkaoArticulations(*instruments, *instrumentData, articulations)) {
    context.fail("Akao retained instrument recipe does not match its structural bank", instruments->metadata.range);
    return;
  }

  std::set<u32> missing;
  for (const auto& regions : instrumentData->regions) {
    for (const auto& region : regions) {
      if (region.articulationId != 0 && !articulations.contains(region.articulationId)) {
        missing.insert(region.articulationId);
      }
    }
  }
  if (!missing.empty()) {
    std::string message = "Akao collection does not provide required articulations:";
    for (const u32 articulation : missing) {
      message += " " + std::to_string(articulation);
    }
    context.warning(std::move(message), sequence->metadata.range);
  }
}

}  // namespace vgmtrans::formats::akao
