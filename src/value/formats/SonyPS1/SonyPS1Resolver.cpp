/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/SonyPS1/SonyPS1.h"

#include "value/scan/CollectionResolver.h"

#include <algorithm>
#include <filesystem>
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

struct SonySampleBinding {
  AssetId soundBank;
  AssetId samplePool;
};

[[nodiscard]] bool needsExternalSamples(const SoundBankAsset& bank) {
  return std::ranges::any_of(bank.instruments, [](const Instrument& instrument) {
    return std::ranges::any_of(instrument.regions,
                               [](const Region& region) { return region.sample.needsBinding(); });
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

[[nodiscard]] std::vector<const SampleEntry*> chooseSamples(const InstrumentEntry& bank,
                                                            const std::vector<SampleEntry>& bodies) {
  std::vector<const SampleEntry*> selected;
  int bestAffinity = -1;
  for (const auto& body : bodies) {
    if (body.sampleBytes != bank.sampleBytes) {
      continue;
    }
    // Compare successively weaker stem, source, and directory evidence.
    const int affinity = (sameStem(bank.file, body.file) ? 4 : 0) +
                         (bank.source && body.source && *bank.source == *body.source ? 2 : 0) +
                         (sameDirectory(bank.file, body.file) ? 1 : 0);
    if (affinity > bestAffinity) {
      selected.clear();
      bestAffinity = affinity;
    }
    if (affinity == bestAffinity) {
      selected.push_back(&body);
    }
  }
  return selected;
}

void attachBank(CollectionAssembly& collection, const InstrumentEntry& bank, const std::vector<SampleEntry>& bodies,
                std::vector<SonySampleBinding>& bindings) {
  collection.soundBank(bank.asset);
  if (!bank.needsExternalSamples) {
    return;
  }
  const auto candidates = chooseSamples(bank, bodies);
  if (candidates.size() == 1) {
    collection.samplePool(candidates.front()->asset);
    bindings.push_back(SonySampleBinding{.soundBank = bank.asset, .samplePool = candidates.front()->asset});
    return;
  }
  if (!candidates.empty()) {
    collection.ambiguous("Sony PS1 sound bank matches multiple external sample pools", bank.asset);
    return;
  }
  collection.incomplete(CollectionIssue{
      .severity = Severity::Warning,
      .code = "missing-sample-pool",
      .message = "Sony PS1 sound bank has no matching external sample pool",
      .asset = bank.asset,
  });
}

void applySampleBinding(CollectionBindingContext& context, SoundBankAsset& bank, const SamplePoolAsset& pool) {
  for (auto& instrument : bank.instruments) {
    for (auto& region : instrument.regions) {
      if (!region.sample.needsBinding()) {
        continue;
      }
      if (region.sample.index() >= pool.pool.samples.size()) {
        context.fail("Sony PS1 sound bank refers outside its external sample pool", region.range);
        return;
      }
      region.sample = SampleRef::resolved(pool.metadata.id, region.sample.index());
    }
  }
}

void applySonyPs1Bindings(CollectionBindingContext& context, std::span<const SonySampleBinding> bindings) {
  u32 bankNumber = 0;
  for (auto& bank : context.soundBanks) {
    if (bank.metadata.format != kSonyPs1FormatName) {
      continue;
    }
    for (auto& instrument : bank.instruments) {
      const u32 program = instrument.explicitAddress ? instrument.explicitAddress->program
                          : instrument.identity      ? instrument.identity->key & 0xff
                                                     : 0;
      instrument.explicitAddress = InstrumentAddress{.bank = bankNumber, .program = program};
      instrument.identity = sonyPs1InstrumentIdentity(static_cast<u16>(bankNumber), static_cast<u8>(program));
    }
    ++bankNumber;
  }

  for (const auto& binding : bindings) {
    auto* bank = context.soundBank(binding.soundBank);
    const auto* pool = context.samplePool(binding.samplePool);
    if (bank == nullptr || bank->metadata.format != kSonyPs1FormatName) {
      context.fail("Sony PS1 sample binding refers to a missing sound bank");
      return;
    }
    if (pool == nullptr || pool->metadata.format != kSonyPs1FormatName) {
      context.fail("Sony PS1 sample binding refers to a missing sample pool", bank->metadata.range);
      return;
    }
    applySampleBinding(context, *bank, *pool);
    if (context.failed) {
      return;
    }
  }

  for (const auto& bank : context.soundBanks) {
    if (bank.metadata.format == kSonyPs1FormatName && needsExternalSamples(bank)) {
      context.fail("Sony PS1 sound bank has no matching external sample pool", bank.metadata.range);
      return;
    }
  }
}

[[nodiscard]] CollectionBinder sonyPs1Binder(std::vector<SonySampleBinding> bindings) {
  return [bindings = std::move(bindings)](CollectionBindingContext& context) {
    applySonyPs1Bindings(context, bindings);
  };
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
    std::vector<SonySampleBinding> bindings;
    const auto banks = chooseInstruments(sequence, sequenceEntries, instrumentEntries);
    for (const auto* bank : banks) {
      attachBank(collection, *bank, sampleEntries, bindings);
    }
    if (banks.empty()) {
      collection.requireSoundBank();
    }
    collection.bind(sonyPs1Binder(std::move(bindings)));
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
    std::vector<SonySampleBinding> bindings;
    attachBank(collection, bank, sampleEntries, bindings);
    collection.bind(sonyPs1Binder(std::move(bindings)));
    collections.push_back(std::move(collection).finish());
  }
  return collections;
}

void bindSonyPs1Collection(CollectionBindingContext& context) {
  std::vector<SonySampleBinding> bindings;
  for (const auto& bank : context.soundBanks) {
    if (bank.metadata.format != kSonyPs1FormatName || !needsExternalSamples(bank)) {
      continue;
    }
    const auto* bankData = bank.privateData.get<SonyPs1SampleSize>();
    if (bankData == nullptr) {
      context.fail("Sony PS1 sound bank has no retained external sample size", bank.metadata.range);
      return;
    }
    const SamplePoolAsset* selected = nullptr;
    for (const auto* pool : context.samplePools) {
      const auto* poolData = pool->privateData.get<SonyPs1SampleSize>();
      if (pool->metadata.format != kSonyPs1FormatName || poolData == nullptr || poolData->bytes != bankData->bytes) {
        continue;
      }
      if (selected != nullptr) {
        context.fail("Sony PS1 sound bank matches multiple external sample pools", bank.metadata.range);
        return;
      }
      selected = pool;
    }
    if (selected == nullptr) {
      context.fail("Sony PS1 sound bank has no matching external sample pool", bank.metadata.range);
      return;
    }
    bindings.push_back(SonySampleBinding{.soundBank = bank.metadata.id, .samplePool = selected->metadata.id});
  }
  applySonyPs1Bindings(context, bindings);
}

}  // namespace vgmtrans::formats::sony_ps1
