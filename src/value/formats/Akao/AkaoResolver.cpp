/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/Akao/Akao.h"
#include "value/scan/CollectionDiscovery.h"

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

using SequenceEntry = AssetWithData<SequenceProgramAsset, AkaoSequenceData>;
using SampleEntry = AssetWithData<SamplePoolAsset, AkaoSamplePoolData>;

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

[[nodiscard]] std::string collectionKey(const SequenceEntry& sequence) {
  return "seq:" + std::to_string(sequence.data->sequenceId) +
         ":source:" + std::to_string(sequence.sourceId() ? sequence.sourceId()->value : 0) +
         ":offset:" + std::to_string(sequence.asset->metadata.range.offset);
}

[[nodiscard]] std::string missingSampleMessage(const SequenceEntry& sequence) {
  if (sequence.data->sampleSetId) {
    return "Akao sequence references sample set " + std::to_string(*sequence.data->sampleSetId) +
           ", but no matching sample pool was found";
  }
  return "Akao sequence has no matching sample pool";
}

std::vector<SampleEntry> chooseSamplesForSequence(const SequenceEntry& sequence,
                                                  const std::vector<SampleEntry>& samples, std::set<u32>& remaining,
                                                  CollectionAssembly& collection) {
  std::vector<SampleEntry> candidates;
  const bool isolated = psfLike(sequence.source);
  const auto sequenceSource = sequence.sourceId();
  for (const auto& sample : samples) {
    const bool sameSource = sequenceSource && sample.sourceId() && sequenceSource == sample.sourceId();
    if (!isolated || sameSource) {
      candidates.push_back(sample);
    }
  }
  std::ranges::sort(candidates, [&](const SampleEntry& left, const SampleEntry& right) {
    const bool leftLocal = sequenceSource && left.sourceId() == sequenceSource;
    const bool rightLocal = sequenceSource && right.sourceId() == sequenceSource;
    return leftLocal != rightLocal ? leftLocal : left.id().value > right.id().value;
  });

  std::vector<AkaoSampleCoverageProvider> providers;
  providers.reserve(candidates.size());
  for (std::size_t i = 0; i < candidates.size(); ++i) {
    const auto& candidate = candidates[i];
    providers.push_back(AkaoSampleCoverageProvider{
        .index = i,
        .sampleSetId = candidate.data->sampleSetId,
        .first = candidate.data->firstArticulationId,
        .count = candidate.data->articulationCount,
    });
  }

  const std::vector<u32> required(remaining.begin(), remaining.end());
  const auto selection = selectAkaoSampleCoverage(sequence.data->sampleSetId, required, providers);

  if (sequence.data->sampleSetId && *sequence.data->sampleSetId > 0 && !isolated &&
      !selection.requestedSampleSetFound) {
    collection.incomplete(CollectionIssue{
        .severity = Severity::Warning,
        .code = "missing-preferred-sample-set",
        .message = missingSampleMessage(sequence),
        .asset = sequence.id(),
    });
  }

  std::vector<SampleEntry> selected;
  for (const std::size_t selectedIndex : selection.providers) {
    selected.push_back(candidates[selectedIndex]);
  }
  remaining = std::set<u32>(selection.missing.begin(), selection.missing.end());
  return selected;
}

void attachSamplesAndReportGaps(CollectionAssembly& collection, const SequenceEntry& sequence,
                                const std::vector<SampleEntry>& selected, const std::set<u32>& remaining) {
  for (const auto& sample : selected) {
    collection.samplePool(sample.id());
  }
  if (selected.empty()) {
    collection.incomplete(CollectionIssue{
        .severity = Severity::Warning,
        .code = "missing-sample-collection",
        .message = missingSampleMessage(sequence),
        .asset = sequence.id(),
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
        .asset = sequence.id(),
    });
  }
}

[[nodiscard]] AkaoArticulationMap selectedArticulations(const CollectionBindingContext& context) {
  AkaoArticulationMap articulations;
  for (const auto* samplePool : context.samplePools) {
    const auto* data = samplePool->privateData.get<AkaoSamplePoolData>();
    if (data == nullptr) {
      continue;
    }
    for (const auto& articulation : data->articulations) {
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
  // ID, first select the first matching collection. Providers arrive in
  // matching priority order: collections from the sequence's source first,
  // then most recently discovered collections. Continue in that order when
  // selecting collections that supply required articulations. Return any
  // required articulation IDs that remain uncovered so the resolver can
  // explain an incomplete match.
  const auto& ordered = providers;

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
    if ((!sameSampleSet || !selected.empty()) && !contributes) {
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

std::vector<DesiredCollection> resolveAkaoCollections(const CollectionDiscoveryContext& context) {
  const auto sequences = context.assetsWithData<SequenceProgramAsset, AkaoSequenceData>();
  const auto samples = context.assetsWithData<SamplePoolAsset, AkaoSamplePoolData>();

  std::vector<DesiredCollection> collections;
  for (const auto& sequence : sequences) {
    CollectionAssembly collection(collectionKey(sequence), sequence.asset->metadata.name.empty()
                                                               ? "Akao Collection"
                                                               : sequence.asset->metadata.name);
    collection.sequence(sequence.id());
    const auto* soundBank = context.asset<SoundBankAsset>(sequence.data->structuralInstrumentSet);
    if (soundBank != nullptr && soundBank->metadata.format == kAkaoFormatName) {
      collection.soundBank(soundBank->metadata.id);
    } else {
      collection.incomplete(CollectionIssue{
          .severity = Severity::Warning,
          .code = "missing-instrument-set",
          .message = "Akao sequence has no detected sound bank",
          .asset = sequence.id(),
      });
    }

    std::set<u32> remaining(sequence.data->requiredArticulations.begin(), sequence.data->requiredArticulations.end());

    const auto selected = chooseSamplesForSequence(sequence, samples, remaining, collection);
    attachSamplesAndReportGaps(collection, sequence, selected, remaining);
    collections.push_back(std::move(collection).finish());
  }
  return collections;
}

void bindAkaoCollection(CollectionBindingContext& context) {
  const auto* sequence = context.sequence;
  if (sequence == nullptr) {
    return;
  }
  const auto* sequenceData = sequence->privateData.get<AkaoSequenceData>();
  if (sequenceData == nullptr) {
    context.fail("Akao sequence is missing retained collection-binding data", sequence->metadata.range);
    return;
  }
  auto* instruments = context.soundBank(sequenceData->structuralInstrumentSet);
  if (instruments == nullptr) {
    context.fail("Akao collection does not contain the sequence's structural sound bank", sequence->metadata.range);
    return;
  }
  const auto* soundBankData = instruments->privateData.get<AkaoSoundBankData>();
  const auto* instrumentData = soundBankData != nullptr ? &soundBankData->binding : nullptr;
  if (instrumentData == nullptr) {
    context.fail("Akao sound bank is missing retained collection-binding data", instruments->metadata.range);
    return;
  }

  for (const auto* samples : context.samplePools) {
    if (samples->metadata.format == kAkaoFormatName && samples->privateData.get<AkaoSamplePoolData>() == nullptr) {
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
