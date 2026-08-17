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
  u64 offset = 0;
  u32 sampleBytes = 0;
  bool needsExternalSamples = false;
};

struct SampleEntry {
  AssetId asset;
  std::optional<SourceId> source;
  const SourceFile* file = nullptr;
  u32 sampleBytes = 0;
};

[[nodiscard]] bool needsExternalSamples(const SoundBankAsset& bank) {
  return std::ranges::any_of(bank.instruments, [](const Instrument& instrument) {
    return std::ranges::any_of(instrument.regions,
                               [](const Region& region) { return region.sample.needsExternalBinding(); });
  });
}

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
  for (const auto& facts : index.assets<SoundBankAsset>(kSonyPs1FormatName)) {
    const auto sampleBytes = facts.id(kSampleBytesFact);
    if (sampleBytes) {
      entries.push_back(InstrumentEntry{
          .asset = facts.asset().metadata.id,
          .source = facts.sourceId,
          .file = facts.source,
          .offset = facts.asset().metadata.range.offset,
          .sampleBytes = *sampleBytes,
          .needsExternalSamples = needsExternalSamples(facts.asset()),
      });
    }
  }
  return entries;
}

[[nodiscard]] std::vector<SampleEntry> samples(const MatchFactIndex& index) {
  std::vector<SampleEntry> entries;
  for (const auto& facts : index.assets<SamplePoolAsset>(kSonyPs1FormatName)) {
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
                                                                    const std::vector<SequenceEntry>& sequenceEntries,
                                                                    const std::vector<InstrumentEntry>& banks) {
  std::vector<const InstrumentEntry*> selected;
  for (const auto& bank : banks) {
    if (sequence.source && bank.source && *sequence.source == *bank.source) {
      selected.push_back(&bank);
    }
  }
  if (!selected.empty()) {
    std::ranges::sort(selected, [](const InstrumentEntry* left, const InstrumentEntry* right) {
      return left->offset > right->offset;
    });
    const size_t rank = std::ranges::count_if(sequenceEntries, [&](const SequenceEntry& candidate) {
      return candidate.source == sequence.source && candidate.offset > sequence.offset;
    });
    return {selected[std::min(rank, selected.size() - 1)]};
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
  collection.soundBank(bank.asset);
  if (bank.needsExternalSamples) {
    if (const auto* body = chooseSamples(bank, bodies)) {
      collection.samplePool(body->asset);
    }
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
    const auto banks = chooseInstruments(sequence, sequenceEntries, instrumentEntries);
    for (const auto* bank : banks) {
      attachBank(collection, *bank, sampleEntries);
    }
    if (banks.empty()) {
      collection.requireSoundBank();
    }
    const bool allBanksHaveSamples = std::ranges::all_of(banks, [&](const InstrumentEntry* bank) {
      return !bank->needsExternalSamples || chooseSamples(*bank, sampleEntries) != nullptr;
    });
    if (!banks.empty() && !allBanksHaveSamples) {
      collection.requireSamplePool();
    }
    collections.push_back(std::move(collection).finish());
  }

  for (const auto& bank : instrumentEntries) {
    const bool pairedWithSequence = std::ranges::any_of(sequenceEntries, [&](const SequenceEntry& sequence) {
      const auto selected = chooseInstruments(sequence, sequenceEntries, instrumentEntries);
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
    if (bank.needsExternalSamples && chooseSamples(bank, sampleEntries) == nullptr) {
      collection.requireSamplePool();
    }
    collections.push_back(std::move(collection).finish());
  }
  return collections;
}

void bindSonyPs1Collection(CollectionBindingContext& context) {
  // Scan-time bank numbers describe every VAB in a source. Collections load
  // their selected VABs into bank slots in member order.
  u32 bank = 0;
  auto samplePool = context.samplePools.begin();
  for (auto& instruments : context.soundBanks) {
    if (instruments.metadata.format != kSonyPs1FormatName) {
      continue;
    }
    for (auto& instrument : instruments.instruments) {
      const u32 program = instrument.explicitAddress ? instrument.explicitAddress->program
                          : instrument.identity      ? instrument.identity->key & 0xff
                                                     : 0;
      instrument.explicitAddress = InstrumentAddress{.bank = bank, .program = program};
      instrument.identity = sonyPs1InstrumentIdentity(static_cast<u16>(bank), static_cast<u8>(program));
    }
    if (needsExternalSamples(instruments)) {
      while (samplePool != context.samplePools.end() && (*samplePool)->metadata.format != kSonyPs1FormatName) {
        ++samplePool;
      }
      if (samplePool == context.samplePools.end()) {
        context.fail("Sony PS1 sound bank has no matching external sample pool", instruments.metadata.range);
        return;
      }
      const auto& pool = **samplePool++;
      for (auto& instrument : instruments.instruments) {
        for (auto& region : instrument.regions) {
          if (!region.sample.needsExternalBinding()) {
            continue;
          }
          if (region.sample.index >= pool.pool.samples.size()) {
            context.fail("Sony PS1 sound bank refers outside its external sample pool", region.range);
            return;
          }
          region.sample = SampleRef::external(pool.metadata.id, region.sample.index);
        }
      }
    }
    ++bank;
  }
}

}  // namespace vgmtrans::formats::sony_ps1
