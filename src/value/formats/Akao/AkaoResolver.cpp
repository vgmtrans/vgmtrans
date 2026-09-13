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

void markCovered(std::set<u32>& remaining, const SampleEntry& sample) {
  for (const auto& articulation : sample.data->articulations) {
    if (articulation.sample.valid()) {
      remaining.erase(articulation.articulationId);
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
         ":source:" + std::to_string(sequence.sourceId().valid() ? sequence.sourceId().value : 0) +
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
    const bool sameSource = sequenceSource.valid() && sequenceSource == sample.sourceId();
    if (!isolated || sameSource) {
      candidates.push_back(sample);
    }
  }
  std::ranges::sort(candidates, [&](const SampleEntry& left, const SampleEntry& right) {
    const bool leftLocal = sequenceSource.valid() && left.sourceId() == sequenceSource;
    const bool rightLocal = sequenceSource.valid() && right.sourceId() == sequenceSource;
    return leftLocal != rightLocal ? leftLocal : left.id().value > right.id().value;
  });

  std::vector<SampleEntry> selected;
  const auto select = [&](const SampleEntry& sample) {
    selected.push_back(sample);
    markCovered(remaining, sample);
  };
  const auto requestedSampleSetId = sequence.data->sampleSetId;
  if (requestedSampleSetId && *requestedSampleSetId > 0) {
    const auto preferred = std::ranges::find_if(
        candidates, [&](const SampleEntry& sample) { return sample.data->sampleSetId == requestedSampleSetId; });
    if (preferred != candidates.end()) {
      select(*preferred);
    } else if (!isolated) {
      collection.incomplete(CollectionIssue{
          .severity = Severity::Warning,
          .code = "missing-preferred-sample-set",
          .message = missingSampleMessage(sequence),
          .asset = sequence.id(),
      });
    }
  }

  // Honor the preferred set, then fill gaps with actual playable articulations
  // in source priority order. A declared ID range may contain rejected samples.
  for (const auto& candidate : candidates) {
    if (std::ranges::any_of(selected, [&](const SampleEntry& sample) { return sample.id() == candidate.id(); })) {
      continue;
    }
    const bool sameSampleSet = requestedSampleSetId.value_or(0) == candidate.data->sampleSetId.value_or(0);
    const bool contributes = std::ranges::any_of(candidate.data->articulations, [&](const auto& articulation) {
      return articulation.sample.valid() && remaining.contains(articulation.articulationId);
    });
    if ((!sameSampleSet || !selected.empty()) && !contributes) {
      continue;
    }
    select(candidate);
    if (remaining.empty()) {
      break;
    }
  }
  // Retain the source table ordering for stable binding of overlapping IDs.
  std::ranges::sort(selected, {}, [](const SampleEntry& sample) { return sample.data->firstArticulationId; });
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
      if (articulation.sample.valid()) {
        articulations[articulation.articulationId] = articulation;
      }
    }
  }
  return articulations;
}

}  // namespace

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
