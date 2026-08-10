/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/SonyPS1/SonyPS1.h"

#include "value/scan/CollectionResolver.h"

#include <algorithm>
#include <filesystem>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace vgmtrans::formats::sony_ps1 {

using namespace core;

namespace {

constexpr std::string_view kSequenceFact = "sony-ps1.sequence";
constexpr std::string_view kSampleBytesFact = "sony-ps1.sample-bytes";
constexpr std::string_view kInstrumentSamplesFact = "sony-ps1.instrument-samples";

struct SequenceEntry {
  AssetId asset;
  std::string name;
  std::optional<SourceId> source;
  const SourceFile* file = nullptr;
  u32 offset = 0;
};

struct InstrumentEntry {
  AssetId asset;
  std::optional<SourceId> source;
  const SourceFile* file = nullptr;
  u32 sampleBytes = 0;
  std::optional<AssetId> samples;
};

struct SampleEntry {
  AssetId asset;
  std::optional<SourceId> source;
  const SourceFile* file = nullptr;
  u32 sampleBytes = 0;
};

[[nodiscard]] std::filesystem::path sourcePath(const SourceFile* source) {
  if (source == nullptr) {
    return {};
  }
  return source->path.empty() ? std::filesystem::path(source->name) : source->path;
}

[[nodiscard]] bool sameDirectory(const SourceFile* left, const SourceFile* right) {
  const auto a = sourcePath(left);
  const auto b = sourcePath(right);
  return !a.empty() && !b.empty() && a.parent_path() == b.parent_path();
}

[[nodiscard]] bool sameStem(const SourceFile* left, const SourceFile* right) {
  const auto a = sourcePath(left);
  const auto b = sourcePath(right);
  return !a.empty() && !b.empty() && a.parent_path() == b.parent_path() && a.stem() == b.stem();
}

[[nodiscard]] std::vector<SequenceEntry> sequences(const MatchFactIndex& index) {
  std::vector<SequenceEntry> entries;
  for (const auto& facts : index.assets<SequenceProgramAsset>(kSonyPs1FormatName)) {
    const auto offset = facts.id(kSequenceFact);
    if (offset) {
      entries.push_back(SequenceEntry{
          .asset = facts.asset().metadata.id,
          .name = facts.asset().metadata.name,
          .source = facts.sourceId,
          .file = facts.source,
          .offset = *offset,
      });
    }
  }
  return entries;
}

[[nodiscard]] std::vector<InstrumentEntry> instruments(const MatchFactIndex& index) {
  std::vector<InstrumentEntry> entries;
  for (const auto& facts : index.assets<InstrumentSetAsset>(kSonyPs1FormatName)) {
    const auto sampleBytes = facts.id(kSampleBytesFact);
    if (sampleBytes) {
      entries.push_back(InstrumentEntry{
          .asset = facts.asset().metadata.id,
          .source = facts.sourceId,
          .file = facts.source,
          .sampleBytes = *sampleBytes,
          .samples = facts.relation(kInstrumentSamplesFact),
      });
    }
  }
  return entries;
}

[[nodiscard]] std::vector<SampleEntry> samples(const MatchFactIndex& index) {
  std::vector<SampleEntry> entries;
  for (const auto& facts : index.assets<SampleCollectionAsset>(kSonyPs1FormatName)) {
    const auto sampleBytes = facts.id(kSampleBytesFact);
    if (sampleBytes) {
      entries.push_back(SampleEntry{
          .asset = facts.asset().metadata.id,
          .source = facts.sourceId,
          .file = facts.source,
          .sampleBytes = *sampleBytes,
      });
    }
  }
  return entries;
}

[[nodiscard]] std::vector<const InstrumentEntry*> chooseInstruments(const SequenceEntry& sequence,
                                                                    const std::vector<InstrumentEntry>& banks) {
  std::vector<const InstrumentEntry*> selected;
  for (const auto& bank : banks) {
    if (sequence.source && bank.source && *sequence.source == *bank.source) {
      selected.push_back(&bank);
    }
  }
  if (!selected.empty()) {
    return selected;
  }
  for (const auto& bank : banks) {
    if (sameDirectory(sequence.file, bank.file)) {
      selected.push_back(&bank);
    }
  }
  if (selected.empty() && banks.size() == 1) {
    selected.push_back(&banks.front());
  }
  return selected;
}

[[nodiscard]] const SampleEntry* chooseSamples(const InstrumentEntry& bank, const std::vector<SampleEntry>& bodies) {
  if (bank.samples) {
    const auto related = std::ranges::find(bodies, *bank.samples, &SampleEntry::asset);
    return related == bodies.end() ? nullptr : &*related;
  }
  std::vector<const SampleEntry*> compatible;
  for (const auto& body : bodies) {
    if (body.sampleBytes == bank.sampleBytes) {
      compatible.push_back(&body);
    }
  }
  const auto byStem =
      std::ranges::find_if(compatible, [&](const SampleEntry* body) { return sameStem(bank.file, body->file); });
  if (byStem != compatible.end()) {
    return *byStem;
  }
  const auto sameSource = std::ranges::find_if(compatible, [&](const SampleEntry* body) {
    return bank.source && body->source && *bank.source == *body->source;
  });
  if (sameSource != compatible.end()) {
    return *sameSource;
  }
  std::vector<const SampleEntry*> local;
  std::ranges::copy_if(compatible, std::back_inserter(local),
                       [&](const SampleEntry* body) { return sameDirectory(bank.file, body->file); });
  if (local.size() == 1) {
    return local.front();
  }
  return compatible.size() == 1 ? compatible.front() : nullptr;
}

void attachBank(CollectionAssembly& collection, const InstrumentEntry& bank, const std::vector<SampleEntry>& bodies) {
  collection.instrumentSet(bank.asset);
  if (const auto* body = chooseSamples(bank, bodies)) {
    collection.sampleCollection(body->asset);
  }
}

}  // namespace

