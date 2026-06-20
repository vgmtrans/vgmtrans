/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/Akao/AkaoResolver.h"

#include "value/formats/Akao/AkaoFacts.h"
#include "value/formats/Akao/AkaoTypes.h"
#include "value/scan/CollectionResolver.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
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

struct InstrumentFactEntry {
  AssetId asset;
  std::optional<SourceId> sourceId;
  u32 sequenceId = 0;
};

struct SampleFactEntry {
  AssetId asset;
  std::optional<SourceId> sourceId;
  const SourceFile* source = nullptr;
  std::optional<u32> sampleSetId;
  u32 firstArt = 0;
  u32 artCount = 0;
  u32 scanOrdinal = 0;
};

[[nodiscard]] bool extensionIsPsf(const std::filesystem::path& path) {
  std::string ext = path.extension().string();
  std::ranges::transform(ext, ext.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return ext == ".psf" || ext == ".minipsf" || ext == ".psflib";
}

[[nodiscard]] bool psfLike(const SourceFile* source) {
  return source != nullptr && extensionIsPsf(source->path);
}

[[nodiscard]] bool covers(const SampleFactEntry& sample, u32 artId) {
  return artId >= sample.firstArt && artId < sample.firstArt + sample.artCount;
}

[[nodiscard]] bool sameSource(const std::optional<SourceId>& a, const std::optional<SourceId>& b) {
  return a && b && *a == *b;
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
      .value = "seq:" + std::to_string(sequence.sequenceId) + ":source:" +
               std::to_string(sequence.sourceId ? sequence.sourceId->value : 0) + ":offset:" +
               std::to_string(sequence.offset),
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
  for (const auto& view : index.formatFacts<SequenceProgramAsset>(kAkaoFormatName, kAkaoFactSequence)) {
    const auto sequenceId = fieldU32(view.payload, kAkaoFieldSequenceId);
    const auto offset = fieldU32(view.payload, kAkaoFieldOffset).value_or(0);
    if (!sequenceId) {
      continue;
    }
    entries.push_back(SequenceFactEntry{
        .asset = view.asset.metadata.id,
        .name = view.asset.metadata.name,
        .sourceId = view.fact.scope.source,
        .source = view.source,
        .sequenceId = *sequenceId,
        .sampleSetId = fieldU32(view.payload, kAkaoFieldSampleSetId),
        .offset = offset,
    });
  }
  std::ranges::sort(entries, {}, [](const SequenceFactEntry& entry) { return entry.asset.value; });
  return entries;
}

[[nodiscard]] std::vector<InstrumentFactEntry> instrumentFacts(const MatchFactIndex& index) {
  std::vector<InstrumentFactEntry> entries;
  for (const auto& view : index.formatFacts<InstrumentSetAsset>(kAkaoFormatName, kAkaoFactInstrumentSet)) {
    const auto sequenceId = fieldU32(view.payload, kAkaoFieldSequenceId);
    if (!sequenceId) {
      continue;
    }
    entries.push_back(InstrumentFactEntry{
        .asset = view.asset.metadata.id,
        .sourceId = view.fact.scope.source,
        .sequenceId = *sequenceId,
    });
  }
  std::ranges::sort(entries, {}, [](const InstrumentFactEntry& entry) { return entry.asset.value; });
  return entries;
}

[[nodiscard]] std::vector<SampleFactEntry> sampleFacts(const MatchFactIndex& index) {
  std::vector<SampleFactEntry> entries;
  for (const auto& view : index.formatFacts<SampleCollectionAsset>(kAkaoFormatName, kAkaoFactSampleCollection)) {
    const auto firstArt = fieldU32(view.payload, kAkaoFieldFirstArt);
    const auto artCount = fieldU32(view.payload, kAkaoFieldArtCount);
    if (!firstArt || !artCount) {
      continue;
    }
    entries.push_back(SampleFactEntry{
        .asset = view.asset.metadata.id,
        .sourceId = view.fact.scope.source,
        .source = view.source,
        .sampleSetId = fieldU32(view.payload, kAkaoFieldSampleSetId),
        .firstArt = *firstArt,
        .artCount = *artCount,
        .scanOrdinal = fieldU32(view.payload, kAkaoFieldScanOrdinal).value_or(0),
    });
  }
  std::ranges::sort(entries, {}, &SampleFactEntry::scanOrdinal);
  return entries;
}

[[nodiscard]] std::map<u32, std::set<u32>> requiredArtFacts(const MatchFactIndex& index) {
  std::map<u32, std::set<u32>> requiredByInstrument;
  for (const auto& view : index.formatFacts<InstrumentSetAsset>(kAkaoFormatName, kAkaoFactRequiredArticulation)) {
    const auto art = fieldU32(view.payload, kAkaoFieldArtId);
    if (!art || *art == 0) {
      continue;
    }
    requiredByInstrument[view.asset.metadata.id.value].insert(*art);
  }
  return requiredByInstrument;
}

[[nodiscard]] std::optional<InstrumentFactEntry> chooseInstrument(const SequenceFactEntry& sequence,
                                                                  const std::vector<InstrumentFactEntry>& instruments) {
  std::optional<InstrumentFactEntry> fallback;
  for (const auto& instrument : instruments) {
    if (instrument.sequenceId != sequence.sequenceId) {
      continue;
    }
    fallback = instrument;
    if (sameSource(sequence.sourceId, instrument.sourceId)) {
      return instrument;
    }
  }
  return fallback;
}

[[nodiscard]] std::vector<SampleFactEntry> candidateSamples(const SequenceFactEntry& sequence,
                                                           const std::vector<SampleFactEntry>& samples) {
  std::vector<SampleFactEntry> candidates;
  const bool isolated = psfLike(sequence.source);
  for (const auto& sample : samples) {
    if (isolated && !sameSource(sequence.sourceId, sample.sourceId)) {
      continue;
    }
    candidates.push_back(sample);
  }
  std::ranges::sort(candidates, std::ranges::greater{}, &SampleFactEntry::scanOrdinal);
  return candidates;
}

void markCoveredArticulations(std::set<u32>& remaining, const SampleFactEntry& sample) {
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
                                                      std::set<u32>& remaining,
                                                      CollectionAssembly& collection) {
  auto candidates = candidateSamples(sequence, samples);
  std::vector<SampleFactEntry> selected;
  if (sequence.sampleSetId && *sequence.sampleSetId > 0) {
    auto preferred = std::ranges::find_if(candidates, [&](const SampleFactEntry& sample) {
      return sample.sampleSetId && *sample.sampleSetId == *sequence.sampleSetId;
    });
    if (preferred != candidates.end()) {
      selected.push_back(*preferred);
      markCoveredArticulations(remaining, *preferred);
    } else if (!psfLike(sequence.source)) {
      collection.incomplete(CollectionIssue{
          .severity = Severity::Warning,
          .code = "missing-preferred-sample-set",
          .message = missingSampleMessage(sequence),
          .asset = sequence.asset,
      });
    }
  }

  for (const auto& sample : candidates) {
    if (std::ranges::find(selected, sample.asset, &SampleFactEntry::asset) != selected.end()) {
      continue;
    }
    const bool associated = sameSampleSet(sequence.sampleSetId, sample.sampleSetId);
    const bool matchesRequired = std::ranges::any_of(remaining, [&](u32 art) { return covers(sample, art); });
    if (!associated && !matchesRequired) {
      continue;
    }
    selected.push_back(sample);
    markCoveredArticulations(remaining, sample);
    if (remaining.empty() && !selected.empty()) {
      break;
    }
  }

  std::ranges::sort(selected, {}, &SampleFactEntry::firstArt);
  return selected;
}

void attachSamplesAndReportGaps(CollectionAssembly& collection, const SequenceFactEntry& sequence,
                                const InstrumentFactEntry& instrument, const std::vector<SampleFactEntry>& selected,
                                const std::set<u32>& remaining) {
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
    for (const u32 art : remaining) {
      message += " " + std::to_string(art);
    }
    collection.incomplete(CollectionIssue{
        .severity = Severity::Warning,
        .code = "missing-articulation-coverage",
        .message = std::move(message),
        .asset = instrument.asset,
    });
  }
}

}  // namespace