std::vector<DesiredCollection> resolveSonyPs1Collections(const MatchContext& context) {
  const MatchFactIndex index(context);
  const auto sequenceEntries = sequences(index);
  const auto instrumentEntries = instruments(index);
  const auto sampleEntries = samples(index);
  std::vector<DesiredCollection> collections;

  for (const auto& sequence : sequenceEntries) {
    CollectionAssembly collection(
        CollectionKey{
            .resolver = std::string(kSonyPs1CollectionResolver),
            .value = "source:" + std::to_string(sequence.source ? sequence.source->value : 0) +
                     ":sequence:" + std::to_string(sequence.offset),
        },
        sequence.name);
    collection.sequence(sequence.asset);
    const auto banks = chooseInstruments(sequence, instrumentEntries);
    for (const auto* bank : banks) {
      attachBank(collection, *bank, sampleEntries);
    }
    if (banks.empty()) {
      collection.requireInstrumentSet();
    }
    const bool allBanksHaveSamples = std::ranges::all_of(
        banks, [&](const InstrumentEntry* bank) { return chooseSamples(*bank, sampleEntries) != nullptr; });
    if (!banks.empty() && !allBanksHaveSamples) {
      collection.requireSampleCollection();
    }
    collections.push_back(std::move(collection).finish());
  }

  for (const auto& bank : instrumentEntries) {
    const bool pairedWithSequence = std::ranges::any_of(sequenceEntries, [&](const SequenceEntry& sequence) {
      const auto selected = chooseInstruments(sequence, instrumentEntries);
      return std::ranges::find(selected, &bank) != selected.end();
    });
    if (pairedWithSequence) {
      continue;
    }
    CollectionAssembly collection(
        CollectionKey{
            .resolver = std::string(kSonyPs1CollectionResolver),
            .value = "source:" + std::to_string(bank.source ? bank.source->value : 0) +
                     ":bank:" + std::to_string(bank.asset.value),
        },
        bank.file == nullptr ? "Sony PS1 VAB" : bank.file->name);
    attachBank(collection, bank, sampleEntries);
    if (chooseSamples(bank, sampleEntries) == nullptr) {
      collection.requireSampleCollection();
    }
    collections.push_back(std::move(collection).finish());
  }
  return collections;
}

}  // namespace vgmtrans::formats::sony_ps1