std::vector<DesiredCollection> resolveAkaoCollections(const MatchContext& context) {
  const MatchFactIndex index(context);
  const auto sequences = sequenceFacts(index);
  const auto instruments = instrumentFacts(index);
  const auto samples = sampleFacts(index);
  const auto requiredByInstrument = requiredArtFacts(index);

  std::vector<DesiredCollection> collections;
  for (const auto& sequence : sequences) {
    CollectionAssembly collection(collectionKey(sequence), sequence.name.empty() ? "Akao Collection" : sequence.name);
    collection.sequence(sequence.asset);

    const auto instrument = chooseInstrument(sequence, instruments);
    if (!instrument) {
      collection.requireInstrumentSet();
      collections.push_back(std::move(collection).finish());
      continue;
    }
    collection.instrumentSet(instrument->asset);

    std::set<u32> remaining;
    if (auto found = requiredByInstrument.find(instrument->asset.value); found != requiredByInstrument.end()) {
      remaining = found->second;
    }

    const auto selected = chooseSamplesForSequence(sequence, samples, remaining, collection);
    attachSamplesAndReportGaps(collection, sequence, *instrument, selected, remaining);
    collection.requireSequence().requireInstrumentSet().requireSampleCollection();
    collections.push_back(std::move(collection).finish());
  }
  return collections;
}

}  // namespace vgmtrans::formats::akao
